/*
 * Copyright (C) 2014 Collabora Ltd.
 *     Author: Nicolas Dufresne <nicolas.dufresne@collabora.co.uk>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include "gstxilinxscd.h"
#include "gstv4l2object.h"

#include <string.h>

#define DEFAULT_PROP_DEVICE "/dev/video0"

GST_DEBUG_CATEGORY_STATIC (gst_xilinx_scd_debug);
#define GST_CAT_DEFAULT gst_xilinx_scd_debug

#define SCD_EVENT_TYPE V4L2_EVENT_PRIVATE_START
#define DRIVER_NAME "xilinx-vipp"

enum
{
  PROP_0,
  V4L2_STD_OBJECT_PROPS
};

#define gst_xilinx_scd_parent_class parent_class
G_DEFINE_TYPE (GstXilinxScd, gst_xilinx_scd, GST_TYPE_BASE_TRANSFORM);

static void
gst_xilinx_scd_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstXilinxScd *self = GST_XILINX_SCD (object);

  switch (prop_id) {
    case PROP_OUTPUT_IO_MODE:
      gst_v4l2_object_set_property_helper (self->v4l2output, prop_id, value,
          pspec);
      break;

    default:
      if (!gst_v4l2_object_set_property_helper (self->v4l2output,
              prop_id, value, pspec)) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;
  }
}

static void
gst_xilinx_scd_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstXilinxScd *self = GST_XILINX_SCD (object);

  switch (prop_id) {
    case PROP_OUTPUT_IO_MODE:
      gst_v4l2_object_get_property_helper (self->v4l2output, prop_id, value,
          pspec);
      break;

    default:
      if (!gst_v4l2_object_get_property_helper (self->v4l2output,
              prop_id, value, pspec)) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;
  }
}

static gboolean
gst_xilinx_scd_open (GstXilinxScd * self)
{
  GST_DEBUG_OBJECT (self, "Opening");

  if (!gst_v4l2_object_open (self->v4l2output, NULL))
    goto failure;

  if (!g_str_equal (self->v4l2output->vcap.driver, DRIVER_NAME)) {
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
        ("Wrong driver: %s (expected: %s)", self->v4l2output->vcap.driver,
            DRIVER_NAME), (NULL));
    goto failure;
  }

  self->probed_sinkcaps = gst_v4l2_object_get_caps (self->v4l2output,
      gst_v4l2_object_get_raw_caps ());

  if (gst_caps_is_empty (self->probed_sinkcaps))
    goto no_input_format;

  gst_poll_fd_init (&self->poll_fd);
  self->poll_fd.fd = self->v4l2output->video_fd;

  gst_poll_add_fd (self->event_poll, &self->poll_fd);
  gst_poll_fd_ctl_pri (self->event_poll, &self->poll_fd, TRUE);

  if (!gst_v4l2_subscribe_event (self->v4l2output, SCD_EVENT_TYPE, 0, 0))
    goto failure;

  return TRUE;

no_input_format:
  GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
      ("Converter on device %s has no supported input format",
          self->v4l2output->videodev), (NULL));
  goto failure;

failure:
  if (GST_V4L2_IS_OPEN (self->v4l2output))
    gst_v4l2_object_close (self->v4l2output);

  gst_caps_replace (&self->probed_sinkcaps, NULL);

  return FALSE;
}

static void
gst_xilinx_scd_close (GstXilinxScd * self)
{
  GST_DEBUG_OBJECT (self, "Closing");

  gst_poll_remove_fd (self->event_poll, &self->poll_fd);

  gst_v4l2_object_close (self->v4l2output);

  gst_caps_replace (&self->probed_sinkcaps, NULL);
}

static gboolean
gst_xilinx_scd_stop (GstBaseTransform * trans)
{
  GstXilinxScd *self = GST_XILINX_SCD (trans);

  GST_DEBUG_OBJECT (self, "Stop");

  gst_v4l2_object_stop (self->v4l2output);

  return TRUE;
}

static gboolean
gst_xilinx_scd_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstV4l2Error error = GST_V4L2_ERROR_INIT;
  GstXilinxScd *self = GST_XILINX_SCD (trans);

  GST_DEBUG_OBJECT (self, "caps: %" GST_PTR_FORMAT, incaps);

  /* make sure the caps changed before doing anything */
  if (gst_v4l2_object_caps_equal (self->v4l2output, incaps))
    return TRUE;

  g_return_val_if_fail (!GST_V4L2_IS_ACTIVE (self->v4l2output), FALSE);

  if (!gst_v4l2_object_set_format (self->v4l2output, incaps, &error))
    goto incaps_failed;

  return TRUE;

incaps_failed:
  {
    GST_ERROR_OBJECT (self, "failed to set input caps: %" GST_PTR_FORMAT,
        incaps);
    gst_v4l2_error (self, &error);
    goto failed;
  }
failed:
  return FALSE;
}

static gboolean
gst_xilinx_scd_query (GstBaseTransform * trans, GstPadDirection direction,
    GstQuery * query)
{
  GstXilinxScd *self = GST_XILINX_SCD (trans);
  gboolean ret = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:{
      GstCaps *filter, *caps = NULL, *result = NULL;
      GstPad *pad, *otherpad;

      gst_query_parse_caps (query, &filter);

      if (direction == GST_PAD_SRC) {
        pad = GST_BASE_TRANSFORM_SRC_PAD (trans);
        otherpad = GST_BASE_TRANSFORM_SINK_PAD (trans);
        if (self->probed_sinkcaps)
          caps = gst_caps_ref (self->probed_sinkcaps);
      } else {
        pad = GST_BASE_TRANSFORM_SINK_PAD (trans);
        otherpad = GST_BASE_TRANSFORM_SRC_PAD (trans);
        if (self->probed_sinkcaps)
          caps = gst_caps_ref (self->probed_sinkcaps);
      }

      if (!caps)
        caps = gst_pad_get_pad_template_caps (pad);

      if (filter) {
        GstCaps *tmp = caps;
        caps = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (tmp);
      }

      result = gst_pad_peer_query_caps (otherpad, caps);
      result = gst_caps_make_writable (result);
      gst_caps_append (result, caps);

      GST_DEBUG_OBJECT (self, "Returning %s caps %" GST_PTR_FORMAT,
          GST_PAD_NAME (pad), result);

      gst_query_set_caps_result (query, result);
      gst_caps_unref (result);
      break;
    }

    default:
      ret = GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
          query);
      break;
  }

  return ret;
}

static GstFlowReturn
gst_xilinx_scd_wait_event (GstXilinxScd * self, struct v4l2_event *event)
{
  gint wait_ret;
  gboolean error = FALSE;

again:
  GST_LOG_OBJECT (self, "waiting for event");
  wait_ret = gst_poll_wait (self->event_poll, GST_CLOCK_TIME_NONE);
  if (G_UNLIKELY (wait_ret < 0)) {
    switch (errno) {
      case EBUSY:
        GST_DEBUG_OBJECT (self, "stop called");
        return GST_FLOW_FLUSHING;
      case EAGAIN:
      case EINTR:
        goto again;
      default:
        error = TRUE;
    }
  }

  if (error || gst_poll_fd_has_error (self->event_poll, &self->poll_fd)) {
    GST_ELEMENT_ERROR (self, RESOURCE, READ, (NULL),
        ("poll error: %s (%d)", g_strerror (errno), errno));
    return GST_FLOW_ERROR;
  }

  if (!gst_v4l2_dqevent (self->v4l2output, event)) {
    GST_ELEMENT_ERROR (self, RESOURCE, READ, (NULL),
        ("Failed to dqueue event"));
    return GST_FLOW_ERROR;
  }

  if (event->type != SCD_EVENT_TYPE) {
    GST_WARNING_OBJECT (self, "Received wront type of event: %d (expected: %d)",
        event->type, SCD_EVENT_TYPE);
    goto again;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_xilinx_scd_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstXilinxScd *self = GST_XILINX_SCD (trans);
  GstV4l2Object *obj = self->v4l2output;
  GstBufferPool *bpool = GST_BUFFER_POOL (obj->pool);
  GstFlowReturn ret;
  struct v4l2_event event;
  guint8 sc_detected;

  GST_LOG_OBJECT (self, "handle buffer: %p", buf);

  if (G_UNLIKELY (obj->pool == NULL)) {
    GST_ERROR_OBJECT (self, "not negotiated");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (G_UNLIKELY (!gst_buffer_pool_is_active (bpool))) {
    GstStructure *config;

    /* this pool was not activated, configure and activate */
    GST_DEBUG_OBJECT (self, "activating pool");

    config = gst_buffer_pool_get_config (bpool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_set_config (bpool, config);

    if (!gst_buffer_pool_set_active (bpool, TRUE)) {
      GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
          ("Failed to allocated required memory."),
          ("Buffer pool activation failed"));
      return GST_FLOW_ERROR;
    }
  }

  ret = gst_v4l2_buffer_pool_process (GST_V4L2_BUFFER_POOL (bpool), &buf, NULL);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    return ret;

  ret = gst_xilinx_scd_wait_event (self, &event);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    return ret;

  sc_detected = event.u.data[0];
  GST_LOG_OBJECT (self, "Received SCD event with data %d", sc_detected);

  if (sc_detected) {
    GstEvent *event;
    GstStructure *s;

    GST_DEBUG_OBJECT (self, "scene change detected; sending event");

    s = gst_structure_new ("omx-alg/scene-change",
        "look-ahead", G_TYPE_UINT, 0, NULL);

    event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);

    gst_pad_send_event (GST_BASE_TRANSFORM_SINK_PAD (self), event);
  }

  return ret;
}

static gboolean
gst_xilinx_scd_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstXilinxScd *self = GST_XILINX_SCD (trans);
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (self, "flush start");
      gst_poll_set_flushing (self->event_poll, TRUE);
      gst_v4l2_object_unlock (self->v4l2output);
      break;
    default:
      break;
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      /* Buffer should be back now */
      GST_DEBUG_OBJECT (self, "flush stop");
      gst_poll_set_flushing (self->event_poll, FALSE);
      gst_v4l2_object_unlock_stop (self->v4l2output);
      break;
    default:
      break;
  }

  return ret;
}

static GstStateChangeReturn
gst_xilinx_scd_change_state (GstElement * element, GstStateChange transition)
{
  GstXilinxScd *self = GST_XILINX_SCD (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_xilinx_scd_open (self))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_poll_set_flushing (self->event_poll, FALSE);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_poll_set_flushing (self->event_poll, TRUE);
      gst_v4l2_object_unlock (self->v4l2output);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_xilinx_scd_close (self);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_xilinx_scd_dispose (GObject * object)
{
  GstXilinxScd *self = GST_XILINX_SCD (object);

  gst_caps_replace (&self->probed_sinkcaps, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_xilinx_scd_finalize (GObject * object)
{
  GstXilinxScd *self = GST_XILINX_SCD (object);

  gst_poll_free (self->event_poll);
  gst_v4l2_object_destroy (self->v4l2output);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_xilinx_scd_init (GstXilinxScd * self)
{
  self->v4l2output = gst_v4l2_object_new (GST_ELEMENT (self),
      GST_OBJECT (GST_BASE_TRANSFORM_SINK_PAD (self)),
      V4L2_BUF_TYPE_VIDEO_OUTPUT, DEFAULT_PROP_DEVICE,
      gst_v4l2_get_output, gst_v4l2_set_output, NULL);
  self->event_poll = gst_poll_new (TRUE);
  self->v4l2output->no_initial_format = TRUE;
  self->v4l2output->keep_aspect = FALSE;

  /* enable QoS */
  gst_base_transform_set_qos_enabled (GST_BASE_TRANSFORM (self), TRUE);
}

static void
gst_xilinx_scd_class_init (GstXilinxScdClass * klass)
{
  GstElementClass *element_class;
  GObjectClass *gobject_class;
  GstBaseTransformClass *base_transform_class;

  element_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;
  base_transform_class = (GstBaseTransformClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_xilinx_scd_debug, "xilinxscd", 0,
      "Xilinx Scene Change Detector");

  gst_element_class_set_static_metadata (element_class,
      "Xilinx Scene Change Detector",
      "Filter/Converter/Video/Scaler",
      "Detect and notify downstream about upcoming scene change",
      "Guillaume Desmottes <guillaume.desmottes@collabora.com>");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          gst_v4l2_object_get_all_caps ()));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_v4l2_object_get_all_caps ()));

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_xilinx_scd_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_xilinx_scd_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_xilinx_scd_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_xilinx_scd_get_property);

  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_xilinx_scd_stop);
  base_transform_class->set_caps = GST_DEBUG_FUNCPTR (gst_xilinx_scd_set_caps);
  base_transform_class->query = GST_DEBUG_FUNCPTR (gst_xilinx_scd_query);
  base_transform_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_xilinx_scd_sink_event);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_xilinx_scd_transform_ip);

  base_transform_class->passthrough_on_same_caps = TRUE;
  base_transform_class->transform_ip_on_passthrough = TRUE;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_xilinx_scd_change_state);

  gst_v4l2_object_install_properties_helper (gobject_class,
      DEFAULT_PROP_DEVICE);
}
