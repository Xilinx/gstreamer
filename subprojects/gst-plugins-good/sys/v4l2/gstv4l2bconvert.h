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

#ifndef __GST_V4L2_BCONVERT_H__
#define __GST_V4L2_BCONVERT_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gstv4l2object.h>
#include <gstv4l2bufferpool.h>

G_BEGIN_DECLS

#define GST_TYPE_V4L2_BCONVERT \
  (gst_v4l2_bconvert_get_type())
#define GST_V4L2_BCONVERT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4L2_BCONVERT,GstV4l2BConvert))
#define GST_V4L2_BCONVERT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4L2_BCONVERT,GstV4l2BConvertClass))
#define GST_IS_V4L2_BCONVERT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4L2_BCONVERT))
#define GST_IS_V4L2_BCONVERT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4L2_BCONVERT))
#define GST_V4L2_BCONVERT_GET_CLASS(inst) \
  (G_TYPE_INSTANCE_GET_CLASS ((inst),GST_TYPE_V4L2_BCONVERT,GstV4l2BConvertClass))

typedef struct _GstV4l2BConvert GstV4l2BConvert;
typedef struct _GstV4l2BConvertClass GstV4l2BConvertClass;




struct _GstV4l2BConvert
{
  GstBaseTransform parent;

  /* < private > */
  GstV4l2Object * v4l2output;
  GstV4l2Object * v4l2capture;

  /* pads */
  GstCaps *probed_srccaps;
  GstCaps *probed_sinkcaps;

  /* Selected caps */
  GstCaps *incaps;
  GstCaps *outcaps;

  gboolean disable_passthrough;
  gboolean import_buffer_alignment;
};

struct _GstV4l2BConvertClass
{
  GstBaseTransformClass parent_class;
  gchar *default_device;
};

GType gst_v4l2_bconvert_get_type (void);

gboolean gst_v4l2_is_bconvert       (GstCaps * sink_caps, GstCaps * src_caps);
void     gst_v4l2_bconvert_register (GstPlugin * plugin,
                                      const gchar *basename,
                                      const gchar *device_path,
                                      GstCaps * sink_caps, GstCaps * src_caps);

G_END_DECLS

#endif /* __GST_V4L2_BCONVERT_H__ */
