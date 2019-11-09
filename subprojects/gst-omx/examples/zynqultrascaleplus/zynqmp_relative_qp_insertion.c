#include <gst/gst.h>
#include "OMX_VideoExt.h"

#define QP_BUF_OFFSET 64

typedef struct
{
  gint delta_qp;
  guint period;
  guint gdr_mode;
  gboolean isAVC;
} GDRSettings;

GDRSettings gdr_settings;

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static gboolean
send_downstream_event (GstPad * pad, GstStructure * s)
{
  GstEvent *event;
  GstPad *peer;

  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);
  peer = gst_pad_get_peer (pad);

  if (!gst_pad_send_event (peer, event))
    g_printerr ("Failed to send custom event\n");

  gst_object_unref (peer);

  return TRUE;
}

static guint8 *
load_qps (gint * size, int frame_count, int height, int width)
{
  guint8 *qp_table;
  gint num_to_read = 0, num_read = 0, block_size = 0, num_to_read_w = 0,
      num_to_read_h = 0, period_location = frame_count % gdr_settings.period;

  /* Calculate QP table size */
  if (gdr_settings.isAVC)
    block_size = 16;
  else
    block_size = 32;

  num_to_read_w = ((width / block_size) + ((width % block_size) != 0));
  num_to_read_h = ((height / block_size) + ((height % block_size) != 0));
  num_to_read = (num_to_read_w * num_to_read_h) + QP_BUF_OFFSET;
  num_read = QP_BUF_OFFSET;

  qp_table = g_malloc0 (num_to_read);
  if (!qp_table) {
    g_print ("malloc fail...Continuing without loading QP\n");
    return NULL;
  }

  /* Fill out QP table vertically or horizontally based on gdr mode */
  if (gdr_settings.gdr_mode == OMX_ALG_GDR_VERTICAL) {
    if (period_location < num_to_read_w) {
      num_read += period_location;
      for (int i = 0; i < num_to_read_h; i++) {
        qp_table[i * num_to_read_w + num_read] = gdr_settings.delta_qp & 0x3F;
      }
    }
  } else if (gdr_settings.gdr_mode == OMX_ALG_GDR_HORIZONTAL) {
    if (period_location < num_to_read_h) {
      num_read += period_location * num_to_read_w;
      for (int i = 0; i < num_to_read_w; i++) {
        qp_table[i + num_read] = gdr_settings.delta_qp & 0x3F;
      }
    }
  }

  *size = num_to_read;
  return qp_table;
}

static GstPadProbeReturn
v4l2src_src_buffer_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  gint buf_len, height, width;
  GstCaps *caps;
  GstStructure *structure;
  guint8 *qp_table;
  static int frame_count = 0;

  /* Obtain resolution for QP table calculation */
  caps = gst_pad_get_current_caps (pad);
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "height", &height);
  gst_structure_get_int (structure, "width", &width);
  gst_caps_unref (caps);

  /* Create and fill out QP table */
  qp_table = load_qps (&buf_len, frame_count, height, width);

  /* Send event */
  if (qp_table) {
    GstStructure *s;
    GstBuffer *buf;

    buf = gst_buffer_new_wrapped (qp_table, buf_len);

    s = gst_structure_new ("omx-alg/load-qp",
        "qp-table", GST_TYPE_BUFFER, buf, NULL);
    send_downstream_event (pad, s);
    gst_buffer_unref (buf);
  }

  if (!frame_count)
    g_print ("Probe successful - begin loading QPs into intra strip\n");

  frame_count++;
  return GST_PAD_PROBE_OK;
}


int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GstElement *pipeline, *source, *enc;
  GstPad *pad;
  GstBus *bus;
  GOptionContext *context;
  gchar **argvn;
  gboolean verbose = FALSE;
  GError *error = NULL;

  /* Options */
  GOptionEntry entries[] = {
    {"delta-qp", 'd', 0, G_OPTION_ARG_INT,
        &gdr_settings.delta_qp, "Delta QP of intra strip", NULL},
    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
        "Output status information and property notifications", NULL},
    {NULL}
  };

  context =
      g_option_context_new
      (" v4l2src name=src [PROPERTIES?] ! ... ! omxh264enc/omxh265enc name=enc [PROPERTIES?] ! [REST OF PIPELINE] ");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gst_init_get_option_group ());
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_print ("option parsing failed: %s\n", error->message);
    g_option_context_free (context);
    g_clear_error (&error);
    return -1;
  }
  g_option_context_free (context);

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_error ("Missing pipeline\n");
    return 1;
  }

  loop = g_main_loop_new (NULL, FALSE);

  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));
  pipeline = (GstElement *) gst_parse_launchv ((const gchar **) argvn, &error);
  g_assert_no_error (error);

  /* Support for verbose */
  if (verbose) {
    g_signal_connect (pipeline, "deep-notify",
        G_CALLBACK (gst_object_default_deep_notify), NULL);
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, bus_call, loop);

  /* Check if using AVC or HEVC  */
  for (int i = 1; i < argc; i++) {
    if (g_strcmp0 (argv[i], "omxh264enc")) {
      gdr_settings.isAVC = TRUE;
      break;
    }
  }

  /* Check if QP is within relative bounds  */
  if (gdr_settings.delta_qp > 31 || gdr_settings.delta_qp < -32) {
    g_print
        ("Relative delta_qp out of range (Must be within [-32,31])  - Setting to 0\n");
    gdr_settings.delta_qp = 0;
  }

  /* Check for enc element to obtain periodicity and gdr-mode */
  enc = gst_bin_get_by_name (GST_BIN (pipeline), "enc");
  if (enc) {
    g_object_get (G_OBJECT (enc), "periodicity-idr", &gdr_settings.period,
        "gdr-mode", &gdr_settings.gdr_mode, NULL);
    gst_object_unref (enc);
  } else {
    g_print ("No element named enc - Exiting\n");
    return -1;
  }

  /* Check for src element to send event from src to enc to load QPs */
  source = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  if (source) {
    pad = gst_element_get_static_pad (source, "src");
    gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, v4l2src_src_buffer_probe,
        NULL, NULL);
    gst_object_unref (pad);
    gst_object_unref (source);
  } else {
    g_print ("No source named src - Skip inserting QP for intra strip\n");
  }

  g_print ("Running...\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Deleting pipeline\n");

  gst_object_unref (pipeline);
  gst_bus_remove_watch (bus);
  gst_object_unref (bus);
  g_main_loop_unref (loop);
  gst_deinit ();

  return 0;
}
