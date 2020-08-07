/*
*
* Test application to showcase VCU transcode through appsrc and appsink
*
* Copyright (C) 2020 Xilinx
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/video/video.h>

#include <glib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_VIDEO_WIDTH 3840
#define DEFAULT_VIDEO_HEIGHT 2160
#define DEFAULT_VIDEO_FRAMERATE 60
#define DEFAULT_VIDEO_FORMAT "NV12"
#define DEFAULT_ENCODER_TYPE "hevc"

typedef struct _App App;

struct _App
{
  GstElement *datasrc_pipeline;
  GstElement *filesrc, *parser, *decoder, *appsink;

  GstElement *datasink_pipeline;
  GstElement *appsrc, *encoder, *filesink;

  gboolean is_eos;
  GAsyncQueue *buf_queue;
  GMainLoop *loop;
};

typedef struct
{
  guint width;
  guint height;
  guint framerate;

  gchar *format;
  gchar *enc_type;
  gchar *output_filename;
  gchar *input_filename;
} Config;

/* Globals */
Config config;

static GstPadProbeReturn
appsink_query_cb (GstPad * pad G_GNUC_UNUSED, GstPadProbeInfo * info,
    gpointer user_data G_GNUC_UNUSED)
{
  GstQuery *query = info->data;

  if (GST_QUERY_TYPE (query) != GST_QUERY_ALLOCATION)
    return GST_PAD_PROBE_OK;

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_PAD_PROBE_HANDLED;
}

static GstFlowReturn
on_new_sample_from_sink (GstElement * elt, App * app)
{
  GstSample *sample;
  GstBuffer *buffer;
  GstMemory *mem;
  static int frame_count = 0;

  /* get the sample from appsink */
  sample = gst_app_sink_pull_sample (GST_APP_SINK (elt));
  buffer = gst_sample_get_buffer (sample);
  if (!buffer) {
    g_print ("\nPulled NULL buffer. Exiting...\n");
    return GST_FLOW_EOS;
  }

  mem = gst_buffer_peek_memory (buffer, 0);
  if (mem && gst_is_dmabuf_memory (mem)) {
    /* buffer ref required because we will unref sample */
    g_async_queue_push (app->buf_queue, gst_buffer_ref (buffer));
  } else {
    g_print ("\nPulled non-dmabuf. Exiting...\n");
    return GST_FLOW_EOS;
  }

  /* start datasink_pipeline */
  if (frame_count == 0) {
    g_print ("Changing state of datasink_pipeline to PLAYING...\n");
    gst_element_set_state (app->datasink_pipeline, GST_STATE_PLAYING);
  }

  gst_sample_unref (sample);
  frame_count++;
  g_print ("\rPushed frame: %d into queue", frame_count);
  return GST_FLOW_OK;
}

static void
feed_data (GstElement * appsrc, guint size, App * app)
{
  GstBuffer *buffer;
  GstFlowReturn ret;

  buffer = app->is_eos ? g_async_queue_try_pop (app->buf_queue) :
      g_async_queue_pop (app->buf_queue);

  if (buffer) {
    /* read any amount of data */
    g_signal_emit_by_name (app->appsrc, "push-buffer", buffer, &ret);
    gst_buffer_unref (buffer);
  } else if (app->is_eos) {
    /* no more buffers left in queue and datasrc_pipeline is EOS */
    g_signal_emit_by_name (app->appsrc, "end-of-stream", &ret);
  }

  return;
}

static gboolean
datasink_message (GstBus * bus, GstMessage * message, App * app)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_error ("\nReceived error from datasink_pipeline...\n");
      if (g_main_loop_is_running (app->loop))
        g_main_loop_quit (app->loop);
      break;
    case GST_MESSAGE_EOS:
      g_print ("Received EOS from datasink_pipeline...\n");
      if (g_main_loop_is_running (app->loop))
        g_main_loop_quit (app->loop);
      break;
    default:
      break;
  }
  return TRUE;
}

static gboolean
datasrc_message (GstBus * bus, GstMessage * message, App * app)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_error ("\nReceived error from datasrc_pipeline...\n");
      if (g_main_loop_is_running (app->loop))
        g_main_loop_quit (app->loop);
      break;
    case GST_MESSAGE_EOS:
      g_print ("\nReceived EOS from datasrc_pipeline...\n");
      app->is_eos = TRUE;
      break;
    default:
      break;
  }
  return TRUE;
}

static gboolean
check_parameters (Config * config)
{
  if (!config->enc_type)
    config->enc_type = g_strdup (DEFAULT_ENCODER_TYPE);

  if (!config->format)
    config->format = g_strdup (DEFAULT_VIDEO_FORMAT);

  if (!config->input_filename) {
    g_print
        ("please provide input-filename argument, use --help option for more details\n");
    return FALSE;
  }

  if (!config->output_filename) {
    g_print
        ("please provide output-filename argument, use --help option for more details\n");
    return FALSE;
  }

  return TRUE;
}

int
main (int argc, char *argv[])
{
  App app;
  GstBus *datasrc_bus;
  GstBus *datasink_bus;
  GstPad *pad;
  gchar *datasrc_pipeline_str = NULL;
  gchar *datasink_pipeline_str = NULL;
  GError *error = NULL;
  GOptionContext *context;

  static GOptionEntry entries[] = {
    {"width", 'w', 0, G_OPTION_ARG_INT, &config.width,
          "Width of the video frame",
        NULL},
    {"height", 'h', 0, G_OPTION_ARG_INT, &config.height,
          "Height of the video frame",
        NULL},
    {"framerate", 'f', 0, G_OPTION_ARG_INT, &config.framerate,
          "Video framerate",
        NULL},
    {"video-format", 'c', 0, G_OPTION_ARG_STRING, &config.format,
          "Video color format: use NV12, NV16, NV12_10LE32, or NV16_10LE32",
        NULL},
    {"encoder-type", 'e', 0, G_OPTION_ARG_STRING, &config.enc_type,
          "Encoder codec selection: use avc for H264 and hevc for H265",
        NULL},
    {"output-filename", 'o', 0, G_OPTION_ARG_FILENAME, &config.output_filename,
        "Output filename", NULL},
    {"input-filename", 'i', 0, G_OPTION_ARG_FILENAME, &config.input_filename,
        "Input filename", NULL},
    {NULL}
  };

  const char *summary =
      "Example application to transcode using appsrc and appsink. Pipeline:\n"
      "filesrc -> parse -> dec -> appsink\n"
      "appsrc -> enc -> filesink\n"
      "Example command: zynqmp_appsrc_appsink_transcode -w 3840 -h 2160 -f 60 -o output -i input -e hevc -c NV12";

  /* set default parameters */
  config.width = DEFAULT_VIDEO_WIDTH;
  config.height = DEFAULT_VIDEO_HEIGHT;
  config.framerate = DEFAULT_VIDEO_FRAMERATE;

  context = g_option_context_new ("vcu transcode appsrc/appsink application");
  g_option_context_set_summary (context, summary);
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gst_init_get_option_group ());
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_print ("option parsing failed: %s\n", error->message);
    g_option_context_free (context);
    g_clear_error (&error);
    return -1;
  }
  g_option_context_free (context);

  if (!check_parameters (&config))
    return -1;

  /* initialize Gstreamer library. It'll also parse the GStreamer-specific command line options */
  gst_init (&argc, &argv);

  app.is_eos = FALSE;
  app.loop = g_main_loop_new (NULL, FALSE);
  app.buf_queue = g_async_queue_new ();

  if (!g_strcmp0 (config.enc_type, "hevc")) {
    datasrc_pipeline_str =
        g_strdup_printf
        ("filesrc location=\"%s\" ! h264parse ! omxh264dec ! appsink name=sink",
        config.input_filename);
    datasink_pipeline_str =
        g_strdup_printf
        ("appsrc is-live=true block=true name=source caps=video/x-raw,format=%s,width=%d,height=%d,framerate=%d/1 ! omxh265enc ! h265parse ! filesink location=\"%s\"",
        config.format, config.width, config.height, config.framerate,
        config.output_filename);
  } else {
    datasrc_pipeline_str =
        g_strdup_printf
        ("filesrc location=\"%s\" ! h265parse ! omxh265dec ! appsink name=sink",
        config.input_filename);
    datasink_pipeline_str =
        g_strdup_printf
        ("appsrc is-live=true block=true name=source caps=video/x-raw,format=%s,width=%d,height=%d,framerate=%d/1 ! omxh264enc ! h264parse ! filesink location=\"%s\"",
        config.format, config.width, config.height, config.framerate,
        config.output_filename);
  }

  app.datasrc_pipeline = gst_parse_launch (datasrc_pipeline_str, NULL);
  if (app.datasrc_pipeline == NULL) {
    g_print ("*** Bad datasrc_pipeline ***\n");
    return -1;
  }
  free (datasrc_pipeline_str);

  app.datasink_pipeline = gst_parse_launch (datasink_pipeline_str, NULL);
  if (app.datasink_pipeline == NULL) {
    g_print ("*** Bad datasink_pipeline_str ***\n");
    return -1;
  }
  free (datasink_pipeline_str);

  datasrc_bus = gst_element_get_bus (app.datasrc_pipeline);
  datasink_bus = gst_element_get_bus (app.datasink_pipeline);

  /* add watch for messages */
  gst_bus_add_watch (datasrc_bus, (GstBusFunc) datasrc_message, &app);
  gst_bus_add_watch (datasink_bus, (GstBusFunc) datasink_message, &app);

  /* set up appsink */
  app.appsink = gst_bin_get_by_name (GST_BIN (app.datasrc_pipeline), "sink");
  g_object_set (G_OBJECT (app.appsink), "emit-signals", TRUE, "sync", FALSE,
      NULL);
  g_signal_connect (app.appsink, "new-sample",
      G_CALLBACK (on_new_sample_from_sink), &app);

  /* Implement the allocation query using a pad probe. This probe will
   * adverstize support for GstVideoMeta, which avoid hardware accelerated
   * decoder that produce special strides and offsets from having to
   * copy the buffers.
   */
  pad = gst_element_get_static_pad (app.appsink, "sink");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, appsink_query_cb,
      NULL, NULL);
  gst_object_unref (pad);

  /* set up appsrc */
  app.appsrc = gst_bin_get_by_name (GST_BIN (app.datasink_pipeline), "source");
  g_object_set (G_OBJECT (app.appsrc), "stream-type", 0, NULL);
  g_signal_connect (app.appsrc, "need-data", G_CALLBACK (feed_data), &app);

  g_print ("Changing state of datasrc_pipeline to PLAYING...\n");

  /* only start datasrc_pipeline to ensure we have enough data before
   * starting datasink_pipeline
   */
  gst_element_set_state (app.datasrc_pipeline, GST_STATE_PLAYING);

  g_main_loop_run (app.loop);

  g_print ("stopping...\n");

  gst_element_set_state (app.datasrc_pipeline, GST_STATE_NULL);
  gst_element_set_state (app.datasink_pipeline, GST_STATE_NULL);

  gst_object_unref (datasrc_bus);
  gst_object_unref (datasink_bus);
  g_main_loop_unref (app.loop);
  g_async_queue_unref (app.buf_queue);

  g_print ("Exiting application...\n");

  return 0;
}
