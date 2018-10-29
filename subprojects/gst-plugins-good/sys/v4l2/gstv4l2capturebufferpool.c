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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef _GNU_SOURCE
# define _GNU_SOURCE            /* O_CLOEXEC */
#endif
#include <fcntl.h>

#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

#include "gst/video/video.h"
#include "gst/video/gstvideometa.h"
#include "gst/video/gstvideopool.h"
#include "gst/allocators/gstdmabuf.h"

#include <gstv4l2capturebufferpool.h>

#include "gstv4l2object.h"
#include "gst/gst-i18n-plugin.h"
#include <gst/glib-compat-private.h>

GST_DEBUG_CATEGORY_STATIC (v4l2capturebufferpool_debug);
GST_DEBUG_CATEGORY_STATIC (CAT_PERFORMANCE);
#define GST_CAT_DEFAULT v4l2capturebufferpool_debug

#define GST_V4L2_CAPTURE_IMPORT_QUARK gst_v4l2_capture_buffer_pool_import_quark ()

/*
 * GstV4l2CaptureBufferPool:
 */
#define gst_v4l2_capture_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstV4l2CaptureBufferPool, gst_v4l2_capture_buffer_pool,
    GST_TYPE_BUFFER_POOL);

enum _GstV4l2CaptureBufferPoolAcquireFlags
{
  GST_V4L2_CAPTURE_BUFFER_POOL_ACQUIRE_FLAG_RESURRECT =
      GST_BUFFER_POOL_ACQUIRE_FLAG_LAST,
  GST_V4L2_CAPTURE_BUFFER_POOL_ACQUIRE_FLAG_LAST
};

static void gst_v4l2_capture_buffer_pool_release_buffer (GstBufferPool * bpool,
    GstBuffer * buffer);
static GstFlowReturn
gst_v4l2_proxy_buffer_pool_dqbuf (GstV4l2BufferPool * pool,
    GstBuffer ** buffer);
static GstFlowReturn
gst_v4l2_proxy_buffer_pool_process (GstV4l2BufferPool * pool, GstBuffer ** buf);
#if 0
static void
gst_v4l2_proxy_buffer_pool_release_buffer (GstBufferPool * bpool,
    GstBuffer * buffer);
#endif

static gboolean
gst_v4l2_is_buffer_valid (GstBuffer * buffer, GstV4l2MemoryGroup ** out_group)
{
  GstMemory *mem = gst_buffer_peek_memory (buffer, 0);
  gboolean valid = FALSE;

  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_TAG_MEMORY))
    goto done;

  if (gst_is_dmabuf_memory (mem))
    mem = gst_mini_object_get_qdata (GST_MINI_OBJECT (mem),
        GST_V4L2_MEMORY_QUARK);

  if (mem && gst_is_v4l2_memory (mem)) {
    GstV4l2Memory *vmem = (GstV4l2Memory *) mem;
    GstV4l2MemoryGroup *group = vmem->group;
    gint i;

    if (group->n_mem != gst_buffer_n_memory (buffer))
      goto done;

    for (i = 0; i < group->n_mem; i++) {
      if (group->mem[i] != gst_buffer_peek_memory (buffer, i))
        goto done;

      if (!gst_memory_is_writable (group->mem[i]))
        goto done;
    }

    valid = TRUE;
    if (out_group)
      *out_group = group;
  }

done:
  return valid;
}

static GQuark
gst_v4l2_capture_buffer_pool_import_quark (void)
{
  static GQuark quark = 0;

  if (quark == 0)
    quark = g_quark_from_string ("GstV4l2BufferPoolUsePtrData");

  return quark;
}

static GstFlowReturn
gst_v4l2_capture_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstV4l2CaptureBufferPool *pool = GST_V4L2_CAPTURE_BUFFER_POOL (bpool);
  GstV4l2MemoryGroup *group = NULL;
  GstBuffer *newbuf = NULL;
  GstV4l2Object *obj;
  GstVideoInfo *info;

  obj = pool->obj;
  info = &obj->info;

  group = gst_v4l2_allocator_alloc_dmabuf (pool->vallocator, pool->allocator);

  if (group != NULL) {
    gint i;
    newbuf = gst_buffer_new ();

    for (i = 0; i < group->n_mem; i++)
      gst_buffer_append_memory (newbuf, group->mem[i]);
  } else if (newbuf == NULL) {
    goto allocation_failed;
  }

  /* add metadata to raw video buffers */
  if (pool->add_videometa)
    gst_buffer_add_video_meta_full (newbuf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
        info->offset, info->stride);

  *buffer = newbuf;

  return GST_FLOW_OK;

  /* ERRORS */
allocation_failed:
  {
    GST_ERROR_OBJECT (pool, "failed to allocate buffer");
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_v4l2_capture_buffer_pool_set_config (GstBufferPool * bpool,
    GstStructure * config)
{
  GstV4l2CaptureBufferPool *pool = GST_V4L2_CAPTURE_BUFFER_POOL (bpool);
  GstV4l2Object *obj = pool->obj;
  GstCaps *caps;
  guint size, min_buffers, max_buffers;
  GstAllocator *allocator;
  GstAllocationParams params;
  gboolean can_allocate = FALSE;
  gboolean updated = FALSE;
  gboolean ret;

  pool->add_videometa =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  /* parse the config and keep around */
  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
          &max_buffers))
    goto wrong_config;

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    goto wrong_config;

  GST_DEBUG_OBJECT (pool, "config %" GST_PTR_FORMAT, config);

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  pool->allocator = gst_dmabuf_allocator_new ();
  can_allocate = GST_V4L2_ALLOCATOR_CAN_ALLOCATE (pool->vallocator, MMAP);

  if (min_buffers < GST_V4L2_MIN_BUFFERS (obj)) {
    updated = TRUE;
    min_buffers = GST_V4L2_MIN_BUFFERS (obj);
    GST_INFO_OBJECT (pool, "increasing minimum buffers to %u", min_buffers);
  }

  /* respect driver requirements */
  if (min_buffers < obj->min_buffers) {
    updated = TRUE;
    min_buffers = obj->min_buffers;
    GST_INFO_OBJECT (pool, "increasing minimum buffers to %u", min_buffers);
  }

  if (max_buffers > VIDEO_MAX_FRAME || max_buffers == 0) {
    updated = TRUE;
    max_buffers = VIDEO_MAX_FRAME;
    GST_INFO_OBJECT (pool, "reducing maximum buffers to %u", max_buffers);
  }

  if (min_buffers > max_buffers) {
    updated = TRUE;
    min_buffers = max_buffers;
    GST_INFO_OBJECT (pool, "reducing minimum buffers to %u", min_buffers);
  } else if (min_buffers != max_buffers) {
    if (!can_allocate) {
      updated = TRUE;
      max_buffers = min_buffers;
      GST_INFO_OBJECT (pool, "can't allocate, setting maximum to minimum");
    }
  }

  if (!pool->add_videometa && obj->need_video_meta) {
    GST_INFO_OBJECT (pool, "adding needed video meta");
    updated = TRUE;
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }

  /* Always update the config to ensure the configured size matches */
  gst_buffer_pool_config_set_params (config, caps, obj->info.size, min_buffers,
      max_buffers);

  ret = GST_BUFFER_POOL_CLASS (parent_class)->set_config (bpool, config);

  /* If anything was changed documentation recommand to return FALSE */
  return !updated && ret;

  /* ERRORS */
wrong_config:
  {
    GST_ERROR_OBJECT (pool, "invalid config %" GST_PTR_FORMAT, config);
    return FALSE;
  }
}

static gboolean
gst_v4l2_capture_buffer_pool_streamon (GstV4l2CaptureBufferPool * pool)
{
  GstV4l2Object *obj = pool->obj;
  g_return_val_if_fail (obj->mode == GST_V4L2_IO_DMABUF, FALSE);

  if (!pool->streaming) {
    if (obj->ioctl (pool->video_fd, VIDIOC_STREAMON, &obj->type) < 0)
      goto streamon_failed;

    pool->streaming = TRUE;

    GST_DEBUG_OBJECT (pool, "Started streaming");
  }
  return TRUE;

streamon_failed:
  {
    GST_ERROR_OBJECT (pool, "error with STREAMON %d (%s)", errno,
        g_strerror (errno));
    return FALSE;
  }
}

static void
gst_v4l2_capture_buffer_pool_streamoff (GstV4l2CaptureBufferPool * pool)
{
  GstV4l2Object *obj = pool->obj;

  g_return_if_fail (obj->mode == GST_V4L2_IO_DMABUF);

  if (pool->streaming) {
    if (obj->ioctl (pool->video_fd, VIDIOC_STREAMOFF, &obj->type) < 0)
      GST_WARNING_OBJECT (pool, "STREAMOFF failed with errno %d (%s)",
          errno, g_strerror (errno));

    pool->streaming = FALSE;

    GST_DEBUG_OBJECT (pool, "Stopped streaming");

    if (pool->vallocator)
      gst_v4l2_allocator_flush (pool->vallocator);
  }
}

static GstFlowReturn
gst_v4l2_capture_buffer_pool_resurect_buffer (GstV4l2CaptureBufferPool * pool)
{
  GstBufferPoolAcquireParams params = { 0 };
  GstBuffer *buffer = NULL;
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (pool, "A buffer was lost, reallocating it");

  params.flags = (GstBufferPoolAcquireFlags)
      GST_V4L2_CAPTURE_BUFFER_POOL_ACQUIRE_FLAG_RESURRECT |
      GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
  ret =
      gst_buffer_pool_acquire_buffer (GST_BUFFER_POOL (pool), &buffer, &params);

  if (ret == GST_FLOW_OK)
    gst_buffer_unref (buffer);

  return ret;
}

static gboolean
gst_v4l2_capture_buffer_pool_start (GstBufferPool * bpool)
{
  GstV4l2CaptureBufferPool *pool = GST_V4L2_CAPTURE_BUFFER_POOL (bpool);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);
  GstV4l2Object *obj = pool->obj;
  GstStructure *config;
  GstCaps *caps;
  guint size, min_buffers, max_buffers;
  guint max_latency, min_latency, copy_threshold = 0;
  gboolean can_allocate = FALSE;
  guint count;

  GST_DEBUG_OBJECT (pool, "activating pool");
  g_return_val_if_fail (obj->mode == GST_V4L2_IO_DMABUF, FALSE);

  config = gst_buffer_pool_get_config (bpool);
  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
          &max_buffers))
    goto wrong_config;

  min_latency = MAX (GST_V4L2_MIN_BUFFERS (obj), obj->min_buffers);

  can_allocate = GST_V4L2_ALLOCATOR_CAN_ALLOCATE (pool->vallocator, MMAP);

  /* first, lets request buffers, and see how many we can get: */
  GST_DEBUG_OBJECT (pool, "requesting %d MMAP buffers", min_buffers);

  count = gst_v4l2_allocator_start (pool->vallocator, min_buffers,
      V4L2_MEMORY_MMAP);

  if (count < GST_V4L2_MIN_BUFFERS (obj)) {
    min_buffers = count;
    goto no_buffers;
  }

  /* V4L2 buffer pool are often very limited in the amount of buffers it
   * can offer. The copy_threshold will workaround this limitation by
   * falling back to copy if the pipeline needed more buffers. This also
   * prevent having to do REQBUFS(N)/REQBUFS(0) everytime configure is
   * called. */
  if (count != min_buffers || pool->enable_copy_threshold) {
    GST_WARNING_OBJECT (pool,
        "Uncertain or not enough buffers, enabling copy threshold");
    min_buffers = count;
    copy_threshold = min_latency;
  }

  if (can_allocate)
    max_latency = max_buffers;
  else
    max_latency = min_buffers;

  pool->size = size;
  pool->copy_threshold = copy_threshold;
  pool->max_latency = max_latency;
  pool->min_latency = min_latency;
  pool->num_queued = 0;

  if (max_buffers != 0 && max_buffers < min_buffers)
    max_buffers = min_buffers;

  gst_buffer_pool_config_set_params (config, caps, size, min_buffers,
      max_buffers);
  pclass->set_config (bpool, config);
  gst_structure_free (config);

  /* now, allocate the buffers: */
  if (!pclass->start (bpool))
    goto start_failed;

  if (!V4L2_TYPE_IS_OUTPUT (obj->type))
    pool->group_released_handler =
        g_signal_connect_swapped (pool->vallocator, "group-released",
        G_CALLBACK (gst_v4l2_capture_buffer_pool_resurect_buffer), pool);

  return TRUE;

  /* ERRORS */
wrong_config:
  {
    GST_ERROR_OBJECT (pool, "invalid config %" GST_PTR_FORMAT, config);
    gst_structure_free (config);
    return FALSE;
  }
no_buffers:
  {
    GST_ERROR_OBJECT (pool,
        "we received %d buffer from device '%s', we want at least %d",
        min_buffers, obj->videodev, GST_V4L2_MIN_BUFFERS (obj));
    gst_structure_free (config);
    return FALSE;
  }
start_failed:
  {
    GST_ERROR_OBJECT (pool, "failed to start streaming");
    return FALSE;
  }
}

static gboolean
gst_v4l2_capture_buffer_pool_stop (GstBufferPool * bpool)
{
  GstV4l2CaptureBufferPool *pool = GST_V4L2_CAPTURE_BUFFER_POOL (bpool);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);
  gboolean ret;
  gint i;

  GST_DEBUG_OBJECT (pool, "stopping pool");

  if (pool->group_released_handler > 0) {
    g_signal_handler_disconnect (pool->vallocator,
        pool->group_released_handler);
    pool->group_released_handler = 0;
  }

  if (pool->proxy_pool) {
    GstV4l2BufferPool *proxy_pool =
        GST_V4L2_BUFFER_POOL_CAST (pool->proxy_pool);

    for (i = 0; i < VIDEO_MAX_FRAME; i++) {
      if (proxy_pool->buffers[i]) {
        gst_buffer_unref (proxy_pool->buffers[i]);
        proxy_pool->buffers[i] = NULL;
      }
    }
    gst_buffer_pool_set_active (pool->proxy_pool, FALSE);
    gst_object_unref (pool->proxy_pool);
    pool->proxy_pool = NULL;
  }

  gst_v4l2_capture_buffer_pool_streamoff (pool);

  for (i = 0; i < VIDEO_MAX_FRAME; i++) {
    if (pool->buffers[i]) {
      GstBuffer *buffer = pool->buffers[i];

      pool->buffers[i] = NULL;
      pclass->release_buffer (bpool, buffer);
      g_atomic_int_add (&pool->num_queued, -1);
    }
  }

  ret = GST_BUFFER_POOL_CLASS (parent_class)->stop (bpool);

  if (ret && pool->vallocator) {
    GstV4l2Return vret;

    vret = gst_v4l2_allocator_stop (pool->vallocator);
    if (vret == GST_V4L2_BUSY)
      GST_WARNING_OBJECT (pool, "some buffers are still outstanding");

    ret = (vret == GST_V4L2_OK);
  }

  return ret;
}

static void
gst_v4l2_capture_buffer_pool_flush_start (GstBufferPool * bpool)
{
  GstV4l2CaptureBufferPool *pool = GST_V4L2_CAPTURE_BUFFER_POOL (bpool);

  GST_DEBUG_OBJECT (pool, "start flushing");

  gst_poll_set_flushing (pool->poll, TRUE);

  GST_OBJECT_LOCK (pool);
  pool->empty = FALSE;
  g_cond_broadcast (&pool->empty_cond);
  GST_OBJECT_UNLOCK (pool);

  if (pool->proxy_pool)
    gst_buffer_pool_set_flushing (pool->proxy_pool, TRUE);
}

static void
gst_v4l2_capture_buffer_pool_flush_stop (GstBufferPool * bpool)
{
  GstV4l2CaptureBufferPool *pool = GST_V4L2_CAPTURE_BUFFER_POOL (bpool);
  GstV4l2Object *obj = pool->obj;
  GstBuffer *buffers[VIDEO_MAX_FRAME];
  gint i;

  GST_DEBUG_OBJECT (pool, "stop flushing");

  g_return_if_fail (obj->mode == GST_V4L2_IO_DMABUF);

  /* If we haven't started streaming yet, simply call streamon */
  if (!pool->streaming)
    goto streamon;

  GST_OBJECT_LOCK (pool);
  gst_v4l2_capture_buffer_pool_streamoff (pool);
  /* Remember buffers to re-enqueue */
  memcpy (buffers, pool->buffers, sizeof (buffers));
  memset (pool->buffers, 0, sizeof (pool->buffers));
  GST_OBJECT_UNLOCK (pool);

  /* Reset our state */
  for (i = 0; i < VIDEO_MAX_FRAME; i++) {
    /* Re-enqueue buffers */
    if (buffers[i]) {
      GstBufferPool *bpool = (GstBufferPool *) pool;
      GstBuffer *buffer = buffers[i];

      /* Remove qdata, this will unmap any map data in
       * userptr/dmabuf-import */
      gst_mini_object_set_qdata (GST_MINI_OBJECT (buffer),
          GST_V4L2_CAPTURE_IMPORT_QUARK, NULL, NULL);

      if (buffer->pool == NULL)
        gst_v4l2_capture_buffer_pool_release_buffer (bpool, buffer);

      g_atomic_int_add (&pool->num_queued, -1);
    }
  }

streamon:
  /* Start streaming on capture device only */
  gst_v4l2_capture_buffer_pool_streamon (pool);

  gst_poll_set_flushing (pool->poll, FALSE);
}

static GstFlowReturn
gst_v4l2_capture_buffer_pool_poll (GstV4l2CaptureBufferPool * pool)
{
  gint ret;

  GST_OBJECT_LOCK (pool);
  while (pool->empty)
    g_cond_wait (&pool->empty_cond, GST_OBJECT_GET_LOCK (pool));
  GST_OBJECT_UNLOCK (pool);

  if (!pool->can_poll_device)
    goto done;

  GST_LOG_OBJECT (pool, "polling device");

again:
  ret = gst_poll_wait (pool->poll, GST_CLOCK_TIME_NONE);
  if (G_UNLIKELY (ret < 0)) {
    switch (errno) {
      case EBUSY:
        goto stopped;
      case EAGAIN:
      case EINTR:
        goto again;
      case ENXIO:
        GST_WARNING_OBJECT (pool,
            "v4l2 device doesn't support polling. Disabling"
            " using libv4l2 in this case may cause deadlocks");
        pool->can_poll_device = FALSE;
        goto done;
      default:
        goto select_error;
    }
  }

  if (gst_poll_fd_has_error (pool->poll, &pool->pollfd))
    goto select_error;

done:
  return GST_FLOW_OK;

  /* ERRORS */
stopped:
  {
    GST_DEBUG_OBJECT (pool, "stop called");
    return GST_FLOW_FLUSHING;
  }
select_error:
  {
    GST_ELEMENT_ERROR (pool->obj->element, RESOURCE, READ, (NULL),
        ("poll error %d: %s (%d)", ret, g_strerror (errno), errno));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2_capture_buffer_pool_qbuf (GstV4l2CaptureBufferPool * pool,
    GstBuffer * buf)
{
  GstV4l2MemoryGroup *group = NULL;
  GstClockTime timestamp;
  gint index;

  GST_DEBUG_OBJECT (pool, "going to queue buffer %p to pool = %p", buf, pool);

  if (!gst_v4l2_is_buffer_valid (buf, &group)) {
    GST_LOG_OBJECT (pool, "unref copied/invalid buffer %p", buf);
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }

  index = group->buffer.index;

  if (pool->buffers[index] != NULL)
    goto already_queued;

  GST_LOG_OBJECT (pool, "queuing buffer with index %i", index);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    timestamp = GST_BUFFER_TIMESTAMP (buf);
    GST_TIME_TO_TIMEVAL (timestamp, group->buffer.timestamp);
  }

  GST_OBJECT_LOCK (pool);
  g_atomic_int_inc (&pool->num_queued);
  pool->buffers[index] = buf;

  if (!gst_v4l2_allocator_qbuf (pool->vallocator, group))
    goto queue_failed;

  pool->empty = FALSE;
  g_cond_signal (&pool->empty_cond);
  GST_OBJECT_UNLOCK (pool);

  return GST_FLOW_OK;

already_queued:
  {
    GST_ERROR_OBJECT (pool, "the buffer %i was already queued", index);
    return GST_FLOW_ERROR;
  }
queue_failed:
  {
    GST_ERROR_OBJECT (pool, "could not queue a buffer %i", index);
    /* Mark broken buffer to the allocator */
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_TAG_MEMORY);
    g_atomic_int_add (&pool->num_queued, -1);
    pool->buffers[index] = NULL;
    GST_OBJECT_UNLOCK (pool);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2_capture_buffer_pool_dqbuf (GstV4l2CaptureBufferPool * pool,
    GstBuffer ** buffer)
{
  GstFlowReturn res;
  GstBuffer *outbuf;
  GstV4l2Object *obj = pool->obj;
  GstClockTime timestamp;
  GstV4l2MemoryGroup *group;
  gint i;

  GST_DEBUG_OBJECT (pool, "going to poll and dequeue from pool = %p", pool);

  if ((res = gst_v4l2_capture_buffer_pool_poll (pool)) != GST_FLOW_OK)
    goto poll_failed;

  res = gst_v4l2_allocator_dqbuf (pool->vallocator, &group);
  if (res == GST_FLOW_EOS)
    goto eos;
  if (res != GST_FLOW_OK)
    goto dqbuf_failed;

  /* get our GstBuffer with that index from the pool, if the buffer was
   * outstanding we have a serious problem.
   */
  outbuf = pool->buffers[group->buffer.index];
  if (outbuf == NULL)
    goto no_buffer;

  /* mark the buffer outstanding */
  pool->buffers[group->buffer.index] = NULL;
  if (g_atomic_int_dec_and_test (&pool->num_queued)) {
    GST_OBJECT_LOCK (pool);
    pool->empty = TRUE;
    GST_OBJECT_UNLOCK (pool);
  }

  timestamp = GST_TIMEVAL_TO_TIME (group->buffer.timestamp);

#ifndef GST_DISABLE_GST_DEBUG
  for (i = 0; i < group->n_mem; i++) {
    GST_LOG_OBJECT (pool,
        "dequeued buffer %p seq:%d (ix=%d), mem %p used %d, plane=%d, flags %08x, ts %"
        GST_TIME_FORMAT ", pool-queued=%d, buffer=%p", outbuf,
        group->buffer.sequence, group->buffer.index, group->mem[i],
        group->planes[i].bytesused, i, group->buffer.flags,
        GST_TIME_ARGS (timestamp), pool->num_queued, outbuf);
  }
#endif

  /* Ignore timestamp and field for OUTPUT device */
  if (V4L2_TYPE_IS_OUTPUT (obj->type))
    goto done;

  /* Check for driver bug in reporting feild */
  if (group->buffer.field == V4L2_FIELD_ANY) {
    /* Only warn once to avoid the spamming */
#ifndef GST_DISABLE_GST_DEBUG
    if (!pool->has_warned_on_buggy_field) {
      pool->has_warned_on_buggy_field = TRUE;
      GST_WARNING_OBJECT (pool,
          "Driver should never set v4l2_buffer.field to ANY");
    }
#endif

    /* Use the value from the format (works for UVC bug) */
    group->buffer.field = obj->format.fmt.pix.field;

    /* If driver also has buggy S_FMT, assume progressive */
    if (group->buffer.field == V4L2_FIELD_ANY) {
#ifndef GST_DISABLE_GST_DEBUG
      if (!pool->has_warned_on_buggy_field) {
        pool->has_warned_on_buggy_field = TRUE;
        GST_WARNING_OBJECT (pool,
            "Driver should never set v4l2_format.pix.field to ANY");
      }
#endif

      group->buffer.field = V4L2_FIELD_NONE;
    }
  }

  /* set top/bottom field first if v4l2_buffer has the information */
  switch (group->buffer.field) {
    case V4L2_FIELD_NONE:
      GST_BUFFER_FLAG_UNSET (outbuf, GST_VIDEO_BUFFER_FLAG_INTERLACED);
      GST_BUFFER_FLAG_UNSET (outbuf, GST_VIDEO_BUFFER_FLAG_TFF);
      break;
    case V4L2_FIELD_INTERLACED_TB:
      GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_INTERLACED);
      GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_TFF);
      break;
    case V4L2_FIELD_INTERLACED_BT:
      GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_INTERLACED);
      GST_BUFFER_FLAG_UNSET (outbuf, GST_VIDEO_BUFFER_FLAG_TFF);
      break;
    case V4L2_FIELD_INTERLACED:
      GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_INTERLACED);
      if (obj->tv_norm == V4L2_STD_NTSC_M ||
          obj->tv_norm == V4L2_STD_NTSC_M_JP ||
          obj->tv_norm == V4L2_STD_NTSC_M_KR) {
        GST_BUFFER_FLAG_UNSET (outbuf, GST_VIDEO_BUFFER_FLAG_TFF);
      } else {
        GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_TFF);
      }
      break;
    default:
      GST_BUFFER_FLAG_UNSET (outbuf, GST_VIDEO_BUFFER_FLAG_INTERLACED);
      GST_BUFFER_FLAG_UNSET (outbuf, GST_VIDEO_BUFFER_FLAG_TFF);
      GST_FIXME_OBJECT (pool,
          "Unhandled enum v4l2_field %d - treating as progressive",
          group->buffer.field);
      break;
  }

  if (GST_VIDEO_INFO_FORMAT (&obj->info) == GST_VIDEO_FORMAT_ENCODED) {
    if (group->buffer.flags & V4L2_BUF_FLAG_KEYFRAME)
      GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
    else
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  if (group->buffer.flags & V4L2_BUF_FLAG_ERROR)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_CORRUPTED);

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  GST_BUFFER_OFFSET (outbuf) = group->buffer.sequence;
  GST_BUFFER_OFFSET_END (outbuf) = group->buffer.sequence + 1;

done:
  *buffer = outbuf;

  return GST_FLOW_OK;

  /* ERRORS */
poll_failed:
  {
    GST_DEBUG_OBJECT (pool, "poll error %s", gst_flow_get_name (res));
    return res;
  }
eos:
  {
    return GST_FLOW_EOS;
  }
dqbuf_failed:
  {
    return GST_FLOW_ERROR;
  }
no_buffer:
  {
    GST_ERROR_OBJECT (pool, "No free buffer found in the pool at index %d.",
        group->buffer.index);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2_capture_buffer_pool_acquire_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret;
  GstV4l2CaptureBufferPool *pool = GST_V4L2_CAPTURE_BUFFER_POOL (bpool);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);
  GstV4l2Object *obj = pool->obj;

  GST_DEBUG_OBJECT (pool, "acquire");

  g_return_val_if_fail (obj->type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
      obj->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, GST_FLOW_NOT_SUPPORTED);

  g_return_val_if_fail (obj->mode == GST_V4L2_IO_DMABUF,
      GST_FLOW_NOT_SUPPORTED);

  /* If this is being called to resurect a lost buffer */
  if (params && params->flags &
      GST_V4L2_CAPTURE_BUFFER_POOL_ACQUIRE_FLAG_RESURRECT) {
    ret = pclass->acquire_buffer (bpool, buffer, params);
    goto done;
  }

  /* just dequeue a buffer, we basically use the queue of v4l2 as the
   * storage for our buffers. This function does poll first so we can
   * interrupt it fine. */
  ret = gst_v4l2_capture_buffer_pool_dqbuf (pool, buffer);

  if (ret == GST_FLOW_OK && pool->proxy_pool) {
    GstBuffer *proxy_buf = NULL;
    GstFlowReturn proxy_ret = GST_FLOW_OK;

    proxy_ret = gst_v4l2_proxy_buffer_pool_dqbuf
        (GST_V4L2_BUFFER_POOL_CAST (pool->proxy_pool), &proxy_buf);
    if (proxy_buf)
      gst_buffer_unref (proxy_buf);
    ret = proxy_ret;
    GST_DEBUG_OBJECT (pool, "proxy pool dqbuf returned : %s",
        gst_flow_get_name (proxy_ret));
  }

done:
  return ret;
}

static void
gst_v4l2_capture_buffer_pool_release_buffer (GstBufferPool * bpool,
    GstBuffer * buffer)
{
  GstV4l2CaptureBufferPool *pool = GST_V4L2_CAPTURE_BUFFER_POOL (bpool);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);
  GstV4l2Object *obj = pool->obj;
  GstV4l2MemoryGroup *group;

  GST_DEBUG_OBJECT (pool, "release buffer %p", buffer);

  g_return_if_fail (obj->type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
      obj->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
  g_return_if_fail (obj->mode == GST_V4L2_IO_DMABUF);

  if (gst_v4l2_is_buffer_valid (buffer, &group)) {
    gst_v4l2_allocator_reset_group (pool->vallocator, group);

    /* queue back in the device */
    if (gst_v4l2_capture_buffer_pool_qbuf (pool, buffer) != GST_FLOW_OK)
      pclass->release_buffer (bpool, buffer);

    if (pool->proxy_pool) {
      GstFlowReturn proxy_ret = GST_FLOW_OK;

      GST_LOG_OBJECT (pool, "queuing buffer %p to proxy pool %p", buffer,
          pool->proxy_pool);

      proxy_ret = gst_v4l2_proxy_buffer_pool_process
          (GST_V4L2_BUFFER_POOL_CAST (pool->proxy_pool), &buffer);
      GST_DEBUG_OBJECT (pool, "pool process returned. reason : %s",
          gst_flow_get_name (proxy_ret));
      g_return_if_fail (proxy_ret == GST_FLOW_OK);
    }
  } else {
    /* Simply release invalide/modified buffer, the allocator will
     * give it back later */
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_TAG_MEMORY);
    pclass->release_buffer (bpool, buffer);
  }
}

static void
gst_v4l2_capture_buffer_pool_dispose (GObject * object)
{
  GstV4l2CaptureBufferPool *pool = GST_V4L2_CAPTURE_BUFFER_POOL (object);

  if (pool->vallocator)
    gst_object_unref (pool->vallocator);
  pool->vallocator = NULL;

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  if (pool->proxy_pool)
    gst_object_unref (pool->proxy_pool);
  pool->proxy_pool = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_v4l2_capture_buffer_pool_finalize (GObject * object)
{
  GstV4l2CaptureBufferPool *pool = GST_V4L2_CAPTURE_BUFFER_POOL (object);

  if (pool->video_fd >= 0)
    pool->obj->close (pool->video_fd);

  gst_poll_free (pool->poll);

  /* FIXME Is this required to keep around ?
   * This can't be done in dispose method because we must not set pointer
   * to NULL as it is part of the v4l2object and dispose could be called
   * multiple times */
  gst_object_unref (pool->obj->element);

  g_cond_clear (&pool->empty_cond);

  /* FIXME have we done enough here ? */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_v4l2_capture_buffer_pool_init (GstV4l2CaptureBufferPool * pool)
{
  pool->poll = gst_poll_new (TRUE);
  pool->can_poll_device = TRUE;
  pool->proxy_pool = NULL;
  g_cond_init (&pool->empty_cond);
  pool->empty = TRUE;
}

static void
gst_v4l2_capture_buffer_pool_class_init (GstV4l2CaptureBufferPoolClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *bufferpool_class = GST_BUFFER_POOL_CLASS (klass);

  object_class->dispose = gst_v4l2_capture_buffer_pool_dispose;
  object_class->finalize = gst_v4l2_capture_buffer_pool_finalize;

  bufferpool_class->start = gst_v4l2_capture_buffer_pool_start;
  bufferpool_class->stop = gst_v4l2_capture_buffer_pool_stop;
  bufferpool_class->set_config = gst_v4l2_capture_buffer_pool_set_config;
  bufferpool_class->alloc_buffer = gst_v4l2_capture_buffer_pool_alloc_buffer;
  bufferpool_class->acquire_buffer =
      gst_v4l2_capture_buffer_pool_acquire_buffer;
  bufferpool_class->release_buffer =
      gst_v4l2_capture_buffer_pool_release_buffer;
  bufferpool_class->flush_start = gst_v4l2_capture_buffer_pool_flush_start;
  bufferpool_class->flush_stop = gst_v4l2_capture_buffer_pool_flush_stop;

  GST_DEBUG_CATEGORY_INIT (v4l2capturebufferpool_debug, "v4l2capturebufferpool",
      0, "V4L2 Capture Buffer Pool");
  GST_DEBUG_CATEGORY_GET (CAT_PERFORMANCE, "GST_PERFORMANCE");
}

/**
 * gst_v4l2_capture_buffer_pool_new:
 * @obj:  the v4l2 object owning the pool
 *
 * Construct a new buffer pool.
 *
 * Returns: the new pool, use gst_object_unref() to free resources
 */
GstBufferPool *
gst_v4l2_capture_buffer_pool_new (GstV4l2Object * obj, GstCaps * caps)
{
  GstV4l2CaptureBufferPool *pool;
  GstStructure *config;
  gchar *name, *parent_name;
  gint fd;

  fd = obj->dup (obj->video_fd);
  if (fd < 0)
    goto dup_failed;

  /* setting a significant unique name */
  parent_name = gst_object_get_name (GST_OBJECT (obj->element));
  name = g_strconcat (parent_name, ":", "pool:",
      V4L2_TYPE_IS_OUTPUT (obj->type) ? "sink" : "src", NULL);
  g_free (parent_name);

  pool = (GstV4l2CaptureBufferPool *)
      g_object_new (GST_TYPE_V4L2_CAPTURE_BUFFER_POOL, "name", name, NULL);
  g_free (name);

  gst_poll_fd_init (&pool->pollfd);
  pool->pollfd.fd = fd;
  gst_poll_add_fd (pool->poll, &pool->pollfd);
  if (V4L2_TYPE_IS_OUTPUT (obj->type))
    gst_poll_fd_ctl_write (pool->poll, &pool->pollfd, TRUE);
  else
    gst_poll_fd_ctl_read (pool->poll, &pool->pollfd, TRUE);

  pool->video_fd = fd;
  pool->obj = obj;
  pool->can_poll_device = TRUE;

  pool->vallocator = gst_v4l2_allocator_new (GST_OBJECT (pool), obj);
  if (pool->vallocator == NULL)
    goto allocator_failed;

  gst_object_ref (obj->element);

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));
  gst_buffer_pool_config_set_params (config, caps, obj->info.size, 0, 0);
  /* This will simply set a default config, but will not configure the pool
   * because min and max are not valid */
  gst_buffer_pool_set_config (GST_BUFFER_POOL_CAST (pool), config);

  return GST_BUFFER_POOL (pool);

  /* ERRORS */
dup_failed:
  {
    GST_ERROR ("failed to dup fd %d (%s)", errno, g_strerror (errno));
    return NULL;
  }
allocator_failed:
  {
    GST_ERROR_OBJECT (pool, "Failed to create V4L2 allocator");
    gst_object_unref (pool);
    return NULL;
  }
}

void
gst_v4l2_capture_buffer_pool_set_proxy_pool (GstV4l2CaptureBufferPool * pool,
    GstBufferPool * proxy_pool)
{
  g_return_if_fail (!gst_buffer_pool_is_active (GST_BUFFER_POOL (pool)));

  if (pool->proxy_pool)
    gst_object_unref (pool->proxy_pool);
  pool->proxy_pool = gst_object_ref (proxy_pool);
}

void
gst_v4l2_capture_buffer_pool_copy_at_threshold (GstV4l2CaptureBufferPool * pool,
    gboolean copy)
{
  GST_OBJECT_LOCK (pool);
  pool->enable_copy_threshold = copy;
  GST_OBJECT_UNLOCK (pool);
}

/*****************************************************************************
 ********                   Proxy Pool APIs                             ******
 *****************************************************************************/

static gboolean
gst_v4l2_proxy_buffer_pool_streamon (GstV4l2BufferPool * pool)
{
  GstV4l2Object *obj = pool->obj;
  g_return_val_if_fail (obj->mode == GST_V4L2_IO_DMABUF_IMPORT, FALSE);

  if (!pool->streaming) {
    if (obj->ioctl (pool->video_fd, VIDIOC_STREAMON, &obj->type) < 0)
      goto streamon_failed;

    pool->streaming = TRUE;

    GST_DEBUG_OBJECT (pool, "Started streaming");
  }
  return TRUE;

streamon_failed:
  {
    GST_ERROR_OBJECT (pool, "error with STREAMON %d (%s)", errno,
        g_strerror (errno));
    return FALSE;
  }
}

static GstFlowReturn
gst_v4l2_proxy_buffer_pool_poll (GstV4l2BufferPool * pool)
{
  gint ret;

  GST_OBJECT_LOCK (pool);
  while (pool->empty)
    g_cond_wait (&pool->empty_cond, GST_OBJECT_GET_LOCK (pool));
  GST_OBJECT_UNLOCK (pool);

  if (!pool->can_poll_device)
    goto done;

  GST_LOG_OBJECT (pool, "polling device");

again:
  ret = gst_poll_wait (pool->poll, GST_CLOCK_TIME_NONE);
  if (G_UNLIKELY (ret < 0)) {
    switch (errno) {
      case EBUSY:
        goto stopped;
      case EAGAIN:
      case EINTR:
        goto again;
      case ENXIO:
        GST_WARNING_OBJECT (pool,
            "v4l2 device doesn't support polling. Disabling"
            " using libv4l2 in this case may cause deadlocks");
        pool->can_poll_device = FALSE;
        goto done;
      default:
        goto select_error;
    }
  }

  if (gst_poll_fd_has_error (pool->poll, &pool->pollfd))
    goto select_error;

done:
  return GST_FLOW_OK;

  /* ERRORS */
stopped:
  {
    GST_DEBUG_OBJECT (pool, "stop called");
    return GST_FLOW_FLUSHING;
  }
select_error:
  {
    GST_ELEMENT_ERROR (pool->obj->element, RESOURCE, READ, (NULL),
        ("poll error %d: %s (%d)", ret, g_strerror (errno), errno));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2_proxy_buffer_pool_qbuf (GstV4l2BufferPool * pool, GstBuffer * buf)
{
  GstV4l2MemoryGroup *group = NULL;
  GstClockTime timestamp;
  gint index;

  GST_DEBUG_OBJECT (pool, "going to queue buffer %p to pool = %p", buf, pool);

  if (!gst_v4l2_is_buffer_valid (buf, &group)) {
    GST_WARNING_OBJECT (pool, "unref copied/invalid buffer %p", buf);
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }

  index = group->buffer.index;

  if (pool->buffers[index] != NULL)
    goto already_queued;

  GST_LOG_OBJECT (pool, "queuing buffer %i", index);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    timestamp = GST_BUFFER_TIMESTAMP (buf);
    GST_TIME_TO_TIMEVAL (timestamp, group->buffer.timestamp);
  }

  GST_OBJECT_LOCK (pool);
  g_atomic_int_inc (&pool->num_queued);
  pool->buffers[index] = buf;

  if (!gst_v4l2_allocator_qbuf (pool->vallocator, group))
    goto queue_failed;

  pool->empty = FALSE;
  g_cond_signal (&pool->empty_cond);
  GST_OBJECT_UNLOCK (pool);

  return GST_FLOW_OK;

already_queued:
  {
    GST_ERROR_OBJECT (pool, "the buffer %i was already queued", index);
    return GST_FLOW_ERROR;
  }
queue_failed:
  {
    GST_ERROR_OBJECT (pool, "could not queue a buffer %i", index);
    /* Mark broken buffer to the allocator */
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_TAG_MEMORY);
    g_atomic_int_add (&pool->num_queued, -1);
    pool->buffers[index] = NULL;
    GST_OBJECT_UNLOCK (pool);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2_proxy_buffer_pool_dqbuf (GstV4l2BufferPool * pool, GstBuffer ** buffer)
{
  GstFlowReturn res;
  GstBuffer *outbuf;
  GstClockTime timestamp;
  GstV4l2MemoryGroup *group;
  gint i;

  GST_DEBUG_OBJECT (pool, "going to poll and dequeue from pool = %p", pool);

  if ((res = gst_v4l2_proxy_buffer_pool_poll (pool)) != GST_FLOW_OK)
    goto poll_failed;

  res = gst_v4l2_allocator_dqbuf (pool->vallocator, &group);
  if (res == GST_FLOW_EOS)
    goto eos;
  if (res != GST_FLOW_OK)
    goto dqbuf_failed;

  /* get our GstBuffer with that index from the pool, if the buffer was
   * outstanding we have a serious problem.
   */
  outbuf = pool->buffers[group->buffer.index];
  if (outbuf == NULL)
    goto no_buffer;

  /* mark the buffer outstanding */
  pool->buffers[group->buffer.index] = NULL;
  if (g_atomic_int_dec_and_test (&pool->num_queued)) {
    GST_OBJECT_LOCK (pool);
    pool->empty = TRUE;
    GST_OBJECT_UNLOCK (pool);
  }

  timestamp = GST_TIMEVAL_TO_TIME (group->buffer.timestamp);

#ifndef GST_DISABLE_GST_DEBUG
  for (i = 0; i < group->n_mem; i++) {
    GST_LOG_OBJECT (pool,
        "dequeued buffer %p seq:%d (ix=%d), mem %p used %d, plane=%d, flags %08x, ts %"
        GST_TIME_FORMAT ", pool-queued=%d, buffer=%p", outbuf,
        group->buffer.sequence, group->buffer.index, group->mem[i],
        group->planes[i].bytesused, i, group->buffer.flags,
        GST_TIME_ARGS (timestamp), pool->num_queued, outbuf);
  }
#endif

  *buffer = outbuf;

  return GST_FLOW_OK;

  /* ERRORS */
poll_failed:
  {
    GST_ERROR_OBJECT (pool, "poll error %s", gst_flow_get_name (res));
    return res;
  }
eos:
  {
    GST_ERROR_OBJECT (pool, "returing EOS");
    return GST_FLOW_EOS;
  }
dqbuf_failed:
  {
    GST_ERROR_OBJECT (pool, "dequeue failed");
    return GST_FLOW_ERROR;
  }
no_buffer:
  {
    GST_ERROR_OBJECT (pool, "No free buffer found in the pool at index %d.",
        group->buffer.index);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2_proxy_buffer_pool_import_dmabuf (GstV4l2BufferPool * pool,
    GstBuffer * dest, GstBuffer * src)
{
  GstV4l2MemoryGroup *group = NULL;
  GstMemory *dma_mem[GST_VIDEO_MAX_PLANES] = { 0 };
  guint n_mem = gst_buffer_n_memory (src);
  gint i;

  GST_LOG_OBJECT (pool, "importing dmabuf %p to %p", src, dest);

  if (!gst_v4l2_is_buffer_valid (dest, &group))
    goto not_our_buffer;

  if (n_mem > GST_VIDEO_MAX_PLANES)
    goto too_many_mems;

  for (i = 0; i < n_mem; i++)
    dma_mem[i] = gst_buffer_peek_memory (src, i);

  if (!gst_v4l2_allocator_import_dmabuf (pool->vallocator, group, n_mem,
          dma_mem))
    goto import_failed;

  gst_mini_object_set_qdata (GST_MINI_OBJECT (dest),
      GST_V4L2_CAPTURE_IMPORT_QUARK,
      gst_buffer_ref (src), (GDestroyNotify) gst_buffer_unref);

  gst_buffer_copy_into (dest, src,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;

not_our_buffer:
  {
    GST_ERROR_OBJECT (pool, "destination buffer invalid or not from our pool");
    return GST_FLOW_ERROR;
  }
too_many_mems:
  {
    GST_ERROR_OBJECT (pool, "could not map buffer");
    return GST_FLOW_ERROR;
  }
import_failed:
  {
    GST_ERROR_OBJECT (pool, "failed to import dmabuf");
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2_proxy_buffer_pool_process (GstV4l2BufferPool * pool, GstBuffer ** buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBufferPool *bpool = GST_BUFFER_POOL_CAST (pool);
  GstV4l2Object *obj = pool->obj;
  GstBuffer *to_queue = NULL;
  GstV4l2MemoryGroup *group;
  GstBufferPoolAcquireParams params = { 0 };

  GST_DEBUG_OBJECT (pool, "process buffer %p", *buf);

  g_return_val_if_fail (obj->type == V4L2_BUF_TYPE_VIDEO_OUTPUT ||
      obj->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, GST_FLOW_NOT_SUPPORTED);

  g_return_val_if_fail (obj->mode == GST_V4L2_IO_DMABUF_IMPORT,
      GST_FLOW_NOT_SUPPORTED);

  g_return_val_if_fail ((*buf)->pool != bpool, GST_FLOW_NOT_SUPPORTED);

  if (GST_BUFFER_POOL_IS_FLUSHING (pool))
    return GST_FLOW_FLUSHING;

  GST_LOG_OBJECT (pool, "alloc buffer from our pool");

  /* this can return EOS if all buffers are outstanding which would
   * be strange because we would expect the upstream element to have
   * allocated them and returned to us.. */
  params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
  ret = gst_buffer_pool_acquire_buffer (bpool, &to_queue, &params);
  if (ret != GST_FLOW_OK)
    goto acquire_failed;

  GST_LOG_OBJECT (pool, "acquired buffer %p from proxy_pool", to_queue);

  ret = gst_v4l2_proxy_buffer_pool_import_dmabuf (pool, to_queue, *buf);
  if (ret != GST_FLOW_OK) {
    gst_buffer_unref (to_queue);
    goto prepare_failed;
  }

  if ((ret = gst_v4l2_proxy_buffer_pool_qbuf (pool, to_queue)) != GST_FLOW_OK)
    goto queue_failed;

  /* if we are not streaming yet (this is the first buffer, start
   * streaming now */
  if (!gst_v4l2_proxy_buffer_pool_streamon (pool)) {
    /* don't check return value because qbuf would have failed */
    gst_v4l2_is_buffer_valid (to_queue, &group);

    /* qbuf has stored to_queue buffer but we are not in
     * streaming state, so the flush logic won't be performed.
     * To avoid leaks, flush the allocator and restore the queued
     * buffer as non-queued */
    gst_v4l2_allocator_flush (pool->vallocator);

    pool->buffers[group->buffer.index] = NULL;

    gst_mini_object_set_qdata (GST_MINI_OBJECT (to_queue),
        GST_V4L2_CAPTURE_IMPORT_QUARK, NULL, NULL);
    gst_buffer_unref (to_queue);
    g_atomic_int_add (&pool->num_queued, -1);
    goto start_failed;
  }

  return ret;

  /* ERRORS */
acquire_failed:
  {
    if (ret == GST_FLOW_FLUSHING)
      GST_DEBUG_OBJECT (pool, "flushing");
    else
      GST_WARNING_OBJECT (pool, "failed to acquire a buffer: %s",
          gst_flow_get_name (ret));
    return ret;
  }
prepare_failed:
  {
    GST_ERROR_OBJECT (pool, "failed to prepare data");
    return ret;
  }
queue_failed:
  {
    GST_ERROR_OBJECT (pool, "failed to queue buffer");
    return ret;
  }
start_failed:
  {
    GST_ERROR_OBJECT (pool, "failed to start streaming");
    return GST_FLOW_ERROR;
  }
}

#if 0
static void
gst_v4l2_proxy_buffer_pool_release_buffer (GstBufferPool * bpool,
    GstBuffer * buffer)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);
  GstV4l2Object *obj = pool->obj;
  GstV4l2MemoryGroup *group;
  guint index;

  GST_DEBUG_OBJECT (pool, "release buffer %p", buffer);

  g_return_if_fail (obj->type == V4L2_BUF_TYPE_VIDEO_OUTPUT ||
      obj->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

  g_return_if_fail (obj->mode == GST_V4L2_IO_DMABUF_IMPORT);

  if (!gst_v4l2_is_buffer_valid (buffer, &group)) {
    /* Simply release invalide/modified buffer, the allocator will
     * give it back later */
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_TAG_MEMORY);
    pclass->release_buffer (bpool, buffer);
    return;
  }

  index = group->buffer.index;

  if (pool->buffers[index] == NULL) {
    GST_LOG_OBJECT (pool, "buffer %u not queued, putting on free list", index);

    /* Remove qdata, this will unmap any map data in userptr */
    gst_mini_object_set_qdata (GST_MINI_OBJECT (buffer),
        GST_V4L2_CAPTURE_IMPORT_QUARK, NULL, NULL);

    /* reset to default size */
    gst_v4l2_allocator_reset_group (pool->vallocator, group);

    /* playback, put the buffer back in the queue to refill later. */
    pclass->release_buffer (bpool, buffer);
  } else {
    /* the buffer is queued in the device but maybe not played yet. We just
     * leave it there and not make it available for future calls to acquire
     * for now. The buffer will be dequeued and reused later. */
    GST_LOG_OBJECT (pool, "buffer %u is queued", index);
  }
}
#endif
