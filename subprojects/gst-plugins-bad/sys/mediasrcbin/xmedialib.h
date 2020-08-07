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
