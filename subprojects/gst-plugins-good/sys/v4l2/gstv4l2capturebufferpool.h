/* GStreamer
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@gmail.com>
 *               2009 Texas Instruments, Inc - http://www.ti.com/
 *               2018 Naveen Cherukuri <naveenc@xilinx.com>
 *
 * This is modified version of gstv4l2bufferpool.c to support
 * Xilinx's 1-N v4l2 based multi scaler.
 *   1. Holds reference to output v4l2object's pool as proxy pool
 *   2. proxy pool should be in dmabuf-import io-mode
 *   3. When a buffer is queued at capture side, same buffer is queued
 *      to proxy pool in dmabuf-import mode
 *   4. When a buffer is dequeued at capture side, a buffer from proxy
 *      pool will also be dequeued
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

#ifndef __GST_V4L2_CAPTURE_BUFFER_POOL_H__
#define __GST_V4L2_CAPTURE_BUFFER_POOL_H__

#include <gst/gst.h>

typedef struct _GstV4l2CaptureBufferPool GstV4l2CaptureBufferPool;
typedef struct _GstV4l2CaptureBufferPoolClass GstV4l2CaptureBufferPoolClass;

#include "gstv4l2object.h"
#include "gstv4l2allocator.h"
#include <gstv4l2bufferpool.h>

G_BEGIN_DECLS
#define GST_TYPE_V4L2_CAPTURE_BUFFER_POOL      (gst_v4l2_capture_buffer_pool_get_type())
#define GST_IS_V4L2_CAPTURE_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_V4L2_CAPTURE_BUFFER_POOL))
#define GST_V4L2_CAPTURE_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_V4L2_CAPTURE_BUFFER_POOL, GstV4l2CaptureBufferPool))
#define GST_V4L2_CAPTURE_BUFFER_POOL_CAST(obj) ((GstV4l2CaptureBufferPool*)(obj))
/* GstV4l2CaptureBufferPool should be same as GstV4l2BufferPool,
 * as GstV4l2Object expects pool of type GstV4l2BufferPool
 */
    struct _GstV4l2CaptureBufferPool
{
  GstBufferPool parent;

  GstV4l2Object *obj;           /* the v4l2 object */
  gint video_fd;                /* a dup(2) of the v4l2object's video_fd */
  GstPoll *poll;                /* a poll for video_fd */
  GstPollFD pollfd;
  gboolean can_poll_device;

  gboolean empty;
  GCond empty_cond;

  GstV4l2Allocator *vallocator;
  GstAllocator *allocator;
  GstAllocationParams params;   /*unused varaible */
  GstBufferPool *proxy_pool;
  guint size;
  GstVideoInfo caps_info;       /*unused varaible */

  gboolean add_videometa;       /* set if video meta should be added */
  gboolean enable_copy_threshold;       /* If copy_threshold should be set */

  guint min_latency;            /* number of buffers we will hold */
  guint max_latency;            /* number of buffers we can hold */
  guint num_queued;             /* number of buffers queued in the driver */
  guint copy_threshold;         /* when our pool runs lower, start handing out copies */

  gboolean streaming;
  gboolean flushing;

  GstBuffer *buffers[VIDEO_MAX_FRAME];

  /* signal handlers */
  gulong group_released_handler;

  /* Control to warn only once on buggy feild driver bug */
  gboolean has_warned_on_buggy_field;
};

struct _GstV4l2CaptureBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType gst_v4l2_capture_buffer_pool_get_type (void);
GstBufferPool *gst_v4l2_capture_buffer_pool_new (GstV4l2Object * obj,
    GstCaps * caps);
void gst_v4l2_capture_buffer_pool_set_proxy_pool (GstV4l2CaptureBufferPool *
    pool, GstBufferPool * proxy_pool);
void gst_v4l2_capture_buffer_pool_copy_at_threshold (GstV4l2CaptureBufferPool *
    pool, gboolean copy);
G_END_DECLS
#endif /*__GST_V4L2_CAPTURE_BUFFER_POOL_H__ */
