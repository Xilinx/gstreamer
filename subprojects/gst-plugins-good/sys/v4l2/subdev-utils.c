/*
 * Copyright (C) 2020 Xilinx Inc.
 *     Author: Dylan Yip <dylan.yip@xilinx.com>
 *
 * subdev-utils.c - generic V4l2 subdev handling
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "subdev-utils.h"
#include "ext/media-bus-format.h"

#define DEFAULT_PROP_DEVICE "/dev/video0"

GstV4l2Subdev *
gst_v4l2_subdev_new (GstV4l2Object * obj)
{
  GstV4l2Subdev *v4l2subdev = g_slice_new0 (struct _GstV4l2Subdev);

  v4l2subdev->event_poll = gst_poll_new (TRUE);
  v4l2subdev->subdev =
      gst_v4l2_object_new (obj->element, obj->dbg_obj, 0,
      DEFAULT_PROP_DEVICE, gst_v4l2_get_output, gst_v4l2_set_output, NULL);
  gst_poll_fd_init (&v4l2subdev->poll_fd);

  return v4l2subdev;
}

void
gst_v4l2_subdev_free (GstV4l2Subdev * subdev)
{
  gst_poll_free (subdev->event_poll);
  gst_v4l2_object_destroy (subdev->subdev);
  g_slice_free (struct _GstV4l2Subdev, subdev);
}

void
gst_v4l2_subdev_free_gpointer (gpointer data)
{
  GstV4l2Subdev *v4l2subdev = (GstV4l2Subdev *) data;

  gst_v4l2_subdev_free (v4l2subdev);
}

gboolean
gst_v4l2_subdev_g_fmt (GstV4l2Subdev * v4l2subdev,
    struct v4l2_subdev_format *fmt)
{
  GstV4l2Object *v4l2object = v4l2subdev->subdev;

  g_return_val_if_fail (fmt, FALSE);

  if (!GST_V4L2_IS_OPEN (v4l2object))
    return FALSE;

  if (v4l2object->ioctl (v4l2object->video_fd, VIDIOC_SUBDEV_G_FMT, fmt) < 0)
    goto subdev_g_fmt_failed;

  return TRUE;

  /* ERRORS */
subdev_g_fmt_failed:
  GST_WARNING ("Failed to get format on subdevice %s", v4l2object->videodev);
  return FALSE;
}

static gboolean
gst_v4l2_subdev_mbus_is_rgb (guint32 fourcc)
{
  gboolean ret = FALSE;

  switch (fourcc) {
    case MEDIA_BUS_FMT_RGB444_1X12:
    case MEDIA_BUS_FMT_RGB444_2X8_PADHI_BE:
    case MEDIA_BUS_FMT_RGB444_2X8_PADHI_LE:
    case MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE:
    case MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE:
    case MEDIA_BUS_FMT_RGB565_1X16:
    case MEDIA_BUS_FMT_BGR565_2X8_BE:
    case MEDIA_BUS_FMT_BGR565_2X8_LE:
    case MEDIA_BUS_FMT_RGB565_2X8_BE:
    case MEDIA_BUS_FMT_RGB565_2X8_LE:
    case MEDIA_BUS_FMT_RGB666_1X18:
    case MEDIA_BUS_FMT_RBG888_1X24:
    case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
    case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
    case MEDIA_BUS_FMT_BGR888_1X24:
    case MEDIA_BUS_FMT_GBR888_1X24:
    case MEDIA_BUS_FMT_RGB888_1X24:
    case MEDIA_BUS_FMT_RGB888_2X12_BE:
    case MEDIA_BUS_FMT_RGB888_2X12_LE:
    case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
    case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
    case MEDIA_BUS_FMT_ARGB8888_1X32:
    case MEDIA_BUS_FMT_RGB888_1X32_PADHI:
    case MEDIA_BUS_FMT_RGB101010_1X30:
    case MEDIA_BUS_FMT_RGB121212_1X36:
    case MEDIA_BUS_FMT_RGB161616_1X48:
    case MEDIA_BUS_FMT_SBGGR8_1X8:
    case MEDIA_BUS_FMT_SGBRG8_1X8:
    case MEDIA_BUS_FMT_SGRBG8_1X8:
    case MEDIA_BUS_FMT_SRGGB8_1X8:
    case MEDIA_BUS_FMT_SBGGR10_ALAW8_1X8:
    case MEDIA_BUS_FMT_SGBRG10_ALAW8_1X8:
    case MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8:
    case MEDIA_BUS_FMT_SRGGB10_ALAW8_1X8:
    case MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8:
    case MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8:
    case MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8:
    case MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8:
    case MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_BE:
    case MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_LE:
    case MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_BE:
    case MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_LE:
    case MEDIA_BUS_FMT_SBGGR10_1X10:
    case MEDIA_BUS_FMT_SGBRG10_1X10:
    case MEDIA_BUS_FMT_SGRBG10_1X10:
    case MEDIA_BUS_FMT_SRGGB10_1X10:
    case MEDIA_BUS_FMT_SBGGR12_1X12:
    case MEDIA_BUS_FMT_SGBRG12_1X12:
    case MEDIA_BUS_FMT_SGRBG12_1X12:
    case MEDIA_BUS_FMT_SRGGB12_1X12:
    case MEDIA_BUS_FMT_SBGGR14_1X14:
    case MEDIA_BUS_FMT_SGBRG14_1X14:
    case MEDIA_BUS_FMT_SGRBG14_1X14:
    case MEDIA_BUS_FMT_SRGGB14_1X14:
    case MEDIA_BUS_FMT_SBGGR16_1X16:
    case MEDIA_BUS_FMT_SGBRG16_1X16:
    case MEDIA_BUS_FMT_SGRBG16_1X16:
    case MEDIA_BUS_FMT_SRGGB16_1X16:
      ret = TRUE;
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_v4l2_subdev_get_colorspace (struct v4l2_subdev_format * fmt,
    GstVideoColorimetry * cinfo)
{
  gboolean is_rgb = gst_v4l2_subdev_mbus_is_rgb (fmt->format.code);
  enum v4l2_colorspace colorspace = fmt->format.colorspace;
  enum v4l2_quantization range = fmt->format.quantization;
  enum v4l2_ycbcr_encoding matrix = fmt->format.ycbcr_enc;
  enum v4l2_xfer_func transfer = fmt->format.xfer_func;
  gboolean ret = TRUE;

  /* First step, set the defaults for each primaries */
  switch (colorspace) {
    case V4L2_COLORSPACE_SMPTE170M:
      cinfo->range = GST_VIDEO_COLOR_RANGE_16_235;
      cinfo->matrix = GST_VIDEO_COLOR_MATRIX_BT601;
      cinfo->transfer = GST_VIDEO_TRANSFER_BT709;
      cinfo->primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE170M;
      break;
    case V4L2_COLORSPACE_REC709:
      cinfo->range = GST_VIDEO_COLOR_RANGE_16_235;
      cinfo->matrix = GST_VIDEO_COLOR_MATRIX_BT709;
      cinfo->transfer = GST_VIDEO_TRANSFER_BT709;
      cinfo->primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case V4L2_COLORSPACE_SRGB:
    case V4L2_COLORSPACE_JPEG:
      cinfo->range = GST_VIDEO_COLOR_RANGE_0_255;
      cinfo->matrix = GST_VIDEO_COLOR_MATRIX_BT601;
      cinfo->transfer = GST_VIDEO_TRANSFER_SRGB;
      cinfo->primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case V4L2_COLORSPACE_OPRGB:
      cinfo->range = GST_VIDEO_COLOR_RANGE_16_235;
      cinfo->matrix = GST_VIDEO_COLOR_MATRIX_BT601;
      cinfo->transfer = GST_VIDEO_TRANSFER_ADOBERGB;
      cinfo->primaries = GST_VIDEO_COLOR_PRIMARIES_ADOBERGB;
      break;
    case V4L2_COLORSPACE_BT2020:
      cinfo->range = GST_VIDEO_COLOR_RANGE_16_235;
      cinfo->matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      cinfo->transfer = GST_VIDEO_TRANSFER_BT2020_12;
      cinfo->primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case V4L2_COLORSPACE_SMPTE240M:
      cinfo->range = GST_VIDEO_COLOR_RANGE_16_235;
      cinfo->matrix = GST_VIDEO_COLOR_MATRIX_SMPTE240M;
      cinfo->transfer = GST_VIDEO_TRANSFER_SMPTE240M;
      cinfo->primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE240M;
      break;
    case V4L2_COLORSPACE_470_SYSTEM_M:
      cinfo->range = GST_VIDEO_COLOR_RANGE_16_235;
      cinfo->matrix = GST_VIDEO_COLOR_MATRIX_BT601;
      cinfo->transfer = GST_VIDEO_TRANSFER_BT709;
      cinfo->primaries = GST_VIDEO_COLOR_PRIMARIES_BT470M;
      break;
    case V4L2_COLORSPACE_470_SYSTEM_BG:
      cinfo->range = GST_VIDEO_COLOR_RANGE_16_235;
      cinfo->matrix = GST_VIDEO_COLOR_MATRIX_BT601;
      cinfo->transfer = GST_VIDEO_TRANSFER_BT709;
      cinfo->primaries = GST_VIDEO_COLOR_PRIMARIES_BT470BG;
      break;
    case V4L2_COLORSPACE_RAW:
      /* Explicitly unknown */
      cinfo->range = GST_VIDEO_COLOR_RANGE_UNKNOWN;
      cinfo->matrix = GST_VIDEO_COLOR_MATRIX_UNKNOWN;
      cinfo->transfer = GST_VIDEO_TRANSFER_UNKNOWN;
      cinfo->primaries = GST_VIDEO_COLOR_PRIMARIES_UNKNOWN;
      break;
    default:
      GST_DEBUG ("Unknown enum v4l2_colorspace %d", colorspace);
      ret = FALSE;
      break;
  }

  if (!ret)
    goto done;

  /* Second step, apply any custom variation */
  switch (range) {
    case V4L2_QUANTIZATION_FULL_RANGE:
      cinfo->range = GST_VIDEO_COLOR_RANGE_0_255;
      break;
    case V4L2_QUANTIZATION_LIM_RANGE:
      cinfo->range = GST_VIDEO_COLOR_RANGE_16_235;
      break;
    case V4L2_QUANTIZATION_DEFAULT:
      /* replicated V4L2_MAP_QUANTIZATION_DEFAULT macro behavior */
      if (is_rgb && colorspace == V4L2_COLORSPACE_BT2020)
        cinfo->range = GST_VIDEO_COLOR_RANGE_16_235;
      else if (is_rgb || matrix == V4L2_YCBCR_ENC_XV601
          || matrix == V4L2_YCBCR_ENC_XV709
          || colorspace == V4L2_COLORSPACE_JPEG)
        cinfo->range = GST_VIDEO_COLOR_RANGE_0_255;
      else
        cinfo->range = GST_VIDEO_COLOR_RANGE_16_235;
      break;
    default:
      GST_WARNING ("Unknown enum v4l2_quantization value %d", range);
      cinfo->range = GST_VIDEO_COLOR_RANGE_UNKNOWN;
      break;
  }

  switch (matrix) {
    case V4L2_YCBCR_ENC_XV601:
    case V4L2_YCBCR_ENC_SYCC:
      GST_FIXME ("XV601 and SYCC not defined, assuming 601");
      /* fallthrough */
    case V4L2_YCBCR_ENC_601:
      cinfo->matrix = GST_VIDEO_COLOR_MATRIX_BT601;
      break;
    case V4L2_YCBCR_ENC_XV709:
      GST_FIXME ("XV709 not defined, assuming 709");
      /* fallthrough */
    case V4L2_YCBCR_ENC_709:
      cinfo->matrix = GST_VIDEO_COLOR_MATRIX_BT709;
      break;
    case V4L2_YCBCR_ENC_BT2020_CONST_LUM:
      GST_FIXME ("BT2020 with constant luma is not defined, assuming BT2020");
      /* fallthrough */
    case V4L2_YCBCR_ENC_BT2020:
      cinfo->matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      break;
    case V4L2_YCBCR_ENC_SMPTE240M:
      cinfo->matrix = GST_VIDEO_COLOR_MATRIX_SMPTE240M;
      break;
    case V4L2_YCBCR_ENC_DEFAULT:
      /* nothing, just use defaults for colorspace */
      break;
    default:
      GST_WARNING ("Unknown enum v4l2_ycbcr_encoding value %d", matrix);
      cinfo->matrix = GST_VIDEO_COLOR_MATRIX_UNKNOWN;
      break;
  }

  /* Set identity matrix for R'G'B' formats to avoid creating
   * confusion. This though is cosmetic as it's now properly ignored by
   * the video info API and videoconvert. */
  if (is_rgb)
    cinfo->matrix = GST_VIDEO_COLOR_MATRIX_RGB;

  switch (transfer) {
    case V4L2_XFER_FUNC_709:
      if (colorspace == V4L2_COLORSPACE_BT2020 && fmt->format.height >= 2160)
        cinfo->transfer = GST_VIDEO_TRANSFER_BT2020_12;
      else
        cinfo->transfer = GST_VIDEO_TRANSFER_BT709;
      break;
    case V4L2_XFER_FUNC_SRGB:
      cinfo->transfer = GST_VIDEO_TRANSFER_SRGB;
      break;
    case V4L2_XFER_FUNC_OPRGB:
      cinfo->transfer = GST_VIDEO_TRANSFER_ADOBERGB;
      break;
    case V4L2_XFER_FUNC_SMPTE240M:
      cinfo->transfer = GST_VIDEO_TRANSFER_SMPTE240M;
      break;
    case V4L2_XFER_FUNC_NONE:
      GST_WARNING
          ("GAMMA 10, 18, 20, 22, 28 transfer functions all share the same V4L2 enumeration. Force caps to be renegotiated with upstream elements' caps");
      ret = FALSE;
      break;
    case V4L2_XFER_FUNC_SMPTE2084:
      cinfo->transfer = GST_VIDEO_TRANSFER_SMPTE2084;
      break;
    case V4L2_XFER_FUNC_HLG:
      cinfo->transfer = GST_VIDEO_TRANSFER_ARIB_STD_B67;
      break;
    case V4L2_XFER_FUNC_DEFAULT:
      /* nothing, just use defaults for colorspace */
      break;
    default:
      GST_WARNING ("Unknown enum v4l2_xfer_func value %d", transfer);
      cinfo->transfer = GST_VIDEO_TRANSFER_UNKNOWN;
      break;
  }

done:
  return ret;
}
