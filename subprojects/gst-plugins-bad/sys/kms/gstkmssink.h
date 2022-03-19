/* GStreamer
 *
 * Copyright (C) 2016 Igalia
 *
 * Authors:
 *  Víctor Manuel Jáquez Leal <vjaquez@igalia.com>
 *  Javier Martin <javiermartin@by.com.es>
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

#ifndef __GST_KMS_SINK_H__
#define __GST_KMS_SINK_H__

#include <gst/video/gstvideosink.h>

G_BEGIN_DECLS

#define GST_TYPE_KMS_SINK \
  (gst_kms_sink_get_type())
#define GST_KMS_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_KMS_SINK, GstKMSSink))
#define GST_KMS_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_KMS_SINK, GstKMSSinkClass))
#define GST_IS_KMS_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_KMS_SINK))
#define GST_IS_KMS_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_KMS_SINK))

typedef struct _GstKMSSink GstKMSSink;
typedef struct _GstKMSSinkClass GstKMSSinkClass;

typedef struct
{
  guint xmin;
  guint ymin;
  guint width;
  guint height;
} roi_coordinate;

typedef struct
{
  guint count;
  guint ts;
  roi_coordinate *coordinate_param;
} roi_params;

struct _GstKMSSink {
  GstVideoSink videosink;

  /*< private >*/
  gint fd;
  gint conn_id;
  gint crtc_id;
  gint plane_id;
  gint primary_plane_id;
  guint pipe;

  /* crtc data */
  guint16 hdisplay, vdisplay;
  guint32 buffer_id;
  gpointer saved_crtc;

  /* capabilities */
  gboolean has_prime_import;
  gboolean has_prime_export;
  gboolean has_async_page_flip;
  gboolean can_scale;

  gboolean modesetting_enabled;
  gboolean restore_crtc;
  gboolean hold_extra_sample;
  GstStructure *connector_props;
  GstStructure *plane_props;
  gboolean fullscreen_enabled;

  GstVideoInfo vinfo;
  GstVideoInfo vinfo_crtc;
  GstCaps *allowed_caps;
  GstBufferPool *pool;
  GstAllocator *allocator;
  GstVideoInfo last_vinfo;
  guint last_width;
  guint last_height;
  GstBuffer *last_buffer;
  GstBuffer *previous_last_buffer;
  GstMemory *tmp_kmsmem;

  gchar *devname;
  gchar *bus_id;

  guint32 mm_width, mm_height;
  GstPoll *poll;
  GstPollFD pollfd;

  /* render video rectangle */
  GstVideoRectangle render_rect;

  /* reconfigure info if driver doesn't scale */
  GstVideoRectangle pending_rect;
  gboolean reconfigure;

  gboolean xlnx_ll;
  /* timestamp of last vblank */
  GstClockTime last_vblank;

  gboolean force_ntsc_tv;
  gboolean gray_to_yuv444;

  /* roi data */
  gboolean draw_roi;
  guint roi_rect_thickness;
  GValue roi_rect_yuv_color;
  roi_params roi_param;
};

struct _GstKMSSinkClass {
  GstVideoSinkClass parent_class;
};

GType gst_kms_sink_get_type (void) G_GNUC_CONST;
GST_ELEMENT_REGISTER_DECLARE (kmssink);
G_END_DECLS

#endif /* __GST_KMS_SINK_H__ */
