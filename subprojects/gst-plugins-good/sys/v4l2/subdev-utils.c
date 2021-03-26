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
