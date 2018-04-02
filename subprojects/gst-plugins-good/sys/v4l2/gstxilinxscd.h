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

#ifndef __GST_XILINX_SCD_H__
#define __GST_XILINX_SCD_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include <gstv4l2object.h>
#include <gstv4l2bufferpool.h>

GST_DEBUG_CATEGORY_EXTERN (xilinxscd_debug);

G_BEGIN_DECLS
#define GST_TYPE_XILINX_SCD \
  (gst_xilinx_scd_get_type())
#define GST_XILINX_SCD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_XILINX_SCD,GstXilinxScd))
#define GST_XILINX_SCD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_XILINX_SCD,GstXilinxScdClass))
#define GST_IS_XILINX_SCD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_XILINX_SCD))
#define GST_IS_XILINX_SCD_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_XILINX_SCD))
#define GST_XILINX_SCD_GET_CLASS(inst) \
  (G_TYPE_INSTANCE_GET_CLASS ((inst),GST_TYPE_XILINX_SCD,GstXilinxScdClass))
typedef struct _GstXilinxScd GstXilinxScd;
typedef struct _GstXilinxScdClass GstXilinxScdClass;

struct _GstXilinxScd
{
  GstBaseTransform parent;

  /* < private > */
  GstV4l2Object *v4l2output;

  /* pads */
  GstCaps *probed_sinkcaps;
};

struct _GstXilinxScdClass
{
  GstBaseTransformClass parent_class;
  gchar *default_device;
};

GType gst_xilinx_scd_get_type (void);

G_END_DECLS
#endif /* __GST_XILINX_SCD_H__ */
