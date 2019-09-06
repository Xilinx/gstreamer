/* GStreamer command line scalable application
 *
 * Copyright (C) 2019 Stéphane Cerveau <scerveau@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>
#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

GST_DEBUG_CATEGORY (scalable_transcoder_debug);
#define GST_CAT_DEFAULT scalable_transcoder_debug

gboolean sQuiet = FALSE;

#define SKIP(c) \
  while (*c) { \
    if ((*c == ' ') || (*c == '\n') || (*c == '\t') || (*c == '\r')) \
      c++; \
    else \
      break; \
  }

typedef struct _GstNLaunchPlayer
{
  GMainLoop *loop;
  guint signal_watch_intr_id;
  GIOChannel *io_stdin;
  GList *branches;
  gboolean interactive;
  GstState pending_state;
  GstState state;
  gboolean auto_play;
  gboolean verbose;
} GstNLaunchPlayer;

typedef struct _GstScalableBranch
{
  GstNLaunchPlayer *player;
  GstElement *pipeline;
  GstState state;
  gboolean buffering;
  gboolean is_live;
  gboolean quiet;
  gchar **exclude_args;
  gulong deep_notify_id;
  gboolean eos;
} GstScalableBranch;

#define PRINT(FMT, ARGS...) do { \
    if (!sQuiet) \
        g_print(FMT "\n", ## ARGS); \
    } while (0)

static void
quit_app (GstNLaunchPlayer * thiz)
{
  if (thiz->loop)
    g_main_loop_quit (thiz->loop);
}

#if defined(G_OS_UNIX) || defined(G_OS_WIN32)
/* As the interrupt handler is dispatched from GMainContext as a GSourceFunc
 * handler, we can react to this by posting a message. */
static gboolean
intr_handler (gpointer user_data)
{
  GstNLaunchPlayer *thiz = (GstNLaunchPlayer *) user_data;

  PRINT ("handling interrupt.");
  quit_app (thiz);
  /* remove signal handler */
  thiz->signal_watch_intr_id = 0;

  return G_SOURCE_REMOVE;
}
#endif

static gboolean
player_is_eos (GstNLaunchPlayer * player)
{
  GList *l;
  GstScalableBranch *branch;
  for (l = player->branches; l; l = l->next) {
    branch = (GstScalableBranch *) l->data;
    if (!branch->eos)
      return FALSE;
  }
  return TRUE;
}

static gboolean
player_is_state (GstNLaunchPlayer * player, GstState state)
{
  GList *l;
  GstScalableBranch *branch;
  for (l = player->branches; l; l = l->next) {
    branch = (GstScalableBranch *) l->data;
    if (branch->state != state)
      return FALSE;
  }
  return TRUE;
}

static gboolean
set_branch_state (GstScalableBranch * branch, GstState state)
{
  gboolean res = TRUE;
  GstStateChangeReturn ret;

  g_assert (branch != NULL);

  ret = gst_element_set_state (branch->pipeline, state);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      PRINT ("ERROR: %s doesn't want to pause.",
          GST_ELEMENT_NAME (branch->pipeline));
      res = FALSE;
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      PRINT ("%s is live and does not need PREROLL ...",
          GST_ELEMENT_NAME (branch->pipeline));
      branch->is_live = TRUE;
      break;
    case GST_STATE_CHANGE_ASYNC:
      PRINT ("%s is PREROLLING ...", GST_ELEMENT_NAME (branch->pipeline));
      break;
      /* fallthrough */
    case GST_STATE_CHANGE_SUCCESS:
      if (branch->state == GST_STATE_PAUSED)
        PRINT ("%s is PREROLLED ...", GST_ELEMENT_NAME (branch->pipeline));
      break;
  }
  return res;
}

static gboolean
set_player_state (GstNLaunchPlayer * player, GstState new_state)
{
  GList *l;
  GstScalableBranch *branch;
  PRINT ("set player state %s", gst_element_state_get_name (new_state));
  for (l = player->branches; l; l = l->next) {
    branch = (GstScalableBranch *) l->data;
    if (!set_branch_state (branch, new_state))
      return FALSE;
  }
  player->pending_state = new_state;
  return TRUE;
}

static void
change_player_state (GstNLaunchPlayer * player, GstState state)
{
  if (player->state == player->pending_state)
    return;

  if (!player_is_state (player, state))
    return;

  player->state = state;
  PRINT ("player is %s", gst_element_state_get_name (state));
  switch (state) {
    case GST_STATE_READY:
      if (player->auto_play)
        set_player_state (player, GST_STATE_PAUSED);
      break;
    case GST_STATE_PAUSED:
      if (player->auto_play)
        set_player_state (player, GST_STATE_PLAYING);
      break;
    case GST_STATE_PLAYING:
      break;
    default:
      break;
  }
}

static gboolean
message_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GstScalableBranch *thiz = (GstScalableBranch *) user_data;
  GST_DEBUG_OBJECT (thiz, "Received new message %s from %s",
      GST_MESSAGE_TYPE_NAME (message), GST_OBJECT_NAME (message->src));
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *name, *debug = NULL;

      name = gst_object_get_path_string (message->src);
      gst_message_parse_error (message, &err, &debug);

      GST_ERROR_OBJECT (thiz, "ERROR: from element %s: %s\n", name,
          err->message);
      if (debug != NULL)
        GST_ERROR_OBJECT (thiz, "Additional debug info:%s", debug);

      g_error_free (err);
      g_free (debug);
      g_free (name);

      g_main_loop_quit (thiz->player->loop);
      break;
    }
    case GST_MESSAGE_WARNING:{
      GError *err = NULL;
      gchar *name, *debug = NULL;

      name = gst_object_get_path_string (message->src);
      gst_message_parse_warning (message, &err, &debug);

      GST_WARNING_OBJECT (thiz, "ERROR: from element %s: %s\n", name,
          err->message);
      if (debug != NULL)
        GST_WARNING_OBJECT (thiz, "Additional debug info:\n%s\n", debug);

      g_error_free (err);
      g_free (debug);
      g_free (name);
      break;
    }
    case GST_MESSAGE_EOS:
      thiz->eos = TRUE;
      if (player_is_eos (thiz->player)) {
        PRINT ("All pipelines are in EOS. Exit.");
        g_main_loop_quit (thiz->player->loop);
      }
      break;

    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState old, new, pending;
      if (GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (thiz->pipeline)) {
        gst_message_parse_state_changed (message, &old, &new, &pending);
        thiz->state = new;
        change_player_state (thiz->player, new);
      }
      break;
    }
    case GST_MESSAGE_BUFFERING:{
      gint percent;

      gst_message_parse_buffering (message, &percent);
      PRINT ("buffering  %d%% ", percent);

      /* no state management needed for live pipelines */
      if (thiz->is_live)
        break;

      if (percent == 100) {
        /* a 100% message means buffering is done */
        thiz->buffering = FALSE;
        /* if the desired state is playing, go back */
        if (thiz->state == GST_STATE_PLAYING) {
          PRINT ("Done buffering, setting pipeline to PLAYING ...");
          gst_element_set_state (thiz->pipeline, GST_STATE_PLAYING);
        }
      } else {
        /* buffering busy */
        if (!thiz->buffering && thiz->state == GST_STATE_PLAYING) {
          /* we were not buffering but PLAYING, PAUSE  the pipeline. */
          PRINT ("Buffering, setting pipeline to PAUSED ...");
          gst_element_set_state (thiz->pipeline, GST_STATE_PAUSED);
        }
        thiz->buffering = TRUE;
      }
      break;
    }
    case GST_MESSAGE_PROPERTY_NOTIFY:{
      const GValue *val;
      const gchar *name;
      GstObject *obj;
      gchar *val_str = NULL;
      gchar **ex_prop, *obj_name;

      if (thiz->quiet)
        break;

      gst_message_parse_property_notify (message, &obj, &name, &val);

      /* Let's not print anything for excluded properties... */
      ex_prop = thiz->exclude_args;
      while (ex_prop != NULL && *ex_prop != NULL) {
        if (g_strcmp0 (name, *ex_prop) == 0)
          break;
        ex_prop++;
      }
      if (ex_prop != NULL && *ex_prop != NULL)
        break;

      obj_name = gst_object_get_path_string (GST_OBJECT (obj));
      if (val != NULL) {
        if (G_VALUE_HOLDS_STRING (val))
          val_str = g_value_dup_string (val);
        else if (G_VALUE_TYPE (val) == GST_TYPE_CAPS)
          val_str = gst_caps_to_string (g_value_get_boxed (val));
        else if (G_VALUE_TYPE (val) == GST_TYPE_TAG_LIST)
          val_str = gst_tag_list_to_string (g_value_get_boxed (val));
        else if (G_VALUE_TYPE (val) == GST_TYPE_STRUCTURE)
          val_str = gst_structure_to_string (g_value_get_boxed (val));
        else
          val_str = gst_value_serialize (val);
      } else {
        val_str = g_strdup ("(no value)");
      }

      PRINT ("%s: %s = %s", obj_name, name, val_str);
      g_free (obj_name);
      g_free (val_str);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static void
destroy_branch (gpointer data)
{
  GstScalableBranch *branch = (GstScalableBranch *) data;
  PRINT ("Destroying %s", GST_ELEMENT_NAME (branch->pipeline));
  gst_element_set_state (branch->pipeline, GST_STATE_READY);
  gst_element_set_state (branch->pipeline, GST_STATE_NULL);
  if (branch->pipeline)
    gst_object_unref (branch->pipeline);
  if (branch->deep_notify_id != 0)
    g_signal_handler_disconnect (branch->pipeline, branch->deep_notify_id);
}

static GstScalableBranch *
add_branch (GstNLaunchPlayer * thiz, gchar * src_desc, gchar * branch_desc,
    gchar * sink_desc)
{
  GstElement *src, *transform, *sink;
  GError *err = NULL;
  GstPad *src_pad = NULL;
  GstPad *sink_pad = NULL;
  GstScalableBranch *branch = NULL;
  GstBus *bus;

  GST_DEBUG ("Add branch with src %s transform %s sink %s",
      src_desc, branch_desc, sink_desc);
  /* create source element and add it to the main pipeline */
  /* create transform bin element and add it to the main pipeline */
  branch = g_new0 (GstScalableBranch, 1);
  branch->pipeline = gst_pipeline_new (NULL);
  branch->state = GST_STATE_NULL;
  if (!src_desc && !sink_desc)
    transform = gst_parse_launch_full (branch_desc, NULL, GST_PARSE_FLAG_NONE,
        &err);
  else
    transform = gst_parse_bin_from_description (branch_desc, TRUE, &err);

  if (err) {
    GST_ERROR_OBJECT (branch,
        "Unable to instantiate the transform branch %s with error %s",
        branch_desc, err->message);
    goto error;
  }
  gst_bin_add (GST_BIN (branch->pipeline), transform);

  if (src_desc) {
    src = gst_element_factory_make (src_desc, NULL);
    if (!src) {
      GST_ERROR_OBJECT (branch, "Unable to create src element %s", src_desc);
      goto error;
    }
    /* retrieve the src pad which will be connected to the transform bin */
    src_pad = gst_element_get_static_pad (src, "src");
    if (!src_pad) {
      GST_ERROR_OBJECT (branch,
          "Unable to retrieve the src pad of src element: %s", src_desc);
      gst_object_unref (src);
      goto error;
    }
    gst_bin_add (GST_BIN (branch->pipeline), src);
    /* retrieve a compatible pad with the src pad */
    sink_pad = gst_element_get_compatible_pad (transform, src_pad, NULL);
    if (!sink_pad) {
      GST_ERROR_OBJECT (branch, "Unable to retreive a sink pad ");
      goto error;
    }
    /* connect src element with transform bin */
    if (GST_PAD_LINK_FAILED (gst_pad_link (src_pad, sink_pad))) {
      GST_ERROR_OBJECT (branch, "Unable to link src to transform");
      goto error;
    }
    gst_object_unref (src_pad);
    src_pad = NULL;
    gst_object_unref (sink_pad);
    sink_pad = NULL;
  }

  if (sink_desc) {
    /* create sink element and add it to the main pipeline */
    sink = gst_element_factory_make (sink_desc, NULL);
    if (!sink) {
      GST_ERROR_OBJECT (branch, "Unable to create sink element %s", sink_desc);
      goto error;
    }
    /* retrieve the sink pad which will be connected to the transform bin */
    sink_pad = gst_element_get_static_pad (sink, "sink");
    if (!sink_pad) {
      GST_ERROR_OBJECT (branch,
          "Unable to retrieve the sink pad of sink element %s", sink_desc);
      gst_object_unref (sink);
      goto error;
    }
    gst_bin_add (GST_BIN (branch->pipeline), sink);
    /* retrieve a compatible pad with the sink pad */
    src_pad = gst_element_get_compatible_pad (transform, sink_pad, NULL);
    if (!src_pad) {
      GST_ERROR_OBJECT (branch, "Unable to get a src pad from transform\n");
      goto error;
    }
    /* connect sink element with transform bin */
    if (GST_PAD_LINK_FAILED (gst_pad_link (src_pad, sink_pad))) {
      GST_ERROR_OBJECT (branch, "Unable to link sink to transform");
      goto error;
    }

    gst_object_unref (src_pad);
    src_pad = NULL;
    gst_object_unref (sink_pad);
    sink_pad = NULL;
  }
  branch->player = thiz;
  bus = gst_pipeline_get_bus (GST_PIPELINE (branch->pipeline));
  g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (message_cb), branch);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (GST_OBJECT (bus));
  if (thiz->verbose) {
    branch->deep_notify_id =
        gst_element_add_property_deep_notify_watch (branch->pipeline, NULL,
        TRUE);
  }
  if (!set_branch_state (branch, GST_STATE_READY))
    goto error;

done:
  return branch;

error:
  if (src_pad)
    gst_object_unref (src_pad);
  if (sink_pad)
    gst_object_unref (sink_pad);
  destroy_branch (branch);
  branch = NULL;
  goto done;
}

/* Process keyboard input */
static gboolean
handle_keyboard (GIOChannel * source, GIOCondition cond,
    GstNLaunchPlayer * thiz)
{
  gchar *str = NULL;
  char op;

  if (g_io_channel_read_line (source, &str, NULL, NULL,
          NULL) == G_IO_STATUS_NORMAL) {

    gchar *cmd = str;
    SKIP (cmd)
        op = *cmd;
    cmd++;
    switch (op) {
      case 'q':
        quit_app (thiz);
        break;
      case 'p':
        if (thiz->state == GST_STATE_PAUSED)
          set_player_state (thiz, GST_STATE_PLAYING);
        else
          set_player_state (thiz, GST_STATE_PAUSED);
        break;
    }
  }
  g_free (str);
  return TRUE;
}

static void
usage (void)
{
  PRINT ("Available commands:\n"
      "  p - Toggle between Play and Pause\n" "  q - Quit");
}

int
main (int argc, char **argv)
{
  int res = EXIT_SUCCESS;
  GError *err = NULL;
  GOptionContext *ctx;
  GstNLaunchPlayer *thiz;
  GstScalableBranch *branch;
  gchar **full_branch_desc_array = NULL;
  gchar **branch_desc;
  gboolean verbose = FALSE;
  gboolean interactive = FALSE;
  GList *l;
  gint repeat = 1;
  gint i = 0;

  GOptionEntry options[] = {
    {"branch", 'b', 0, G_OPTION_ARG_STRING_ARRAY, &full_branch_desc_array,
        "Add a custom branch with gst-launch style description", NULL}
    ,
    {"repeat", 'r', 0, G_OPTION_ARG_INT, &repeat,
        "Repeat the branches n times", NULL}
    ,
    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
        ("Output status information and property notifications"), NULL}
    ,
    {"interactive", 'i', 0, G_OPTION_ARG_NONE, &interactive,
        ("Put on interactive mode with branches in GST_STATE_READY"), NULL}
    ,
    {NULL}
  };

  thiz = g_new0 (GstNLaunchPlayer, 1);

  ctx = g_option_context_new ("[ADDITIONAL ARGUMENTS]");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    GST_ERROR ("Error initializing: %s\n", GST_STR_NULL (err->message));
    res = -1;
    goto done;
  }
  g_option_context_free (ctx);
  thiz->interactive = interactive;
  thiz->verbose = verbose;
  GST_DEBUG_CATEGORY_INIT (scalable_transcoder_debug, "n-launch", 0,
      "gst-n-launch");

  if (!full_branch_desc_array) {
    g_printerr ("Usage: %s -b branch1 \n", argv[0]);
    goto done;
  }

  for (branch_desc = full_branch_desc_array;
      branch_desc != NULL && *branch_desc != NULL; ++branch_desc) {
    for (i = 0; i < repeat; i++) {
      branch = add_branch (thiz, NULL, *branch_desc, NULL);

      if (!branch) {
        res = -2;
        PRINT ("ERROR: unable to add branch-%d \"%s\"", i, *branch_desc);
        goto done;
      }

      thiz->branches = g_list_append (thiz->branches, branch);
    }
  }
  thiz->state = GST_STATE_NULL;
  thiz->pending_state = GST_STATE_READY;
  PRINT ("%d branches created and set state to READY",
      g_list_length (thiz->branches));

  if (interactive) {
    thiz->io_stdin = g_io_channel_unix_new (fileno (stdin));
    g_io_add_watch (thiz->io_stdin, G_IO_IN, (GIOFunc) handle_keyboard, thiz);
    usage ();
  } else
    thiz->auto_play = TRUE;

  thiz->loop = g_main_loop_new (NULL, FALSE);
#ifdef G_OS_UNIX
  thiz->signal_watch_intr_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, thiz);
#endif
  g_main_loop_run (thiz->loop);

  /* No need to see all those pad caps going to NULL etc., it's just noise */

done:
  if (thiz->loop)
    g_main_loop_unref (thiz->loop);

  for (l = thiz->branches; l; l = g_list_next (l)) {
    g_autoptr (GstBus) bus = NULL;

    branch = l->data;
    bus = gst_pipeline_get_bus (GST_PIPELINE (branch->pipeline));

    gst_bus_remove_signal_watch (bus);
  }

  g_list_free_full (thiz->branches, destroy_branch);
  thiz->branches = NULL;
  g_strfreev (full_branch_desc_array);
  g_free (thiz);

  gst_deinit ();
  return res;
}
