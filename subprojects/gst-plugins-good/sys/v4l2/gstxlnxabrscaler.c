/*
 * Copyright (C) 2017 – 2018 Xilinx, Inc.
 *     Author: Naveen Cherukuri <naveenc@xilinx.com>
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
 *
 */
/**
 * SECTION:xlnxabrscaler
 *
 * This element can be used scale raw video to multiple resolutions (max 8)
 * using Xilinx's Multiscaler V4L2 based driver.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 videotestsrc ! \
 * "video/x-raw, width=1920, height=1080, format=RGB, framerate=30/1" ! \
 * xlnxabrscaler device="/dev/video0" name=sc \
 * sc.src_0 ! "video/x-raw, width=1600, height=900, format=RGB" ! queue ! fakesink \
 * sc.src_1 ! "video/x-raw, width=1280, height=720, format=RGB" ! queue ! fakesink \
 * sc.src_2 ! "video/x-raw, width=800, height=600, format=RGB" ! queue ! fakesink \
 * sc.src_3 ! "video/x-raw, width=832, height=480, format=RGB" ! queue ! fakesink \
 * sc.src_4 ! "video/x-raw, width=640, height=480, format=RGB" ! queue ! fakesink \
 * sc.src_5 ! "video/x-raw, width=480, height=320, format=RGB" ! queue ! fakesink \
 * sc.src_6 ! "video/x-raw, width=320, height=240, format=RGB" ! queue ! fakesink \
 * sc.src_7 ! "video/x-raw, width=176, height=144, format=RGB" ! queue ! fakesink -v
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <stdio.h>

#include "gstxlnxabrscaler.h"
#include "gstv4l2object.h"

GST_DEBUG_CATEGORY_STATIC (gst_xlnx_abr_scaler_debug);
#define GST_CAT_DEFAULT gst_xlnx_abr_scaler_debug

#define DEFAULT_MULTI_SCALER_DEVICE "/dev/video0"
#define MULTI_SCALER_MAX_OUTPUTS 8
#define DRIVER_NAME "xm2msc"
#define DEFAULT_PROP_IO_MODE GST_V4L2_IO_DMABUF

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_OUTPUT_IO_MODE,
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{RGB, YUY2, NV16, GRAY8, BGRx, UYVY, BGR, NV12}"))
    );

static GstStaticPadTemplate src_request_template =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{RGB, YUY2, NV16, GRAY8, BGRx, UYVY, BGR, NV12}"))
    );

#define gst_xlnx_abr_scaler_parent_class parent_class
G_DEFINE_TYPE (GstXlnxABRScaler, gst_xlnx_abr_scaler, GST_TYPE_ELEMENT);

GType gst_xlnx_abr_scaler_pad_get_type (void);

typedef struct _GstXlnxABRScalerPad GstXlnxABRScalerPad;
typedef struct _GstXlnxABRScalerPadClass GstXlnxABRScalerPadClass;

struct _GstXlnxABRScalerPad
{
  GstPad parent;

  guint index;

  /* < private > */
  GstV4l2Object *v4l2output;
  GstV4l2Object *v4l2capture;

  /* pads */
  GstCaps *probed_srccaps;
  GstCaps *probed_sinkcaps;

  /* Selected caps */
  GstCaps *incaps;
  GstCaps *outcaps;

  /*capture buffers */
  guint min_buffers;
  guint max_buffers;
};

struct _GstXlnxABRScalerPadClass
{
  GstPadClass parent;
};

#define GST_TYPE_XLNX_ABR_SCALER_PAD \
  (gst_xlnx_abr_scaler_pad_get_type())
#define GST_XLNX_ABR_SCALER_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_XLNX_ABR_SCALER_PAD, \
    GstXlnxABRScalerPad))
#define GST_XLNX_ABR_SCALER_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_XLNX_ABR_SCALER_PAD, \
    GstXlnxABRScalerPadClass))
#define GST_IS_XLNX_ABR_SCALER_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_XLNX_ABR_SCALER_PAD))
#define GST_IS_XLNX_ABR_SCALER_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_XLNX_ABR_SCALER_PAD))
#define GST_XLNX_ABR_SCALER_PAD_CAST(obj) \
  ((GstXlnxABRScalerPad *)(obj))

G_DEFINE_TYPE (GstXlnxABRScalerPad, gst_xlnx_abr_scaler_pad, GST_TYPE_PAD);

#define gst_xlnx_abr_scaler_srcpad_at_index(self, idx) \
	((GstXlnxABRScalerPad *)(g_list_nth ((self)->srcpads, idx))->data)

static void
gst_xlnx_abr_scaler_pad_class_init (GstXlnxABRScalerPadClass * klass)
{
  /* nothing */
}

static void
gst_xlnx_abr_scaler_pad_init (GstXlnxABRScalerPad * pad)
{
  pad->v4l2output = NULL;
  pad->v4l2capture = NULL;
  pad->min_buffers = 0;
  pad->max_buffers = 0;
}

static void gst_xlnx_abr_scaler_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_xlnx_abr_scaler_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_xlnx_abr_scaler_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_xlnx_abr_scaler_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static gboolean gst_xlnx_abr_scaler_sink_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static GstStateChangeReturn gst_xlnx_abr_scaler_change_state
    (GstElement * element, GstStateChange transition);
static GstCaps *gst_xlnx_abr_scaler_transform_caps (GstXlnxABRScaler * self,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstPad *gst_xlnx_abr_scaler_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name_templ, const GstCaps * caps);
static void gst_xlnx_abr_scaler_release_pad (GstElement * element,
    GstPad * pad);

GType
abr_scaler_io_mode_get_type (void)
{
  static const GEnumValue io_modes[] = {
    {GST_V4L2_IO_DMABUF, "GST_V4L2_IO_DMABUF", "dmabuf"},
    {GST_V4L2_IO_DMABUF_IMPORT, "GST_V4L2_IO_DMABUF_IMPORT", "dmabuf-import"},
    {0, NULL, NULL}
  };
  static volatile GType abr_io_mode = 0;

  if (g_once_init_enter ((gsize *) & abr_io_mode)) {
    GType _id;

    _id = g_enum_register_static ("GstABRIOMode", io_modes);

    g_once_init_leave ((gsize *) & abr_io_mode, _id);
  }
  return abr_io_mode;
}

static gboolean
gst_xlnx_abr_scaler_open (GstXlnxABRScaler * self)
{
  int idx;
  GstXlnxABRScalerPad *srcpad = NULL;
  guint start_devidx = 0;
  GstV4l2Error error = GST_V4L2_ERROR_INIT;

  if (!self->videodev) {
    GST_ERROR_OBJECT (self, "videodev node is not set");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Opening");

  sscanf (self->videodev, "/dev/video%u", &start_devidx);

  for (idx = 0; idx < g_list_length (self->srcpads); idx++) {
    char videodev[50] = { 0 };

    srcpad = gst_xlnx_abr_scaler_srcpad_at_index (self, idx);
    snprintf (videodev, 50, "/dev/video%d", start_devidx + srcpad->index);

    GST_INFO_OBJECT (self, "creating v4l2 objects for node %s", videodev);

    srcpad->v4l2output = gst_v4l2_object_new (GST_ELEMENT (self),
        GST_OBJECT (GST_BASE_TRANSFORM_SINK_PAD (self)),
        V4L2_BUF_TYPE_VIDEO_OUTPUT, videodev,
        gst_v4l2_get_output, gst_v4l2_set_output, NULL);
    srcpad->v4l2output->no_initial_format = TRUE;
    srcpad->v4l2output->keep_aspect = FALSE;
    srcpad->v4l2output->req_mode = idx ? GST_V4L2_IO_DMABUF_IMPORT :
        self->output_req_mode;

    srcpad->v4l2capture = gst_v4l2_object_new (GST_ELEMENT (self),
        GST_OBJECT (GST_BASE_TRANSFORM_SRC_PAD (self)),
        V4L2_BUF_TYPE_VIDEO_CAPTURE, videodev,
        gst_v4l2_get_input, gst_v4l2_set_input, NULL);
    srcpad->v4l2capture->no_initial_format = TRUE;
    srcpad->v4l2capture->keep_aspect = FALSE;
    srcpad->v4l2capture->req_mode = GST_V4L2_IO_DMABUF;

    if (!gst_v4l2_object_open (srcpad->v4l2output, &error)) {
      gst_v4l2_error (srcpad->v4l2output, &error);
      goto failure;
    }

    GST_INFO_OBJECT (self, "ABR Scaler driver name = %s",
        srcpad->v4l2output->vcap.driver);

    if (!g_str_equal (srcpad->v4l2output->vcap.driver, DRIVER_NAME)) {
      GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
          ("Wrong driver: %s (expected: %s)", srcpad->v4l2output->vcap.driver,
              DRIVER_NAME), (NULL));
      goto failure;
    }

    if (!gst_v4l2_object_open_shared (srcpad->v4l2capture, srcpad->v4l2output))
      goto failure;

    srcpad->probed_sinkcaps = gst_v4l2_object_get_caps (srcpad->v4l2output,
        gst_v4l2_object_get_raw_caps ());

    GST_DEBUG_OBJECT (srcpad, "probed sinkcaps %p %" GST_PTR_FORMAT,
        srcpad->probed_sinkcaps, srcpad->probed_sinkcaps);

    if (gst_caps_is_empty (srcpad->probed_sinkcaps))
      goto no_input_format;

    srcpad->probed_srccaps = gst_v4l2_object_get_caps (srcpad->v4l2capture,
        gst_v4l2_object_get_raw_caps ());

    GST_DEBUG_OBJECT (srcpad, "probed src caps %p %" GST_PTR_FORMAT,
        srcpad->probed_srccaps, srcpad->probed_srccaps);

    if (gst_caps_is_empty (srcpad->probed_srccaps))
      goto no_output_format;
  }

  return TRUE;

no_input_format:
  GST_ERROR_OBJECT (self, "Converter on device has no supported input format");
  goto failure;

no_output_format:
  GST_ERROR_OBJECT (self, "Converter on device has no supported output format");
  goto failure;

failure:
  if (GST_V4L2_IS_OPEN (srcpad->v4l2output))
    gst_v4l2_object_close (srcpad->v4l2output);

  if (GST_V4L2_IS_OPEN (srcpad->v4l2capture))
    gst_v4l2_object_close (srcpad->v4l2capture);

  gst_caps_replace (&srcpad->probed_srccaps, NULL);
  gst_caps_replace (&srcpad->probed_sinkcaps, NULL);

  return FALSE;
}

static void
gst_xlnx_abr_scaler_close (GstXlnxABRScaler * self)
{
  GstXlnxABRScalerPad *srcpad = NULL;
  guint idx;

  GST_DEBUG_OBJECT (self, "Closing");

  for (idx = 0; idx < g_list_length (self->srcpads); idx++) {
    srcpad = gst_xlnx_abr_scaler_srcpad_at_index (self, idx);

    gst_v4l2_object_close (srcpad->v4l2output);
    gst_v4l2_object_close (srcpad->v4l2capture);

    gst_caps_replace (&srcpad->probed_srccaps, NULL);
    gst_caps_replace (&srcpad->probed_sinkcaps, NULL);

    gst_v4l2_object_destroy (srcpad->v4l2capture);
    gst_v4l2_object_destroy (srcpad->v4l2output);
  }
}

static void
gst_xlnx_abr_scaler_finalize (GObject * object)
{
  GstXlnxABRScaler *self = GST_XLNX_ABR_SCALER (object);

  g_hash_table_unref (self->pad_indexes);

  if (self->videodev)
    g_free (self->videodev);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_xlnx_abr_scaler_class_init (GstXlnxABRScalerClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_xlnx_abr_scaler_debug, "xlnxabrscaler",
      0, "Xilinx's ABR Scaler");

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_xlnx_abr_scaler_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_xlnx_abr_scaler_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_xlnx_abr_scaler_finalize);

  gst_element_class_set_details_simple (gstelement_class,
      "Xilinx ABR Scaler",
      "Filter/Converter/Video/Scaler",
      "Adaptive Bit Rate (ABR) scaler using V4L2 API",
      "Naveen Cherukuri <naveenc@xilinx.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &src_request_template);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_xlnx_abr_scaler_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_xlnx_abr_scaler_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_xlnx_abr_scaler_release_pad);

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device", "Device location",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_OUTPUT_IO_MODE,
      g_param_spec_enum ("output-io-mode", "Output IO mode",
          "Output side I/O mode (matches sink pad)",
          GST_TYPE_ABR_IO_MODE, DEFAULT_PROP_IO_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_xlnx_abr_scaler_init (GstXlnxABRScaler * self)
{
  self->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xlnx_abr_scaler_sink_event));
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xlnx_abr_scaler_chain));
  gst_pad_set_query_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xlnx_abr_scaler_sink_query));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->next_pad_index = 0;
  self->num_request_pads = 0;
  self->pad_indexes = g_hash_table_new (NULL, NULL);
  self->videodev = NULL;
  self->srcpads = NULL;
  self->output_req_mode = GST_V4L2_IO_DMABUF;
}

static GstPad *
gst_xlnx_abr_scaler_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name_templ, const GstCaps * caps)
{
  GstXlnxABRScaler *self = GST_XLNX_ABR_SCALER (element);
  gchar *name;
  GstPad *srcpad;
  guint index = 0;

  GST_DEBUG_OBJECT (self, "requesting pad");

  GST_OBJECT_LOCK (self);

  if (GST_STATE (self) > GST_STATE_NULL) {
    GST_ERROR_OBJECT (self, "adding pads is supported only when state is NULL");
    return NULL;
  }

  if (self->num_request_pads == MULTI_SCALER_MAX_OUTPUTS) {
    GST_ERROR_OBJECT (self, "reached maximum scaler channels");
    GST_OBJECT_UNLOCK (self);
    return NULL;
  }

  if (name_templ && sscanf (name_templ, "src_%u", &index) == 1) {
    GST_LOG_OBJECT (element, "name: %s (index %d)", name_templ, index);
    if (g_hash_table_contains (self->pad_indexes, GUINT_TO_POINTER (index))) {
      GST_ERROR_OBJECT (element, "pad name %s is not unique", name_templ);
      GST_OBJECT_UNLOCK (self);
      return NULL;
    }
  } else {
    GST_ERROR_OBJECT (element, "incorrect padname : %s", name_templ);
    GST_OBJECT_UNLOCK (self);
    return NULL;
  }

  g_hash_table_insert (self->pad_indexes, GUINT_TO_POINTER (index), NULL);

  name = g_strdup_printf ("src_%u", index);

  srcpad = GST_PAD_CAST (g_object_new (GST_TYPE_XLNX_ABR_SCALER_PAD,
          "name", name, "direction", templ->direction, "template", templ,
          NULL));
  GST_XLNX_ABR_SCALER_PAD_CAST (srcpad)->index = index;
  g_free (name);

  self->srcpads = g_list_append (self->srcpads,
      GST_XLNX_ABR_SCALER_PAD_CAST (srcpad));
  self->num_request_pads++;

  GST_OBJECT_UNLOCK (self);

  gst_element_add_pad (GST_ELEMENT_CAST (self), srcpad);

  return srcpad;
}

static void
gst_xlnx_abr_scaler_release_pad (GstElement * element, GstPad * pad)
{
  GstXlnxABRScaler *self;
  guint index;
  GList *lsrc = NULL;

  self = GST_XLNX_ABR_SCALER (element);

  GST_OBJECT_LOCK (self);

  if (GST_STATE (self) > GST_STATE_NULL) {
    GST_ERROR_OBJECT (self, "adding pads is supported only when state is NULL");
    return;
  }

  lsrc = g_list_find (self->srcpads, GST_XLNX_ABR_SCALER_PAD_CAST (pad));
  if (!lsrc) {
    GST_ERROR_OBJECT (self, "could not find pad to release");
    return;
  }
  self->srcpads =
      g_list_remove (self->srcpads, GST_XLNX_ABR_SCALER_PAD_CAST (pad));
  index = GST_XLNX_ABR_SCALER_PAD_CAST (pad)->index;
  GST_DEBUG_OBJECT (self, "releasing pad with index = %d", index);

  GST_OBJECT_UNLOCK (self);

  gst_object_ref (pad);
  gst_element_remove_pad (GST_ELEMENT_CAST (self), pad);

  gst_pad_set_active (pad, FALSE);

  gst_object_unref (pad);

  self->num_request_pads--;

  GST_OBJECT_LOCK (self);
  g_hash_table_remove (self->pad_indexes, GUINT_TO_POINTER (index));
  GST_OBJECT_UNLOCK (self);
}

static void
gst_xlnx_abr_scaler_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstXlnxABRScaler *self = GST_XLNX_ABR_SCALER (object);
  switch (prop_id) {
    case PROP_OUTPUT_IO_MODE:
      self->output_req_mode = g_value_get_enum (value);
      break;
    case PROP_DEVICE:
      g_free (self->videodev);
      self->videodev = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_xlnx_abr_scaler_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstXlnxABRScaler *self = GST_XLNX_ABR_SCALER (object);

  switch (prop_id) {
    case PROP_OUTPUT_IO_MODE:
      g_value_set_enum (value, self->output_req_mode);
      break;
    case PROP_DEVICE:
      g_value_set_string (value, self->videodev);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_xlnx_abr_scaler_change_state (GstElement * element,
    GstStateChange transition)
{
  GstXlnxABRScaler *self = GST_XLNX_ABR_SCALER (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      self->do_init = TRUE;
      if (!gst_xlnx_abr_scaler_open (self))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      GstXlnxABRScalerPad *srcpad = NULL;
      guint idx;

      for (idx = 0; idx < g_list_length (self->srcpads); idx++) {
        srcpad = gst_xlnx_abr_scaler_srcpad_at_index (self, idx);

        gst_v4l2_object_unlock (srcpad->v4l2output);
        gst_v4l2_object_unlock (srcpad->v4l2capture);

        gst_v4l2_object_stop (srcpad->v4l2output);
        gst_v4l2_object_stop (srcpad->v4l2capture);

        gst_caps_replace (&srcpad->incaps, NULL);
        gst_caps_replace (&srcpad->outcaps, NULL);
      }
    }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_xlnx_abr_scaler_close (self);
      break;
    default:
      break;
  }

  return ret;
}

static GstCaps *
gst_xlnx_abr_scaler_fixate_caps (GstXlnxABRScaler * self,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;
  GValue fpar = G_VALUE_INIT, tpar = G_VALUE_INIT;

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);

  GST_DEBUG_OBJECT (self, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  {
    const gchar *in_format;

    in_format = gst_structure_get_string (ins, "format");
    if (in_format) {
      /* Try to set output format for pass through */
      gst_structure_fixate_field_string (outs, "format", in_format);
    }
  }

  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

  /* If we're fixating from the sinkpad we always set the PAR and
   * assume that missing PAR on the sinkpad means 1/1 and
   * missing PAR on the srcpad means undefined
   */
  if (direction == GST_PAD_SINK) {
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION_RANGE);
      gst_value_set_fraction_range_full (&tpar, 1, G_MAXINT, G_MAXINT, 1);
      to_par = &tpar;
    }
  } else {
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&tpar, 1, 1);
      to_par = &tpar;

      gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
          NULL);
    }
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
  }

  /* we have both PAR but they might not be fixated */
  {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    gint w = 0, h = 0;
    gint from_dar_n, from_dar_d;
    gint num, den;

    /* from_par should be fixed */
    g_return_val_if_fail (gst_value_is_fixed (from_par), othercaps);

    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    gst_structure_get_int (outs, "width", &w);
    gst_structure_get_int (outs, "height", &h);

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (w && h) {
      guint n, d;

      GST_DEBUG_OBJECT (self, "dimensions already set to %dx%d, not fixating",
          w, h);
      if (!gst_value_is_fixed (to_par)) {
        if (gst_video_calculate_display_ratio (&n, &d, from_w, from_h,
                from_par_n, from_par_d, w, h)) {
          GST_DEBUG_OBJECT (self, "fixating to_par to %dx%d", n, d);
          if (gst_structure_has_field (outs, "pixel-aspect-ratio"))
            gst_structure_fixate_field_nearest_fraction (outs,
                "pixel-aspect-ratio", n, d);
          else if (n != d)
            gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                n, d, NULL);
        }
      }
      goto done;
    }

    /* Calculate input DAR */
    if (!gst_util_fraction_multiply (from_w, from_h, from_par_n, from_par_d,
            &from_dar_n, &from_dar_d)) {
      GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      goto done;
    }

    GST_DEBUG_OBJECT (self, "Input DAR is %d/%d", from_dar_n, from_dar_d);

    /* If either width or height are fixed there's not much we
     * can do either except choosing a height or width and PAR
     * that matches the DAR as good as possible
     */
    if (h) {
      GstStructure *tmp;
      gint set_w, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (self, "height is fixed (%d)", h);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the width that is nearest to the
       * width with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (self, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        w = (guint) gst_util_uint64_scale_int (h, num, den);
        gst_structure_fixate_field_nearest_int (outs, "width", w);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input width */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "width", G_TYPE_INT, set_w,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the width to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int (h, num, den);
      gst_structure_fixate_field_nearest_int (outs, "width", w);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (w) {
      GstStructure *tmp;
      gint set_h, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (self, "width is fixed (%d)", w);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the height that is nearest to the
       * height with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (self, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        h = (guint) gst_util_uint64_scale_int (w, den, num);
        gst_structure_fixate_field_nearest_int (outs, "height", h);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input height */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }
      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "height", G_TYPE_INT, set_h,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the height to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      h = (guint) gst_util_uint64_scale_int (w, den, num);
      gst_structure_fixate_field_nearest_int (outs, "height", h);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (gst_value_is_fixed (to_par)) {
      GstStructure *tmp;
      gint set_h, set_w, f_h, f_w;

      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);

      GST_DEBUG_OBJECT (self, "PAR is fixed %d/%d", to_par_n, to_par_d);

      /* Calculate scale factor for the PAR change */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
              to_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      /* Try to keep the input height (because of interlacing) */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      w = (guint) gst_util_uint64_scale_int (set_h, num, den);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &set_w);
      gst_structure_free (tmp);

      /* We kept the DAR and the height is nearest to the original height */
      if (set_w == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      f_h = set_h;
      f_w = set_w;

      /* If the former failed, try to keep the input width at least */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      h = (guint) gst_util_uint64_scale_int (set_w, den, num);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_free (tmp);

      /* We kept the DAR and the width is nearest to the original width */
      if (set_h == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      /* If all this failed, keep the height that was nearest to the orignal
       * height and the nearest possible width. This changes the DAR but
       * there's not much else to do here.
       */
      gst_structure_set (outs, "width", G_TYPE_INT, f_w, "height", G_TYPE_INT,
          f_h, NULL);
      goto done;
    } else {
      GstStructure *tmp;
      gint set_h, set_w, set_par_n, set_par_d, tmp2;

      /* width, height and PAR are not fixed but passthrough is not possible */

      /* First try to keep the height and width as good as possible
       * and scale PAR */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);

        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* Otherwise try to scale width to keep the DAR with the set
       * PAR and height */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int (set_h, num, den);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, tmp2, "height",
            G_TYPE_INT, set_h, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* ... or try the same with the height */
      h = (guint) gst_util_uint64_scale_int (set_w, den, num);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, tmp2, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* If all fails we can't keep the DAR and take the nearest values
       * for everything from the first try */
      gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, set_h, NULL);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);
    }
  }

done:
  GST_DEBUG_OBJECT (self, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  if (from_par == &fpar)
    g_value_unset (&fpar);
  if (to_par == &tpar)
    g_value_unset (&tpar);

  /* fixate remaining fields */
  othercaps = gst_caps_fixate (othercaps);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, othercaps)) {
      gst_caps_replace (&othercaps, caps);
    }
  }

  return othercaps;
}

static gboolean
gst_xlnx_abr_scaler_do_bufferpool (GstXlnxABRScaler * self,
    GstXlnxABRScalerPad * srcpad, GstCaps * outcaps)
{
  GstQuery *query;
  gboolean ret = FALSE;

  /* find a pool for the negotiated caps now */
  GST_DEBUG_OBJECT (srcpad, "doing allocation query");
  query = gst_query_new_allocation (outcaps, TRUE);
  if (!gst_pad_peer_query (GST_PAD_CAST (srcpad), query)) {
    /* not a problem, just debug a little */
    GST_DEBUG_OBJECT (srcpad, "peer ALLOCATION query failed");
  }

  GST_DEBUG_OBJECT (srcpad, "ALLOCATION params: %" GST_PTR_FORMAT, query);

  ret = gst_v4l2_object_decide_allocation (srcpad->v4l2capture, query);
  if (ret) {
    GstBufferPool *pool = GST_BUFFER_POOL (srcpad->v4l2capture->pool);
    GstStructure *config = gst_buffer_pool_get_config (pool);
    guint min_buffers;

    gst_buffer_pool_config_get_params (config, NULL, NULL, &min_buffers, NULL);

    /*setting max buffers same as min buffers to avoid buffer allocation again */
    gst_buffer_pool_config_set_params (config, srcpad->outcaps,
        srcpad->v4l2capture->info.size, min_buffers, min_buffers);

    if (!gst_buffer_pool_set_config (pool, config))
      goto activate_failed;

    if (!gst_buffer_pool_set_active (pool, TRUE))
      goto activate_failed;

    config = gst_buffer_pool_get_config (pool);

    gst_buffer_pool_config_get_params (config, NULL, NULL,
        &(srcpad->min_buffers), &(srcpad->max_buffers));

    GST_INFO_OBJECT (srcpad, "capture pool %p min_buffers = %u and"
        "max_buffers = %u", pool, srcpad->min_buffers, srcpad->max_buffers);

    /* maintain same min buffers at output pool as well */
    srcpad->v4l2output->min_buffers = srcpad->min_buffers;
    gst_structure_free (config);
  }

  return ret;

activate_failed:
  GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
      ("failed to activate bufferpool"), ("failed to activate bufferpool"));
  return FALSE;
}

static GstCaps *
gst_xlnx_abr_scaler_transform_caps (GstXlnxABRScaler * self,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *ret;
  GstStructure *structure;
  GstCapsFeatures *features;
  gint i, n;

  GST_DEBUG_OBJECT (self,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  ret = gst_caps_new_empty ();
  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    structure = gst_caps_get_structure (caps, i);
    features = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (ret, structure, features))
      continue;

    /* make copy */
    structure = gst_structure_copy (structure);

    /* If the features are non-sysmem we can only do passthrough */
    if (!gst_caps_features_is_any (features)
        && gst_caps_features_is_equal (features,
            GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY)) {
      gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

      /* if pixel aspect ratio, make a range of it */
      if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
        gst_structure_set (structure, "pixel-aspect-ratio",
            GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
      }
    }
    gst_caps_append_structure_full (ret, structure,
        gst_caps_features_copy (features));
  }

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = intersection;
  }

  GST_DEBUG_OBJECT (self, "returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static GstCaps *
gst_xlnx_abr_scaler_find_transform (GstXlnxABRScaler * self, GstPad * pad,
    GstPad * otherpad, GstCaps * caps)
{
  GstPad *otherpeer;
  GstCaps *othercaps;
  gboolean is_fixed;

  /* caps must be fixed here, this is a programming error if it's not */
  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);

  otherpeer = gst_pad_get_peer (otherpad);

  /* see how we can transform the input caps. We need to do this even for
   * passthrough because it might be possible that this element cannot support
   * passthrough at all. */
  othercaps = gst_xlnx_abr_scaler_transform_caps (self,
      GST_PAD_DIRECTION (pad), caps, NULL);

  /* The caps we can actually output is the intersection of the transformed
   * caps with the pad template for the pad */
  if (othercaps && !gst_caps_is_empty (othercaps)) {
    GstCaps *intersect, *templ_caps;

    templ_caps = gst_pad_get_pad_template_caps (otherpad);
    GST_DEBUG_OBJECT (self,
        "intersecting against padtemplate %" GST_PTR_FORMAT, templ_caps);

    intersect =
        gst_caps_intersect_full (othercaps, templ_caps,
        GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (othercaps);
    gst_caps_unref (templ_caps);
    othercaps = intersect;
  }

  /* check if transform is empty */
  if (!othercaps || gst_caps_is_empty (othercaps))
    goto no_transform;

  /* if the othercaps are not fixed, we need to fixate them, first attempt
   * is by attempting passthrough if the othercaps are a superset of caps. */
  /* FIXME. maybe the caps is not fixed because it has multiple structures of
   * fixed caps */
  is_fixed = gst_caps_is_fixed (othercaps);
  if (!is_fixed) {
    GST_DEBUG_OBJECT (self,
        "transform returned non fixed  %" GST_PTR_FORMAT, othercaps);

    /* Now let's see what the peer suggests based on our transformed caps */
    if (otherpeer) {
      GstCaps *peercaps, *intersection, *templ_caps;

      GST_DEBUG_OBJECT (self,
          "Checking peer caps with filter %" GST_PTR_FORMAT, othercaps);

      peercaps = gst_pad_query_caps (otherpeer, othercaps);
      GST_DEBUG_OBJECT (self, "Resulted in %" GST_PTR_FORMAT, peercaps);
      if (!gst_caps_is_empty (peercaps)) {
        templ_caps = gst_pad_get_pad_template_caps (otherpad);

        GST_DEBUG_OBJECT (self,
            "Intersecting with template caps %" GST_PTR_FORMAT, templ_caps);

        intersection =
            gst_caps_intersect_full (peercaps, templ_caps,
            GST_CAPS_INTERSECT_FIRST);
        GST_DEBUG_OBJECT (self, "Intersection: %" GST_PTR_FORMAT, intersection);
        gst_caps_unref (peercaps);
        gst_caps_unref (templ_caps);
        peercaps = intersection;

        GST_DEBUG_OBJECT (self,
            "Intersecting with transformed caps %" GST_PTR_FORMAT, othercaps);
        intersection =
            gst_caps_intersect_full (peercaps, othercaps,
            GST_CAPS_INTERSECT_FIRST);
        GST_DEBUG_OBJECT (self, "Intersection: %" GST_PTR_FORMAT, intersection);
        gst_caps_unref (peercaps);
        gst_caps_unref (othercaps);
        othercaps = intersection;
      } else {
        gst_caps_unref (othercaps);
        othercaps = peercaps;
      }

      is_fixed = gst_caps_is_fixed (othercaps);
    } else {
      goto no_transform_possible;
    }
  }
  if (gst_caps_is_empty (othercaps))
    goto no_transform_possible;

  GST_DEBUG ("have %s fixed caps %" GST_PTR_FORMAT, (is_fixed ? "" : "non-"),
      othercaps);

  /* second attempt at fixation, call the fixate vmethod */
  /* caps could be fixed but the subclass may want to add fields */
  GST_DEBUG_OBJECT (self, "calling fixate_caps for %" GST_PTR_FORMAT
      " using caps %" GST_PTR_FORMAT " on pad %s:%s", othercaps, caps,
      GST_DEBUG_PAD_NAME (otherpad));
  /* note that we pass the complete array of structures to the fixate
   * function, it needs to truncate itself */
  othercaps =
      gst_xlnx_abr_scaler_fixate_caps (self, GST_PAD_DIRECTION (pad), caps,
      othercaps);
  is_fixed = gst_caps_is_fixed (othercaps);
  GST_DEBUG_OBJECT (self, "after fixating %" GST_PTR_FORMAT, othercaps);

  /* caps should be fixed now, if not we have to fail. */
  if (!is_fixed)
    goto could_not_fixate;

  /* and peer should accept */
  if (otherpeer && !gst_pad_query_accept_caps (otherpeer, othercaps))
    goto peer_no_accept;

  GST_DEBUG_OBJECT (self, "Input caps were %" GST_PTR_FORMAT
      ", and got final caps %" GST_PTR_FORMAT, caps, othercaps);

  if (otherpeer)
    gst_object_unref (otherpeer);

  return othercaps;

  /* ERRORS */
no_transform:
  {
    GST_DEBUG_OBJECT (self,
        "transform returned useless  %" GST_PTR_FORMAT, othercaps);
    goto error_cleanup;
  }
no_transform_possible:
  {
    GST_DEBUG_OBJECT (self,
        "transform could not transform %" GST_PTR_FORMAT
        " in anything we support", caps);
    goto error_cleanup;
  }
could_not_fixate:
  {
    GST_DEBUG_OBJECT (self, "FAILED to fixate %" GST_PTR_FORMAT, othercaps);
    goto error_cleanup;
  }
peer_no_accept:
  {
    GST_DEBUG_OBJECT (self, "FAILED to get peer of %" GST_PTR_FORMAT
        " to accept %" GST_PTR_FORMAT, otherpad, othercaps);
    goto error_cleanup;
  }
error_cleanup:
  {
    if (otherpeer)
      gst_object_unref (otherpeer);
    if (othercaps)
      gst_caps_unref (othercaps);
    return NULL;
  }
}

static gboolean
gst_xlnx_abr_scaler_configure_caps (GstXlnxABRScaler * self,
    GstXlnxABRScalerPad * srcpad, GstCaps * incaps, GstCaps * outcaps)
{
  GstV4l2Error error = GST_V4L2_ERROR_INIT;

  GST_DEBUG_OBJECT (srcpad, "in caps:  %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (srcpad, "out caps: %" GST_PTR_FORMAT, outcaps);

  if (srcpad->incaps && srcpad->outcaps) {
    if (gst_caps_is_equal (incaps, srcpad->incaps) &&
        gst_caps_is_equal (outcaps, srcpad->outcaps)) {
      GST_DEBUG_OBJECT (srcpad, "Caps did not changed");
      return TRUE;
    }
  }

  /* TODO Add renegotiation support */
  g_return_val_if_fail (!GST_V4L2_IS_ACTIVE (srcpad->v4l2output), FALSE);
  g_return_val_if_fail (!GST_V4L2_IS_ACTIVE (srcpad->v4l2capture), FALSE);

  gst_caps_replace (&srcpad->incaps, incaps);
  gst_caps_replace (&srcpad->outcaps, outcaps);

  if (!gst_v4l2_object_set_format (srcpad->v4l2output, incaps, &error))
    goto incaps_failed;

  if (!gst_v4l2_object_set_format (srcpad->v4l2capture, outcaps, &error))
    goto outcaps_failed;

  if (srcpad->v4l2capture->pool) {
    /*destroying capture pool of type GstV4l2BufferPool created by _set_format */
    GST_INFO_OBJECT (srcpad, "deactivating & destroying already created pool");
    gst_buffer_pool_set_active (srcpad->v4l2capture->pool, FALSE);
    gst_object_unref (srcpad->v4l2capture->pool);
  }

  /* create custom bufferpool for our ABR use case */
  srcpad->v4l2capture->pool =
      gst_v4l2_capture_buffer_pool_new (srcpad->v4l2capture, outcaps);
  if (srcpad->v4l2capture->pool == NULL) {
    GST_ERROR_OBJECT (srcpad, "failed to create v4l2 capture buffer pool");
    goto failed;
  }

  if (!gst_v4l2_object_setup_padding (srcpad->v4l2output))
    goto failed;

  if (!gst_v4l2_object_setup_padding (srcpad->v4l2capture))
    goto failed;

  return TRUE;

incaps_failed:
  {
    GST_ERROR_OBJECT (srcpad, "failed to set input caps: %" GST_PTR_FORMAT,
        incaps);
    gst_v4l2_error (self, &error);
    goto failed;
  }
outcaps_failed:
  {
    gst_v4l2_object_stop (srcpad->v4l2output);
    GST_ERROR_OBJECT (srcpad, "failed to set output caps: %" GST_PTR_FORMAT,
        outcaps);
    gst_v4l2_error (self, &error);
    goto failed;
  }
failed:
  return FALSE;
}

static gboolean
gst_xlnx_abr_scaler_sink_setcaps (GstXlnxABRScaler * self, GstPad * sinkpad,
    GstCaps * in_caps)
{
  GstCaps *outcaps = NULL, *prev_incaps = NULL, *prev_outcaps = NULL;
  gboolean ret = TRUE;
  guint idx = 0;
  GstXlnxABRScalerPad *srcpad = NULL;
  GstCaps *incaps = gst_caps_copy (in_caps);

  GST_DEBUG_OBJECT (self, "have new sink caps %p %" GST_PTR_FORMAT, incaps,
      incaps);

  prev_incaps = gst_pad_get_current_caps (self->sinkpad);

  for (idx = 0; idx < g_list_length (self->srcpads); idx++) {
    srcpad = gst_xlnx_abr_scaler_srcpad_at_index (self, idx);

    /* find best possible caps for the other pad */
    outcaps =
        gst_xlnx_abr_scaler_find_transform (self, sinkpad,
        GST_PAD_CAST (srcpad), incaps);
    if (!outcaps || gst_caps_is_empty (outcaps))
      goto no_transform_possible;

    if (gst_caps_is_equal (incaps, outcaps)) {
      GST_ERROR_OBJECT (srcpad,
          "input and output caps are same..no pass through supported");
      goto no_transform_possible;
    }

    prev_outcaps = gst_pad_get_current_caps (GST_PAD_CAST (srcpad));

    ret = prev_incaps && prev_outcaps && gst_caps_is_equal (prev_incaps, incaps)
        && gst_caps_is_equal (prev_outcaps, outcaps);

    if (ret) {
      GST_DEBUG_OBJECT (self,
          "New caps equal to old ones: %" GST_PTR_FORMAT " -> %" GST_PTR_FORMAT,
          incaps, outcaps);
    } else {
      /* call configure now */
      if (!(ret =
              gst_xlnx_abr_scaler_configure_caps (self, srcpad, incaps,
                  outcaps)))
        goto failed_configure;

      if (!prev_outcaps || !gst_caps_is_equal (outcaps, prev_outcaps)) {
        /* let downstream know about our caps */
        ret = gst_pad_set_caps (GST_PAD_CAST (srcpad), outcaps);
        if (!ret)
          goto failed_configure;
      }
    }

    gst_caps_unref (incaps);
    incaps = outcaps;

    if (prev_outcaps) {
      gst_caps_unref (prev_outcaps);
      prev_outcaps = NULL;
    }
  }

done:
  if (outcaps)
    gst_caps_unref (outcaps);
  if (prev_incaps)
    gst_caps_unref (prev_incaps);
  if (prev_outcaps)
    gst_caps_unref (prev_outcaps);

  return ret;

  /* ERRORS */
no_transform_possible:
  {
    GST_ERROR_OBJECT (self,
        "transform could not transform %" GST_PTR_FORMAT
        " in anything we support", incaps);
    ret = FALSE;
    goto done;
  }
failed_configure:
  {
    GST_ERROR_OBJECT (self, "FAILED to configure incaps %" GST_PTR_FORMAT
        " and outcaps %" GST_PTR_FORMAT, incaps, outcaps);
    ret = FALSE;
    goto done;
  }
}

static gboolean
gst_xlnx_abr_scaler_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstXlnxABRScaler *self = GST_XLNX_ABR_SCALER (parent);
  gboolean ret = TRUE;
  GstXlnxABRScalerPad *srcpad;
  guint idx;

  GST_DEBUG_OBJECT (pad, "received event '%s' %p %" GST_PTR_FORMAT,
      gst_event_type_get_name (GST_EVENT_TYPE (event)), event, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:{
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_xlnx_abr_scaler_sink_setcaps (self, self->sinkpad, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_FLUSH_START:{
      for (idx = 0; idx < g_list_length (self->srcpads); idx++) {
        srcpad = gst_xlnx_abr_scaler_srcpad_at_index (self, idx);
        gst_v4l2_object_unlock (srcpad->v4l2output);
        gst_v4l2_object_unlock (srcpad->v4l2capture);
      }
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    case GST_EVENT_FLUSH_STOP:{
      ret = gst_pad_event_default (pad, parent, event);

      for (idx = 0; idx < g_list_length (self->srcpads); idx++) {
        srcpad = gst_xlnx_abr_scaler_srcpad_at_index (self, idx);
        gst_v4l2_object_unlock_stop (srcpad->v4l2capture);
        gst_v4l2_object_unlock_stop (srcpad->v4l2output);
      }
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
gst_xlnx_abr_scaler_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstXlnxABRScaler *self = GST_XLNX_ABR_SCALER (parent);
  gboolean ret = TRUE;
  GstXlnxABRScalerPad *srcpad = NULL;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:{
      guint idx;

      for (idx = 0; idx < g_list_length (self->srcpads); idx++) {
        srcpad = gst_xlnx_abr_scaler_srcpad_at_index (self, idx);

        ret = ret
            && gst_v4l2_object_propose_allocation (srcpad->v4l2output, query);
      }
      break;
    }
    case GST_QUERY_CAPS:{
      GstCaps *filter, *caps = NULL, *result = NULL;

      gst_query_parse_caps (query, &filter);

      if (g_list_length (self->srcpads) > 0) {
        srcpad = gst_xlnx_abr_scaler_srcpad_at_index (self, 0);
        if (srcpad->probed_sinkcaps)
          caps = gst_caps_ref (srcpad->probed_sinkcaps);
      }

      if (!srcpad) {
        GST_ERROR_OBJECT (pad, "source pads not available..");
        return FALSE;
      }

      if (!caps)
        caps = gst_pad_get_pad_template_caps (pad);

      if (filter) {
        GstCaps *tmp = caps;
        caps = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (tmp);
      }

      if (srcpad) {
        result = gst_pad_peer_query_caps (GST_PAD_CAST (srcpad), caps);
        result = gst_caps_make_writable (result);
        gst_caps_append (result, caps);

        GST_DEBUG_OBJECT (self, "Returning %s caps %" GST_PTR_FORMAT,
            GST_PAD_NAME (pad), result);

        gst_query_set_caps_result (query, result);
        gst_caps_unref (result);
      }
      break;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;

      gst_query_parse_accept_caps (query, &caps);

      gst_query_set_accept_caps_result (query, ret);
      /* return TRUE, we answered the query */
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

static gboolean
gst_xlnx_abr_scaler_activate_pools (GstXlnxABRScaler * self)
{
  int idx;
  GstBufferPool *out_pool = NULL;
  GstBufferPool *cap_pool = NULL;
  GstXlnxABRScalerPad *srcpad = NULL;

  for (idx = (g_list_length (self->srcpads) - 1); idx >= 0; idx--) {
    GstXlnxABRScalerPad *next_srcpad = NULL;

    srcpad = gst_xlnx_abr_scaler_srcpad_at_index (self, idx);
    out_pool = srcpad->v4l2output->pool;
    cap_pool = srcpad->v4l2capture->pool;

    GST_DEBUG_OBJECT (srcpad, "index = %d, output pool = %p and "
        "capture pool = %p", idx, out_pool, cap_pool);

    if (idx < (g_list_length (self->srcpads) - 1)) {
      /* next pad's output pool will be proxy to current capture pool */
      next_srcpad = gst_xlnx_abr_scaler_srcpad_at_index (self, idx + 1);
      gst_v4l2_capture_buffer_pool_set_proxy_pool
          (GST_V4L2_CAPTURE_BUFFER_POOL_CAST (cap_pool),
          next_srcpad->v4l2output->pool);
      GST_DEBUG_OBJECT (srcpad, "setting output pool %p as proxy pool",
          next_srcpad->v4l2output->pool);
    }

    /* does activation of capture pool */
    if (!gst_xlnx_abr_scaler_do_bufferpool (self, srcpad, srcpad->outcaps)) {
      GST_ERROR_OBJECT (srcpad, "failed to configure pool");
      return FALSE;
    }

    /* activate output pool */
    if (!gst_buffer_pool_is_active (out_pool)) {
      GstStructure *config = gst_buffer_pool_get_config (out_pool);

      gint min =
          srcpad->v4l2output->min_buffers ==
          0 ? GST_V4L2_MIN_BUFFERS (srcpad->v4l2output) : srcpad->v4l2output->
          min_buffers;

      gst_buffer_pool_config_set_params (config, srcpad->incaps,
          srcpad->v4l2output->info.size, min, min);

      if (!gst_buffer_pool_set_config (out_pool, config))
        goto activate_failed;

      if (!gst_buffer_pool_set_active (out_pool, TRUE))
        goto activate_failed;
    }
  }

  return TRUE;

activate_failed:
  GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
      ("failed to activate bufferpool"), ("failed to activate bufferpool"));
  return FALSE;
}

static GstFlowReturn
gst_xlnx_abr_scaler_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuf)
{
  GstXlnxABRScaler *self = GST_XLNX_ABR_SCALER (parent);
  GstFlowReturn fret = GST_FLOW_OK;
  GstXlnxABRScalerPad *srcpad = NULL;
  guint idx = 0;

  if (self->do_init) {
    if (!gst_xlnx_abr_scaler_activate_pools (self))
      goto error;

    self->do_init = FALSE;
  }

  srcpad = gst_xlnx_abr_scaler_srcpad_at_index (self, 0);

  GST_DEBUG_OBJECT (self, "queue input buffer %p to channel 0", inbuf);

  fret =
      gst_v4l2_buffer_pool_process (GST_V4L2_BUFFER_POOL_CAST
      (srcpad->v4l2output->pool), &inbuf);
  if (G_UNLIKELY (fret != GST_FLOW_OK))
    goto error;

  for (idx = 0; idx < g_list_length (self->srcpads); idx++) {
    GstBuffer *outbuf = NULL;

    srcpad = gst_xlnx_abr_scaler_srcpad_at_index (self, idx);

    GST_DEBUG_OBJECT (srcpad, "dequeue output buffer from channel %d", idx);

    fret =
        gst_buffer_pool_acquire_buffer (srcpad->v4l2capture->pool, &outbuf,
        NULL);
    if (fret != GST_FLOW_OK)
      goto alloc_failed;

    gst_buffer_copy_into (outbuf, inbuf,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

    GST_LOG_OBJECT (srcpad,
        "pushing outbuf %p with pts = %" GST_TIME_FORMAT " dts = %"
        GST_TIME_FORMAT " duration = %" GST_TIME_FORMAT, outbuf,
        GST_TIME_ARGS (GST_BUFFER_PTS (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DTS (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

    fret = gst_pad_push (GST_PAD_CAST (srcpad), outbuf);
    if (G_UNLIKELY (fret != GST_FLOW_OK))
      goto error;
  }

  gst_buffer_unref (inbuf);
  return fret;

error:
  GST_ERROR_OBJECT (self, "failed with reason : %s", gst_flow_get_name (fret));
  gst_buffer_unref (inbuf);
  return fret;

alloc_failed:
  gst_buffer_unref (inbuf);
  GST_DEBUG_OBJECT (self, "could not allocate buffer from pool");
  return fret;
}
