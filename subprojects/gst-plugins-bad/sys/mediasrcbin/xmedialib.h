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

#ifndef __XMEDIALIB_H__
#define __XMEDIALIB_H__

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/media.h>
#include <linux/types.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>

#include <mediactl/mediactl.h>
#include <mediactl/v4l2subdev.h>

#define MAX_MEDIA_DEVS	255

#define ARRAY_SIZE(array)	(sizeof(array) / sizeof((array)[0]))

struct media_entity
{
  struct media_device *media;
  struct media_entity_desc info;
  struct media_pad *pads;
  struct media_link *links;
  unsigned int max_links;
  unsigned int num_links;

  char dev_name[32];
  int fd;
};

struct media_device
{
  int fd;
  int refcount;
  char *devnode;

  struct media_device_info info;
  struct media_entity *entities;
  unsigned int entities_count;

  struct
  {
    struct media_entity *v4l;
  } def;
};

struct xml_vdev_fmt_info
{
  unsigned int width;
  unsigned int height;
  unsigned int frm_num;
  unsigned int frm_den;
  unsigned int code;
};

int xml_get_media_dev_count (void);
int xml_get_video_dev_count (char media_dev_name[]);
int xml_get_video_dev_name (char media_dev_name[], unsigned int index,
    char video_dev_name[]);
int xml_init_media_dev (const char *mediadev_path);
int xml_set_connected_src_pad (const char *video_dev_path,
    struct xml_vdev_fmt_info *fmt);

#endif
