/*
 * Copyright (C) 2020 – 2021 Xilinx, Inc.  All rights reserved.
 *
 * Authors:
 *   Naveen Cherukuri <naveen.cherukuri@xilinx.com>
 *   Vishal Sagar <vishal.sagar@xilinx.com>
 *   Ronak Shah <ronak.shah@xilinx.com>
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * XILINX BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the Xilinx shall not be used
 * in advertising or otherwise to promote the sale, use or other dealings in
 * this Software without prior written authorization from Xilinx.
 */

#ifndef _GST_MEDIA_SRC_BIN_H_
#define _GST_MEDIA_SRC_BIN_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include "xmedialib.h"
#include "gstmediasrcbinpad.h"

G_BEGIN_DECLS
#define GST_TYPE_MEDIA_SRC_BIN            (gst_media_src_bin_get_type())
#define GST_MEDIA_SRC_BIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MEDIA_SRC_BIN,GstMediaSrcBin))
#define GST_IS_MEDIA_SRC_BIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MEDIA_SRC_BIN))
#define GST_MEDIA_SRC_BIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_MEDIA_SRC_BIN,GstMediaSrcBinClass))
#define GST_IS_MEDIA_SRC_BIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_MEDIA_SRC_BIN))
#define GST_MEDIA_SRC_BIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_MEDIA_SRC_BIN,GstMediaSrcBinClass))
typedef struct _GstMediaSrcBin GstMediaSrcBin;
typedef struct _GstMediaSrcBinClass GstMediaSrcBinClass;
typedef struct _GstVideoNodeInfo GstVideoNodeInfo;

struct _GstVideoNodeInfo
{
  gchar *vdevice;
  GstMediaSrcBinPad *ghostpad;
};

struct _GstMediaSrcBin
{
  GstBin parent;
  gchar *mediadev;
  GList *vnodes_info;
};

struct _GstMediaSrcBinClass
{
  GstBinClass parent_class;
};

GType gst_media_src_bin_get_type (void);

G_END_DECLS
#endif /* _GST_MEDIA_SRC_BIN_H_ */
