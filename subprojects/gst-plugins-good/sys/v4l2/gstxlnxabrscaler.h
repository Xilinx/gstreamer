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

#ifndef __GST_XLNX_ABR_SCALER_H__
#define __GST_XLNX_ABR_SCALER_H__

#include <gst/gst.h>
#include <gstv4l2object.h>
#include <gstv4l2bufferpool.h>
#include "gstv4l2capturebufferpool.h"

G_BEGIN_DECLS
/* #defines don't like whitespacey bits */
#define GST_TYPE_XLNX_ABR_SCALER (gst_xlnx_abr_scaler_get_type())
#define GST_XLNX_ABR_SCALER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_XLNX_ABR_SCALER,GstXlnxABRScaler))
#define GST_XLNX_ABR_SCALER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_XLNX_ABR_SCALER,GstXlnxABRScalerClass))
#define GST_XLNX_ABR_SCALER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_XLNX_ABR_SCALER,GstXlnxABRScalerClass))
#define GST_IS_XLNX_ABR_SCALER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_XLNX_ABR_SCALER))
#define GST_IS_XLNX_ABR_SCALER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_XLNX_ABR_SCALER))
#define GST_TYPE_ABR_IO_MODE (abr_scaler_io_mode_get_type ())
    GType abr_scaler_io_mode_get_type (void);

typedef struct _GstXlnxABRScaler GstXlnxABRScaler;
typedef struct _GstXlnxABRScalerClass GstXlnxABRScalerClass;

struct _GstXlnxABRScaler
{
  GstElement element;

  GstPad *sinkpad;
  GList *srcpads;

  char *videodev;
  GstV4l2IOMode capture_req_mode;
  GstV4l2IOMode output_req_mode;

  /* pads */
  GstCaps *probed_srccaps;
  GstCaps *probed_sinkcaps;

  /* Selected caps */
  GstCaps *incaps;
  GstCaps *outcaps;

  GHashTable *pad_indexes;
  guint next_pad_index;
  guint num_request_pads;
  gboolean do_init;
};

struct _GstXlnxABRScalerClass
{
  GstElementClass parent_class;
};

GType gst_xlnx_abr_scaler_get_type (void);

G_END_DECLS
#endif /* __GST_XLNX_ABR_SCALER_H__ */
