/*
*
* Test application to showcase VCU2 SEI extraction
*
* Copyright (C) 2024 AMD
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

static guint frame_count = 0;

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

    case GST_MESSAGE_APPLICATION:{
      const GstStructure *s;
      guint sei_type;
      GstBuffer *buf;
      gboolean prefix;

      s = gst_message_get_structure (msg);
      if (!gst_structure_has_name (s, "omx-alg/sei-parsed"))
        return TRUE;

      g_assert (gst_structure_get (s, "payload-type", G_TYPE_UINT, &sei_type,
              "payload", GST_TYPE_BUFFER, &buf, "prefix",
              G_TYPE_BOOLEAN, &prefix, NULL));

      g_print ("Parsed SEI %s after frame %u (type=%d size=%" G_GSIZE_FORMAT
          "):\n", prefix ? "prefix" : "suffix", frame_count, sei_type,
          gst_buffer_get_size (buf));

      gst_util_dump_buffer (buf);

      gst_buffer_unref (buf);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static GstPadProbeReturn
sink_buffer_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  frame_count++;
  return GST_PAD_PROBE_OK;
}

int
main (int argc, char *argv[])
{
  GError *error = NULL;
  GMainLoop *loop;
  GstElement *pipeline, *sink;
  GstPad *pad;
  gchar *desc;
  GstBus *bus;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_error ("Missing HEVC file\n");
    return 1;
  }

  loop = g_main_loop_new (NULL, FALSE);

  g_print ("Opening %s\n", argv[1]);
  desc =
      g_strdup_printf
      ("filesrc location=%s ! h265parse ! omxh265dec ! fakevideosink name=sink",
      argv[1]);
  pipeline = gst_parse_launch (desc, &error);
  g_assert_no_error (error);
  g_free (desc);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, bus_call, loop);

  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  pad = gst_element_get_static_pad (sink, "sink");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, sink_buffer_cb, NULL,
      NULL);
  gst_object_unref (pad);
  gst_object_unref (sink);

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
