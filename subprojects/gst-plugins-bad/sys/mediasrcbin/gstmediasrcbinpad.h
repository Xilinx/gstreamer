/*
 * Copyright (C) 2020 – 2021 Xilinx, Inc.  All rights reserved.
 *
 * Authors:
 *   Naveen Cherukuri <naveen.cherukuri@xilinx.com>
 *   Vishal Sagar <vishal.sagar@xilinx.com>
 *   Ronak Shah <ronak.shah@xilinx.com>
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

#ifndef __GST_MEDIA_SRC_BIN_PAD_H__
#define __GST_MEDIA_SRC_BIN_PAD_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_MEDIA_SRC_BIN_PAD (gst_media_src_bin_pad_get_type())
#define GST_MEDIA_SRC_BIN_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MEDIA_SRC_BIN_PAD, GstMediaSrcBinPad))
#define GST_MEDIA_SRC_BIN_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MEDIA_SRC_BIN_PAD, GstMediaSrcBinPadClass))
#define GST_IS_MEDIA_SRC_BIN_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MEDIA_SRC_BIN_PAD))
#define GST_IS_MEDIA_SRC_BIN_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MEDIA_SRC_BIN_PAD))
typedef struct _GstMediaSrcBinPad GstMediaSrcBinPad;
typedef struct _GstMediaSrcBinPadClass GstMediaSrcBinPadClass;

/**
 * GstMediaSrcBinPad:
 *
 * The opaque #GstMediaSrcBinPad structure.
 */
struct _GstMediaSrcBinPad
{
  GstGhostPad parent;
  GstElement *src;
};

struct _GstMediaSrcBinPadClass
{
  GstGhostPadClass parent_class;
};

GType gst_media_src_bin_pad_get_type (void);

G_END_DECLS
#endif /* __GST_MEDIA_SRC_BIN_PAD_H__ */
