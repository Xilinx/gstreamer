/*
 * Copyright (C) 2020 Xilinx Inc.
 *     Author: Dylan Yip <dylan.yip@xilinx.com>
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

#ifndef __SUBDEV_UTILS_H__
#define __SUBDEV_UTILS_H__

#include <gst/gst.h>

#include "gstv4l2object.h"
#include "ext/v4l2-subdev.h"


G_BEGIN_DECLS

typedef struct _GstV4l2Subdev GstV4l2Subdev;

struct _GstV4l2Subdev
{
  GstV4l2Object * subdev;
  GstPoll * event_poll;
  GstPollFD poll_fd;
};

GstV4l2Subdev *    gst_v4l2_subdev_new (GstV4l2Object * obj);
void               gst_v4l2_subdev_free (GstV4l2Subdev * subdev);
void               gst_v4l2_subdev_free_gpointer (gpointer data);

gboolean           gst_v4l2_subdev_g_fmt (GstV4l2Subdev * v4l2subdev, struct v4l2_subdev_format * fmt);
gboolean           gst_v4l2_subdev_get_colorspace (struct v4l2_subdev_format * fmt, GstVideoColorimetry * cinfo);

G_END_DECLS

#endif /* __SUBDEV_UTILS_H__ */
