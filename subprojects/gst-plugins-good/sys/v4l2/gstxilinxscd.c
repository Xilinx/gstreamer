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
#include "gstv4l2media.h"

#include <string.h>
#include "ext/media.h"
#include "ext/xilinx-v4l2-controls.h"
#include "ext/media-bus-format.h"
#include "ext/v4l2-subdev.h"

#define DEFAULT_PROP_DEVICE "/dev/video0"
#define DEFAULT_PROP_THRESHOLD 50

GST_DEBUG_CATEGORY_STATIC (gst_xilinx_scd_debug);
#define GST_CAT_DEFAULT gst_xilinx_scd_debug

#define SCD_EVENT_TYPE 0x08000301
#define DRIVER_NAME "xilinx-vipp"
#define ENTITY_SCD_PREFIX "xlnx-scdchan"

enum
{
  PROP_0,
  V4L2_STD_OBJECT_PROPS,
  PROP_THRESHOLD,
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

    case PROP_THRESHOLD:
      self->thresh_val = g_value_get_uint (value);
      gst_v4l2_set_attribute (self->subdev, V4L2_CID_XILINX_SCD_THRESHOLD,
          self->thresh_val);
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

    case PROP_THRESHOLD:
      gst_v4l2_get_attribute (self->subdev, V4L2_CID_XILINX_SCD_THRESHOLD,
          (int *) &self->thresh_val);
      g_value_set_uint (value, self->thresh_val);
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
find_scd_video (GstV4l2Media * media, GstV4l2MediaEntity * scd_entity,
    GstV4l2MediaEntity ** video_entity, GstV4l2MediaInterface ** video)
{
  GstV4l2MediaPad *pad;
  GstV4l2MediaLink *link;
  GList *l;

  g_return_val_if_fail (video_entity && video, FALSE);

  if (!scd_entity->pads)
    return FALSE;

  pad = scd_entity->pads->data;
  l = gst_v4l2_media_find_pads_linked_with_sink (media, pad);

  if (!l)
    return FALSE;

  link = l->data;
  g_list_free (l);
  pad = link->source;
  *video_entity = pad->entity;

  l = gst_v4l2_media_find_interfaces_linked_with_entity (media, *video_entity,
      MEDIA_INTF_T_V4L_VIDEO);
  if (!l)
    return FALSE;

  *video = l->data;
  g_list_free (l);

  return TRUE;
}

static GstV4l2MediaInterface *
find_scd_subdev (GstV4l2Media * media, GstV4l2MediaEntity * scd_entity)
{
  GstV4l2MediaInterface *subdev;
  GList *l;

  if (!g_str_has_prefix (scd_entity->name, ENTITY_SCD_PREFIX))
    return NULL;

  l = gst_v4l2_media_find_interfaces_linked_with_entity (media, scd_entity,
      MEDIA_INTF_T_V4L_SUBDEV);

  if (!l)
    return FALSE;
  subdev = l->data;
  g_list_free (l);
  return subdev;
}

static gboolean
gst_xilinx_scd_try_opening_device (GstXilinxScd * self, const gchar * video,
    const gchar * subdev)
{
  /* Video device */
  gst_v4l2_object_set_device (self->v4l2output, video);

  if (!gst_v4l2_object_open (self->v4l2output, NULL))
    goto failed;

  if (!gst_v4l2_object_get_exclusive_lock (self->v4l2output)) {
    GST_DEBUG_OBJECT (self, "%s is already used", video);
    goto failed;
  }

  self->output_locked = TRUE;

  /* v4l2 subdev */
  gst_v4l2_object_set_device (self->subdev, subdev);

  if (!gst_v4l2_object_open (self->subdev, NULL))
    goto failed;

  if (!gst_v4l2_object_get_exclusive_lock (self->subdev)) {
    GST_DEBUG_OBJECT (self, "%s is already used", subdev);
    goto failed;
  }

  self->subdev_locked = TRUE;

  return TRUE;

failed:
  if (self->output_locked) {
    gst_v4l2_object_release_exclusive_lock (self->v4l2output);
    self->output_locked = FALSE;
  }
  if (GST_V4L2_IS_OPEN (self->v4l2output))
    gst_v4l2_object_close (self->v4l2output);

  if (self->subdev_locked) {
    gst_v4l2_object_release_exclusive_lock (self->subdev);
    self->subdev_locked = FALSE;
  }
  if (GST_V4L2_IS_OPEN (self->subdev))
    gst_v4l2_object_close (self->subdev);

  return FALSE;
}

#define MAX_MEDIA_INDEX 64

typedef struct
{
  GstV4l2Media *media;
  gint media_idx;
  GList *entities;
  GList *entities_it;
} GstXilinxScdIterator;

static GstXilinxScdIterator *
gst_xilinx_scd_iterator_new (void)
{
  GstXilinxScdIterator *it;

  it = g_new0 (GstXilinxScdIterator, 1);
  it->media_idx = -1;
  return it;
}

static void
gst_xilinx_scd_iterator_free (GstXilinxScdIterator * it)
{
  g_clear_pointer (&it->media, gst_v4l2_media_free);
  g_clear_pointer (&it->entities, g_list_free);
  g_free (it);
}

static gboolean
gst_xilinx_scd_iterator_next (GstXilinxScdIterator * it,
    gchar ** video_file_out, gchar ** subdev_file_out)
{
  GList *l;

  /* Find the next /dev/media%d */
retry:
  while (!it->media) {
    gchar *path;

    it->media_idx++;

    if (it->media_idx > MAX_MEDIA_INDEX)
      return FALSE;

    path = g_strdup_printf ("/dev/media%d", it->media_idx);
    if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
      g_free (path);
      continue;
    }

    it->media = gst_v4l2_media_new (path);
    g_free (path);

    if (!gst_v4l2_media_open (it->media)) {
      g_clear_pointer (&it->media, gst_v4l2_media_free);
      continue;
    }

    if (!gst_v4l2_media_refresh_topology (it->media)) {
      g_clear_pointer (&it->media, gst_v4l2_media_free);
      continue;
    }

    g_clear_pointer (&it->entities, g_list_free);
    it->entities = gst_v4l2_media_get_entities (it->media);
    it->entities_it = it->entities;
  }

  for (l = it->entities_it; l; l = g_list_next (l)) {
    GstV4l2MediaEntity *entity = l->data;
    GstV4l2MediaEntity *video_entity;
    GstV4l2MediaInterface *subdev, *video;
    gchar *subdev_file, *video_file;

    subdev = find_scd_subdev (it->media, entity);
    if (!subdev)
      continue;

    if (!find_scd_video (it->media, entity, &video_entity, &video))
      continue;

    /* Found device */
    subdev_file = gst_v4l2_media_get_interface_device_file (it->media, subdev);
    video_file = gst_v4l2_media_get_interface_device_file (it->media, video);

    GST_DEBUG ("SCD device: '%s' (%s) subdev '%s' (%s)",
        video_entity->name, video_file, entity->name, subdev_file);

    if (video_file_out)
      *video_file_out = video_file;
    else
      g_free (video_file);

    if (subdev_file_out)
      *subdev_file_out = subdev_file;
    else
      g_free (subdev_file);

    it->entities_it = g_list_next (l);
    return TRUE;
  }

  /* Didn't find device with current media, try next one */
  g_clear_pointer (&it->media, gst_v4l2_media_free);
  goto retry;
}

static gboolean
gst_xilinx_scd_find_device (GstXilinxScd * self)
{
  GstXilinxScdIterator *it;
  gboolean result = FALSE;
  gchar *subdev_file, *video_file;

  it = gst_xilinx_scd_iterator_new ();

  while (gst_xilinx_scd_iterator_next (it, &video_file, &subdev_file)
      && !result) {
    result = gst_xilinx_scd_try_opening_device (self, video_file, subdev_file);

    g_free (subdev_file);
    g_free (video_file);
  }

  gst_xilinx_scd_iterator_free (it);

  if (!result)
    GST_DEBUG ("Didn't find any SCD device");

  return result;
}

static gboolean
gst_xilinx_scd_open (GstXilinxScd * self)
{
  GST_DEBUG_OBJECT (self, "Opening");

  if (!gst_xilinx_scd_find_device (self))
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
  self->poll_fd.fd = self->subdev->video_fd;

  gst_poll_add_fd (self->event_poll, &self->poll_fd);
  gst_poll_fd_ctl_pri (self->event_poll, &self->poll_fd, TRUE);

  if (!gst_v4l2_subscribe_event (self->subdev, SCD_EVENT_TYPE, 0, 0))
    goto failure;

  gst_v4l2_set_attribute (self->subdev, V4L2_CID_XILINX_SCD_THRESHOLD,
      self->thresh_val);

  return TRUE;

no_input_format:
  GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
      ("Converter on device %s has no supported input format",
          self->v4l2output->videodev), (NULL));
  goto failure;

failure:
  if (self->output_locked) {
    gst_v4l2_object_release_exclusive_lock (self->v4l2output);
    self->output_locked = FALSE;
  }
  if (GST_V4L2_IS_OPEN (self->v4l2output))
    gst_v4l2_object_close (self->v4l2output);

  if (self->subdev_locked) {
    gst_v4l2_object_release_exclusive_lock (self->subdev);
    self->subdev_locked = FALSE;
  }
  if (GST_V4L2_IS_OPEN (self->subdev))
    gst_v4l2_object_close (self->subdev);

  gst_caps_replace (&self->probed_sinkcaps, NULL);

  return FALSE;
}

static void
gst_xilinx_scd_close (GstXilinxScd * self)
{
  GST_DEBUG_OBJECT (self, "Closing");

  gst_poll_remove_fd (self->event_poll, &self->poll_fd);

  if (self->output_locked) {
    gst_v4l2_object_release_exclusive_lock (self->v4l2output);
    self->output_locked = FALSE;
  }

  gst_v4l2_object_close (self->v4l2output);

  if (self->subdev_locked) {
    gst_v4l2_object_release_exclusive_lock (self->subdev);
    self->subdev_locked = FALSE;
  }

  gst_v4l2_object_close (self->subdev);

  gst_caps_replace (&self->probed_sinkcaps, NULL);
}

static gboolean
gst_xilinx_scd_stop (GstBaseTransform * trans)
{
  GstXilinxScd *self = GST_XILINX_SCD (trans);

  GST_DEBUG_OBJECT (self, "Stop");

  gst_v4l2_object_stop (self->v4l2output);
  gst_v4l2_object_stop (self->subdev);

  return TRUE;
}

static gboolean
gst_xilinx_scd_set_subdev_format (GstBaseTransform * trans, GstCaps * incaps)
{
  GstXilinxScd *self = GST_XILINX_SCD (trans);
  GstVideoInfo info;
  struct v4l2_subdev_format fmt;
  struct v4l2_mbus_framefmt format;

  if (!gst_video_info_from_caps (&info, incaps))
    goto s_fmt_failed;

  /* Convert from GST format to media bus format */
  switch (GST_VIDEO_INFO_FORMAT (&info)) {
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      format.code = MEDIA_BUS_FMT_UYVY8_1X16;
      break;
    case GST_VIDEO_FORMAT_NV12:
      format.code = MEDIA_BUS_FMT_VYYUYY8_1X24;
      break;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      format.code = MEDIA_BUS_FMT_VYYUYY10_4X20;
      break;
    case GST_VIDEO_FORMAT_NV16_10LE32:
      format.code = MEDIA_BUS_FMT_UYVY10_1X20;
      break;
    case GST_VIDEO_FORMAT_NV16:
      format.code = MEDIA_BUS_FMT_UYVY8_1X16;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_BGRx:
      format.code = MEDIA_BUS_FMT_RBG888_1X24;
      break;
    default:
      GST_ERROR_OBJECT (self, "Color format %d is not supported by Xilinx SCD",
          GST_VIDEO_INFO_FORMAT (&info));
      goto s_fmt_failed;
  }

  /* Convert from GST field to V4L2 field */
  if (!GST_VIDEO_INFO_IS_INTERLACED (&info))
    format.field = V4L2_FIELD_NONE;

  if (GST_VIDEO_INFO_INTERLACE_MODE (&info) ==
      GST_VIDEO_INTERLACE_MODE_ALTERNATE)
    format.field = V4L2_FIELD_ALTERNATE;

  switch (GST_VIDEO_INFO_FIELD_ORDER (&info)) {
    case GST_VIDEO_FIELD_ORDER_TOP_FIELD_FIRST:
      format.field = V4L2_FIELD_INTERLACED_TB;
    case GST_VIDEO_FIELD_ORDER_BOTTOM_FIELD_FIRST:
      format.field = V4L2_FIELD_INTERLACED_BT;
    case GST_VIDEO_FIELD_ORDER_UNKNOWN:
    default:
      format.field = V4L2_FIELD_INTERLACED;
  }

  format.width = GST_VIDEO_INFO_WIDTH (&info);
  format.height = GST_VIDEO_INFO_FIELD_HEIGHT (&info);

  fmt.pad = 0;
  fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
  fmt.format = format;

  /* Set format of subdev pad based on caps */
  if (ioctl (self->subdev->video_fd, VIDIOC_SUBDEV_S_FMT, &fmt)) {
    GST_ERROR_OBJECT (self, "VIDIOC_SUBDEV_S_FMT on Xilinx SCD subdev failed");
    goto s_fmt_failed;
  }

  return TRUE;

s_fmt_failed:
  return FALSE;
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

  if (!gst_xilinx_scd_set_subdev_format (trans, incaps))
    goto incaps_failed;

  /* Clear format list as subdev format has changed */
  g_slist_free_full (g_steal_pointer (&self->v4l2output->formats), g_free);

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
  gint xscd_supported_formats[] =
      { GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_NV12,
    GST_VIDEO_FORMAT_NV12_10LE32, GST_VIDEO_FORMAT_NV16_10LE32,
    GST_VIDEO_FORMAT_NV16, GST_VIDEO_FORMAT_RGB, GST_VIDEO_FORMAT_BGR,
    GST_VIDEO_FORMAT_BGRx
  };
  GValue xscd_formats = G_VALUE_INIT;
  gint i;

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

      /* HACK: Advertise all formats because we automatically set media graph */
      g_value_init (&xscd_formats, GST_TYPE_LIST);
      for (i = 0;
          i <
          sizeof (xscd_supported_formats) / sizeof (*xscd_supported_formats);
          i++) {
        GValue value = G_VALUE_INIT;

        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value,
            gst_video_format_to_string (xscd_supported_formats[i]));
        gst_value_list_append_and_take_value (&xscd_formats, &value);
      }

      gst_caps_set_value (result, "format", &xscd_formats);
      g_value_unset (&xscd_formats);

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

  if (!gst_v4l2_dqevent (self->subdev, event)) {
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
  gst_v4l2_object_destroy (self->subdev);

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

  self->subdev = gst_v4l2_object_new (GST_ELEMENT (self), GST_OBJECT (self),
      0, DEFAULT_PROP_DEVICE, gst_v4l2_get_output, gst_v4l2_set_output, NULL);
  self->thresh_val = DEFAULT_PROP_THRESHOLD;

  /* enable QoS */
  gst_base_transform_set_qos_enabled (GST_BASE_TRANSFORM (self), TRUE);
}

static void
gst_xilinx_scd_debug_init (void)
{
  GST_DEBUG_CATEGORY_INIT (gst_xilinx_scd_debug, "xilinxscd", 0,
      "Xilinx Scene Change Detector");
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

  gst_xilinx_scd_debug_init ();

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

  g_object_class_install_property (gobject_class, PROP_IO_MODE,
      g_param_spec_enum ("io-mode", "IO mode",
          "I/O mode",
          GST_TYPE_V4L2_IO_MODE, GST_V4L2_IO_AUTO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_THRESHOLD,
      g_param_spec_uint ("threshold", "Threshold",
          "SAD value at which the SCD will signal a scene change. Increasing this value decreases the sensitivity of SCD. 0 signals any change to be a scene change, 100 never signals a scene change.",
          0, 100, DEFAULT_PROP_THRESHOLD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

gboolean
gst_xilinx_scd_register (GstPlugin * plugin)
{
  GstXilinxScdIterator *it;
  gboolean found;

  gst_xilinx_scd_debug_init ();

  /* Register the element if there is at least one SCD device on the system */
  it = gst_xilinx_scd_iterator_new ();
  found = gst_xilinx_scd_iterator_next (it, NULL, NULL);
  gst_xilinx_scd_iterator_free (it);

  if (!found) {
    GST_DEBUG ("No SCD device found, don't register xilinxscd");
    return TRUE;
  }

  return gst_element_register (plugin, "xilinxscd", GST_RANK_NONE,
      GST_TYPE_XILINX_SCD);
}
