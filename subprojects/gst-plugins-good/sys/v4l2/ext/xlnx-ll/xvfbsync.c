/*
* Copyright (C) 2019 - 2020  Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person
* obtaining a copy of this software and associated documentation
* files (the "Software"), to deal in the Software without restriction,
* including without limitation the rights to use, copy, modify, merge,
* publish, distribute, sublicense, and/or sell copies of the Software,
* and to permit persons to whom the Software is furnished to do so,
* subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in this
* Software without prior written authorization from Xilinx.
*
*/

#include <assert.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include "xvfbsync.h"

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

#include "xlnxsync.h"

#define FOURCC2(A, B, C, D) ((uint32_t)(((uint32_t)((A))) \
                                       | ((uint32_t)((B)) << 8) \
                                       | ((uint32_t)((C)) << 16) \
                                       | ((uint32_t)((D)) << 24)))

#define FOURCC_MAPPING(FCC, ChromaMode, BD, StorageMode, ChromaOrder, \
		       Compression, Packed10) \
                       { FCC, { ChromaMode, BD, StorageMode, ChromaOrder, \
				Compression, Packed10 } }

#define MIN(a, b) ((a) < (b) ? a : b)

/**********************************/
/* xvfbsync private structs/enums */
/**********************************/

static int logs_enabled = 0;

typedef enum
{
  POLL_SUCCESS,
  POLL_ERROR_UNKNOWN,
  POLL_ERROR_NO_MEMORY,
  POLL_ERROR_CHANNEL,
  POLL_TIMEOUT,
} EPollError;

typedef enum
{
  CHROMA_MONO,
  CHROMA_4_0_0 = CHROMA_MONO,
  CHROMA_4_2_0,
  CHROMA_4_2_2,
  CHROMA_4_4_4,
  CHROMA_MAX_ENUM,              /* sentinel */
} EChromaMode;

typedef enum
{
  FB_RASTER = 0,
  FB_TILE_32x4 = 2,
  FB_TILE_64x4 = 3,
  FB_MAX_ENUM,                  /* sentinel */
} EFbStorageMode;

typedef enum
{
  C_ORDER_NO_CHROMA,
  C_ORDER_U_V,
  C_ORDER_V_U,
  C_ORDER_SEMIPLANAR
} EChromaOrder;

/**
 * struct TPicFormat - Format obtained from fourCC
 * @e_chroma_mode: Chroma mode
 * @u_bit_depth: Bit depth
 * @e_storage_mode: Storage mode
 * @e_chroma_order: Chroma order
 * @b_compressed: True if compressed
 * @b_10bPacked: True if 10bit packed
 */
typedef struct
{
  EChromaMode e_chroma_mode;
  u8 u_bit_depth;
  EFbStorageMode e_storage_mode;
  EChromaOrder e_chroma_order;
  bool b_compressed;
  bool b_10b_packed;
} TPicFormat;

/**
 * struct TFourCCMapping - Mapping of fourCC to format string
 * @t_fourcc: fourCC to be mapped
 * @t_pict_format: Format to be mapped to
 */
typedef struct
{
  u32 t_fourcc;
  TPicFormat t_pict_format;
} TFourCCMapping;

/******************/
/* xvfbsync Queue */
/******************/

static bool
xvfbsync_queue_init (Queue * q)
{
  if (!q) {
    fprintf (stderr, "Queue: Queue is NULL\n");
    return false;
  }

  q->front = NULL;
  q->last = NULL;
  q->size = 0;

  return true;
}

static bool
xvfbsync_queue_empty (Queue * q)
{
  if (!q) {
    fprintf (stderr, "Queue: Queue is NULL\n");
    return false;
  }

  return q->size == 0;
}

static XLNXLLBuf *
xvfbsync_queue_front (Queue * q)
{
  if (!q) {
    fprintf (stderr, "Queue: Queue is NULL\n");
    return NULL;
  }

  return q->front->buf;
}

static bool
xvfbsync_queue_pop (Queue * q)
{
  struct Node *temp;

  if (!q || xvfbsync_queue_empty (q)) {
    fprintf (stderr, "Queue: Queue is NULL or empty\n");
    return false;
  }

  q->size--;
  temp = q->front;
  q->front = q->front->next;
  if (temp)
    free (temp);

  return true;
}

static bool
xvfbsync_queue_push (Queue * q, XLNXLLBuf * buf_ptr)
{
  if (!q) {
    fprintf (stderr, "Queue: Queue is NULL\n");
    return false;
  }

  q->size++;

  if (q->front == NULL) {
    q->front = (struct Node *) calloc (1, sizeof (struct Node));
    if (!q->front) {
      fprintf (stderr, "Queue: Memory allocation failed\n");
      return false;
    }

    q->front->buf = buf_ptr;
    q->front->next = NULL;
    q->last = q->front;
  } else {
    q->last->next = (struct Node *) calloc (1, sizeof (struct Node));
    if (!q->last->next) {
      fprintf (stderr, "Queue: Memory allocation failed\n");
      return false;
    }

    q->last->next->buf = buf_ptr;
    q->last->next->next = NULL;
    q->last = q->last->next;
  }

  return false;
}

/***************************/
/* xvfbsync syncip helpers */
/***************************/

static void
xvfbsync_syncip_parse_chan_status (struct xlnxsync_stat *status,
    ChannelStatus * channel_statuses, u8 max_channels,
    u8 max_users, u8 max_buffers)
{
  for (u8 channel = 0; channel < max_channels; ++channel) {
    ChannelStatus *channel_status = &(channel_statuses[channel]);

    for (u8 buffer = 0; buffer < max_buffers; ++buffer) {
      for (u8 user = 0; user < max_users; ++user)
        channel_status->fb_avail[buffer][user] =
            status->fbdone[channel][buffer][user];
    }

    channel_status->enable = status->enable[channel];
    channel_status->sync_error = status->sync_err[channel];
    channel_status->watchdog_error = status->wdg_err[channel];
    channel_status->luma_diff_error = status->ldiff_err[channel];
    channel_status->chroma_diff_error = status->cdiff_err[channel];
  }
}

static int
xvfbsync_syncip_get_latest_chan_status (SyncIp * syncip)
{
  struct xlnxsync_stat chan_status;
  int ret = 0;

  ret = ioctl (syncip->fd, XLNXSYNC_GET_CHAN_STATUS, &chan_status);
  if (ret)
    fprintf (stderr, "SyncIp: Couldn't get sync ip channel status\n");
  else
    xvfbsync_syncip_parse_chan_status (&chan_status, syncip->channel_statuses,
        syncip->max_channels, syncip->max_users, syncip->max_buffers);

  return ret;
}

static int
xvfbsync_syncip_reset_status (SyncIp * syncip, u8 chan_id)
{
  struct xlnxsync_clr_err clr;
  int ret = 0;

  clr.channel_id = chan_id;
  clr.sync_err = 1;
  clr.wdg_err = 1;
  clr.ldiff_err = 1;
  clr.cdiff_err = 1;

  ret = ioctl (syncip->fd, XLNXSYNC_CLR_CHAN_ERR, &clr);
  if (ret)
    fprintf (stderr, "SyncIp: Couldnt reset status of channel %d\n", chan_id);

  return ret;
}

static int
xvfbsync_syncip_enable_channel (SyncIp * syncip, u8 chan_id)
{
  u8 chan = chan_id;
  int ret = 0;

  ret = ioctl (syncip->fd, XLNXSYNC_CHAN_ENABLE, (void *) (uintptr_t) chan);
  if (ret)
    fprintf (stderr, "SyncIp: Couldn't enable channel %d\n", chan_id);

  return ret;
}

static int
xvfbsync_syncip_disable_channel (SyncIp * syncip, u8 chan_id)
{
  u8 chan = chan_id;
  int ret = 0;

  ret = ioctl (syncip->fd, XLNXSYNC_CHAN_DISABLE, (void *) (uintptr_t) chan);
  if (ret)
    fprintf (stderr, "SyncIp: Couldn't disable channel %d\n", chan_id);

  return ret;
}

static int
xvfbsync_syncip_add_buffer (SyncIp * syncip,
    struct xlnxsync_chan_config *fb_config)
{
  int ret = 0;

  ret = ioctl (syncip->fd, XLNXSYNC_SET_CHAN_CONFIG, fb_config);
  if (ret)
    fprintf (stderr, "SyncIp: Couldn't add buffer\n");

  return ret;
}

static void
xvfbsync_syncip_poll_errors (SyncIp * syncip, int timeout)
{
  EPollError ret_code;
  struct pollfd poll_data;

  poll_data.events = POLLPRI;
  poll_data.fd = (int) (intptr_t) syncip->fd;

  ret_code = poll (&poll_data, 1, timeout);
  if (ret_code == POLL_TIMEOUT)
    return;

  pthread_mutex_lock (&(syncip->mutex));
  xvfbsync_syncip_get_latest_chan_status (syncip);

  for (u8 i = 0; i < syncip->max_channels; ++i) {
    ChannelStatus *status = &(syncip->channel_statuses[i]);

    if (syncip->event_listeners[i] && (status->sync_error
            || status->watchdog_error || status->luma_diff_error
            || status->chroma_diff_error)) {
      syncip->event_listeners[i] (status);
      xvfbsync_syncip_reset_status (syncip, i);
    }
  }

  pthread_mutex_unlock (&(syncip->mutex));
}

static void *
xvfbsync_syncip_polling_routine (void *arg)
{
  SyncIp *syncip = ((ThreadInfo *) arg)->syncip;

  while (true) {
    pthread_mutex_lock (&(syncip->mutex));
    if (syncip->quit) {
      break;
    }
    pthread_mutex_unlock (&(syncip->mutex));
    xvfbsync_syncip_poll_errors (syncip, 500);
  }

  pthread_mutex_unlock (&(syncip->mutex));
  xvfbsync_syncip_poll_errors (syncip, 0);
  if (arg)
    free ((ThreadInfo *) arg);

  return NULL;
}

static void
xvfbsync_syncip_add_listener (SyncIp * syncip, u8 chan_id,
    void (*delegate) (ChannelStatus *))
{
  pthread_mutex_lock (&(syncip->mutex));
  syncip->event_listeners[chan_id] = delegate;
  pthread_mutex_unlock (&(syncip->mutex));
}

static void
xvfbsync_syncip_remove_listener (SyncIp * syncip, u8 chan_id)
{
  pthread_mutex_lock (&(syncip->mutex));
  syncip->event_listeners[chan_id] = NULL;
  pthread_mutex_unlock (&(syncip->mutex));
}

/*******************/
/* xvfbsync syncIP */
/*******************/

ChannelStatus *
xvfbsync_syncip_get_status (SyncIp * syncip, u8 chan_id)
{
  pthread_mutex_lock (&(syncip->mutex));
  xvfbsync_syncip_get_latest_chan_status (syncip);
  pthread_mutex_unlock (&(syncip->mutex));

  return &(syncip->channel_statuses[chan_id]);
}

int
xvfbsync_syncip_get_free_channel (SyncIp * syncip)
{
  u8 chan_id;

  pthread_mutex_lock (&(syncip->mutex));
  xvfbsync_syncip_get_latest_chan_status (syncip);

  if (ioctl (syncip->fd, XLNXSYNC_RESERVE_GET_CHAN_ID, &chan_id)) {
    fprintf (stderr, "SyncIp: Couldn't get sync ip channel ID\n");
    return -1;
  }

  pthread_mutex_unlock (&(syncip->mutex));
  return chan_id;
}

int
xvfbsync_syncip_populate (SyncIp * syncip, u32 fd)
{
  ThreadInfo *t_info;
  struct xlnxsync_config config;
  int ret = 0;

  if (getenv ("SYNCIP_LOGS") != NULL)
    logs_enabled = atoi (getenv ("SYNCIP_LOGS"));

  t_info = calloc (1, sizeof (ThreadInfo));
  if (!t_info) {
    fprintf (stderr, "SyncIp: Memory allocation failed\n");
    return -1;
  }

  t_info->syncip = syncip;
  syncip->quit = false;
  syncip->fd = fd;

  ret = ioctl (syncip->fd, XLNXSYNC_GET_CFG, &config);
  if (ret) {
    fprintf (stderr, "SyncIp: Couldn't get sync ip configuration\n");
    return ret;
  }

  fprintf (stdout, "[fd: %d] mode: %s, channel number: %d\n", syncip->fd,
      config.encode ? "encode" : "decode", config.max_channels);
  syncip->max_channels = config.max_channels;
  syncip->max_users = XLNXSYNC_IO;
  syncip->max_buffers = XLNXSYNC_BUF_PER_CHAN;
  syncip->max_cores = XLNXSYNC_MAX_CORES;
  syncip->channel_statuses = calloc (config.max_channels, sizeof (void *));
  if (!syncip->channel_statuses) {
    fprintf (stderr, "SyncIp: Memory allocation failed\n");
    return -1;
  }

  syncip->event_listeners = calloc (config.max_channels, sizeof (void *));
  if (!syncip->event_listeners) {
    fprintf (stderr, "SyncIp: Memory allocation failed\n");
    return -1;
  }

  ret = pthread_mutex_init (&(syncip->mutex), NULL);
  if (ret) {
    fprintf (stderr, "SyncIp: Couldn't intialize lock");
    return ret;
  }

  ret = pthread_create (&(syncip->polling_thread), NULL,
      &xvfbsync_syncip_polling_routine, t_info);
  if (ret) {
    fprintf (stderr, "Couldn't create thread");
    return ret;
  }

  return 0;
}

int
xvfbsync_syncip_depopulate (SyncIp * syncip)
{
  syncip->quit = true;
  pthread_join (syncip->polling_thread, NULL);
  pthread_mutex_destroy (&(syncip->mutex));
  if (syncip->channel_statuses)
    free (syncip->channel_statuses);

  if (syncip->event_listeners)
    free (syncip->event_listeners);

  return 0;
}

/*******************************/
/* Allegro address calculation */
/*******************************/

static const TFourCCMapping fourcc_mappings[] = {
  // planar: 8b
  FOURCC_MAPPING (FOURCC2 ('I', '4', '2', '0'), CHROMA_4_2_0, 8,
      FB_RASTER, C_ORDER_U_V, false, false)
      , FOURCC_MAPPING (FOURCC2 ('I', 'Y', 'U', 'V'), CHROMA_4_2_0, 8,
      FB_RASTER, C_ORDER_U_V, false, false)
      , FOURCC_MAPPING (FOURCC2 ('Y', 'V', '1', '2'), CHROMA_4_2_0, 8,
      FB_RASTER, C_ORDER_V_U, false, false)
      , FOURCC_MAPPING (FOURCC2 ('I', '4', '2', '2'), CHROMA_4_2_2, 8,
      FB_RASTER, C_ORDER_U_V, false, false)
      , FOURCC_MAPPING (FOURCC2 ('Y', 'V', '1', '6'), CHROMA_4_2_2, 8,
      FB_RASTER, C_ORDER_U_V, false, false)
      // planar: 10b
      , FOURCC_MAPPING (FOURCC2 ('I', '0', 'A', 'L'), CHROMA_4_2_0, 10,
      FB_RASTER, C_ORDER_U_V, false, false)
      , FOURCC_MAPPING (FOURCC2 ('I', '2', 'A', 'L'), CHROMA_4_2_2, 10,
      FB_RASTER, C_ORDER_U_V, false, false)
      // semi-planar: 8b
      , FOURCC_MAPPING (FOURCC2 ('N', 'V', '1', '2'), CHROMA_4_2_0, 8,
      FB_RASTER, C_ORDER_SEMIPLANAR, false, false)
      , FOURCC_MAPPING (FOURCC2 ('N', 'V', '1', '6'), CHROMA_4_2_2, 8,
      FB_RASTER, C_ORDER_SEMIPLANAR, false, false)
      // semi-planar: 10b
      , FOURCC_MAPPING (FOURCC2 ('P', '0', '1', '0'), CHROMA_4_2_0, 10,
      FB_RASTER, C_ORDER_SEMIPLANAR, false, false)
      , FOURCC_MAPPING (FOURCC2 ('P', '2', '1', '0'), CHROMA_4_2_2, 10,
      FB_RASTER, C_ORDER_SEMIPLANAR, false, false)
      // monochrome
      , FOURCC_MAPPING (FOURCC2 ('Y', '8', '0', '0'), CHROMA_4_0_0, 8,
      FB_RASTER, C_ORDER_NO_CHROMA, false, false)
      , FOURCC_MAPPING (FOURCC2 ('Y', '0', '1', '0'), CHROMA_4_0_0, 10,
      FB_RASTER, C_ORDER_NO_CHROMA, false, false)
      // tile : 64x4
      , FOURCC_MAPPING (FOURCC2 ('T', '6', '0', '8'), CHROMA_4_2_0, 8,
      FB_TILE_64x4, C_ORDER_SEMIPLANAR, false, false)
      , FOURCC_MAPPING (FOURCC2 ('T', '6', '2', '8'), CHROMA_4_2_2, 8,
      FB_TILE_64x4, C_ORDER_SEMIPLANAR, false, false)
      , FOURCC_MAPPING (FOURCC2 ('T', '6', 'm', '8'), CHROMA_4_0_0, 8,
      FB_TILE_64x4, C_ORDER_NO_CHROMA, false, false)
      , FOURCC_MAPPING (FOURCC2 ('T', '6', '0', 'A'), CHROMA_4_2_0, 10,
      FB_TILE_64x4, C_ORDER_SEMIPLANAR, false, false)
      , FOURCC_MAPPING (FOURCC2 ('T', '6', '2', 'A'), CHROMA_4_2_2, 10,
      FB_TILE_64x4, C_ORDER_SEMIPLANAR, false, false)
      , FOURCC_MAPPING (FOURCC2 ('T', '6', 'm', 'A'), CHROMA_4_0_0, 10,
      FB_TILE_64x4, C_ORDER_NO_CHROMA, false, false)
      // tile : 32x4
      , FOURCC_MAPPING (FOURCC2 ('T', '5', '0', '8'), CHROMA_4_2_0, 8,
      FB_TILE_32x4, C_ORDER_SEMIPLANAR, false, false)
      , FOURCC_MAPPING (FOURCC2 ('T', '5', '2', '8'), CHROMA_4_2_2, 8,
      FB_TILE_32x4, C_ORDER_SEMIPLANAR, false, false)
      , FOURCC_MAPPING (FOURCC2 ('T', '5', 'm', '8'), CHROMA_4_0_0, 8,
      FB_TILE_32x4, C_ORDER_NO_CHROMA, false, false)
      , FOURCC_MAPPING (FOURCC2 ('T', '5', '0', 'A'), CHROMA_4_2_0, 10,
      FB_TILE_32x4, C_ORDER_SEMIPLANAR, false, false)
      , FOURCC_MAPPING (FOURCC2 ('T', '5', '2', 'A'), CHROMA_4_2_2, 10,
      FB_TILE_32x4, C_ORDER_SEMIPLANAR, false, false)
      , FOURCC_MAPPING (FOURCC2 ('T', '5', 'm', 'A'), CHROMA_4_0_0, 10,
      FB_TILE_32x4, C_ORDER_NO_CHROMA, false, false)
      // 10b packed
      , FOURCC_MAPPING (FOURCC2 ('X', 'V', '1', '0'), CHROMA_4_0_0, 10,
      FB_RASTER, C_ORDER_NO_CHROMA, false, true)
      , FOURCC_MAPPING (FOURCC2 ('X', 'V', '1', '5'), CHROMA_4_2_0, 10,
      FB_RASTER, C_ORDER_SEMIPLANAR, false, true)
      , FOURCC_MAPPING (FOURCC2 ('X', 'V', '2', '0'), CHROMA_4_2_2, 10,
      FB_RASTER, C_ORDER_SEMIPLANAR, false, true)
};

static const int fourcc_mapping_size =
    sizeof (fourcc_mappings) / sizeof (fourcc_mappings[0]);

static bool
get_pic_format (u32 t_fourcc, TPicFormat * t_pic_format)
{
  const TFourCCMapping *p_begin_mapping = &fourcc_mappings[0];
  const TFourCCMapping *p_end_mapping = p_begin_mapping + fourcc_mapping_size;

  for (const TFourCCMapping * p_mapping = p_begin_mapping;
      p_mapping != p_end_mapping; p_mapping++) {
    if (p_mapping->t_fourcc == t_fourcc) {
      *t_pic_format = p_mapping->t_pict_format;
      return true;
    }
  }

  return false;
}

static bool
is_tiled (u32 t_fourcc)
{
  TPicFormat t_pic_format;
  return get_pic_format (t_fourcc, &t_pic_format)
      && (t_pic_format.e_storage_mode != FB_RASTER);
}

static EChromaMode
get_chroma_mode (u32 t_fourcc)
{
  TPicFormat t_pic_format;
  return get_pic_format (t_fourcc, &t_pic_format)
      ? t_pic_format.e_chroma_mode : (EChromaMode) - 1;
}

static bool
is_semi_planar (u32 t_fourcc)
{
  TPicFormat t_pic_format;
  return get_pic_format (t_fourcc, &t_pic_format)
      && (t_pic_format.e_chroma_order == C_ORDER_SEMIPLANAR);
}

static bool
is_monochrome (u32 t_fourcc)
{
  return get_chroma_mode (t_fourcc) == CHROMA_MONO;
}

static bool
is_10bit_packed (u32 t_fourcc)
{
  TPicFormat t_pic_format;
  return get_pic_format (t_fourcc, &t_pic_format) && t_pic_format.b_10b_packed;
}

static u8
get_bit_depth (u32 t_fourcc)
{
  TPicFormat t_pic_format;
  return get_pic_format (t_fourcc, &t_pic_format)
      ? t_pic_format.u_bit_depth : -1;
}

static int
get_pixel_size (u32 t_fourcc)
{
  return (get_bit_depth (t_fourcc) > 8) ? sizeof (uint16_t) : sizeof (uint8_t);
}

static int
get_luma_size (XLNXLLBuf * buf)
{
  if (is_tiled (buf->t_fourcc))
    return buf->t_planes[PLANE_Y].i_pitch * buf->t_dim.i_height / 4;
  return buf->t_planes[PLANE_Y].i_pitch * buf->t_dim.i_height;
}

static int
get_chroma_size (XLNXLLBuf * buf)
{
  EChromaMode e_cmode;
  int i_height_c;

  e_cmode = get_chroma_mode (buf->t_fourcc);

  if (e_cmode == CHROMA_MONO)
    return 0;

  i_height_c =
      (e_cmode == CHROMA_4_2_0) ? buf->t_dim.i_height / 2 : buf->t_dim.i_height;

  if (is_tiled (buf->t_fourcc))
    return buf->t_planes[PLANE_UV].i_pitch * i_height_c / 4;

  if (is_semi_planar (buf->t_fourcc))
    return buf->t_planes[PLANE_UV].i_pitch * i_height_c;

  return buf->t_planes[PLANE_UV].i_pitch * i_height_c * 2;
}

static int
get_offset_uv (XLNXLLBuf * buf)
{
  assert (buf->t_planes[PLANE_Y].i_pitch * buf->t_dim.i_height <=
      buf->t_planes[PLANE_UV].i_offset || (is_tiled (buf->t_fourcc)
          && (buf->t_planes[PLANE_Y].i_pitch * buf->t_dim.i_height / 4 <=
              buf->t_planes[PLANE_UV].i_offset)));

  return buf->t_planes[PLANE_UV].i_offset;
}

static void
print_framebuffer_config (struct xlnxsync_chan_config *config, u8 max_users,
    u8 max_cores)
{
  fprintf (stdout, "********************************\n");
  fprintf (stdout, "channel_id:%d\n", config->channel_id);
  fprintf (stdout, "luma_margin:%d\n", config->luma_margin);
  fprintf (stdout, "chroma_margin:%d\n", config->chroma_margin);

  for (u8 user = 0; user < max_users; ++user) {
    fprintf (stdout, "%s[%d]:\n",
        (user == XLNXSYNC_PROD) ? "prod" : (user ==
            XLNXSYNC_CONS) ? "cons" : "unknown", user);
    fprintf (stdout, "\t-fb_id:%d %s\n", config->fb_id[user],
        config->fb_id[user] == XLNXSYNC_AUTO_SEARCH ? "(auto_search)" : "");
    fprintf (stdout, "\t-ismono:%s\n",
        (config->ismono[user] == 0) ? "false" : "true");
    fprintf (stdout, "\t-luma_start_offset:%" PRIx64 "\n",
        config->luma_start_offset[user]);
    fprintf (stdout, "\t-luma_end_offset:%" PRIx64 "\n",
        config->luma_end_offset[user]);
    fprintf (stdout, "\t-chroma_start_offset:%" PRIx64 "\n",
        config->chroma_start_offset[user]);
    fprintf (stdout, "\t-chroma_end_offset:%" PRIx64 "\n",
        config->chroma_end_offset[user]);
  }

  for (int core = 0; core < max_cores; ++core) {
    fprintf (stdout, "core[%i]:\n", core);
    fprintf (stdout, "\t-luma_core_offset:%d\n",
        config->luma_core_offset[core]);
    fprintf (stdout, "\t-chroma_core_offset:%d\n",
        config->chroma_core_offset[core]);
  }

  fprintf (stdout, "********************************\n");
}

static struct xlnxsync_chan_config
set_enc_framebuffer_config (u8 channel_id, XLNXLLBuf * buf,
    u32 hardware_horizontal_stride_alignment,
    u32 hardware_vertical_stride_alignment)
{
  struct xlnxsync_chan_config config;
  int src_row_size;
  int i_hardware_pitch, i_hardware_luma_vertical_pitch;
  int i_vertical_factor, i_hardware_chroma_vertical_pitch;

  src_row_size = is_10bit_packed (buf->t_fourcc) ?
      ((buf->t_dim.i_width + 2) / 3 * 4) :
      buf->t_dim.i_width * get_pixel_size (buf->t_fourcc);
  config.dma_fd = buf->dma_fd;

  config.luma_start_offset[XLNXSYNC_PROD] = buf->t_planes[PLANE_Y].i_offset;
  config.luma_end_offset[XLNXSYNC_PROD] =
      config.luma_start_offset[XLNXSYNC_PROD] +
      get_luma_size (buf) - buf->t_planes[PLANE_Y].i_pitch + src_row_size - 1;

  config.luma_start_offset[XLNXSYNC_CONS] = buf->t_planes[PLANE_Y].i_offset;
  /*           <------------> stride
   *           <--------> width
   * height   ^
   *          |
   *          |
   *          v         x last pixel of the image
   * end = (height - 1) * stride + width - 1 (to get the last pixel of the image)
   * total_size = height * stride
   * end = total_size - stride + width - 1
   */
  i_hardware_pitch = buf->t_planes[PLANE_Y].i_pitch;
  i_hardware_luma_vertical_pitch =
      buf->t_planes[PLANE_UV].i_offset / i_hardware_pitch;
  config.luma_end_offset[XLNXSYNC_CONS] =
      config.luma_start_offset[XLNXSYNC_CONS] +
      (i_hardware_pitch * (i_hardware_luma_vertical_pitch - 1)) +
      i_hardware_pitch - 1;

  /* chroma is the same, but the width depends on the format of the yuv
   * here we make the assumption that the fourcc is semi planar */
  if (!is_monochrome (buf->t_fourcc)) {
    assert (is_semi_planar (buf->t_fourcc));
    config.chroma_start_offset[XLNXSYNC_PROD] = get_offset_uv (buf);
    config.chroma_end_offset[XLNXSYNC_PROD] =
        config.chroma_start_offset[XLNXSYNC_PROD] +
        get_chroma_size (buf) - buf->t_planes[PLANE_UV].i_pitch + src_row_size -
        1;
    config.chroma_start_offset[XLNXSYNC_CONS] =
        buf->t_planes[PLANE_UV].i_offset;
    i_vertical_factor =
        (get_chroma_mode (buf->t_fourcc) == CHROMA_4_2_0) ? 2 : 1;
    i_hardware_chroma_vertical_pitch =
        i_hardware_luma_vertical_pitch / i_vertical_factor;
    config.chroma_end_offset[XLNXSYNC_CONS] =
        config.chroma_start_offset[XLNXSYNC_CONS] +
        (i_hardware_pitch * (i_hardware_chroma_vertical_pitch - 1)) +
        buf->t_planes[PLANE_UV].i_pitch - 1;
  } else {
    for (int user = 0; user < XLNXSYNC_IO; user++) {
      config.chroma_start_offset[user] = 0;
      config.chroma_end_offset[user] = 0;
      config.ismono[user] = 1;
    }
  }

  for (int core = 0; core < XLNXSYNC_MAX_CORES; core++) {
    config.luma_core_offset[core] = 0;
    config.chroma_core_offset[core] = 0;
  }

  /* no margin for now (only needed for the decoder) */
  config.luma_margin = 0;
  config.chroma_margin = 0;

  config.fb_id[XLNXSYNC_PROD] = XLNXSYNC_AUTO_SEARCH;
  config.fb_id[XLNXSYNC_CONS] = XLNXSYNC_AUTO_SEARCH;
  config.channel_id = channel_id;

  return config;
}

static struct xlnxsync_chan_config
set_dec_framebuffer_config (u8 channel_id, XLNXLLBuf * buf)
{
  struct xlnxsync_chan_config config;
  int src_row_size = is_10bit_packed (buf->t_fourcc) ?
      ((buf->t_dim.i_width + 2) / 3 * 4) :
      buf->t_dim.i_width * get_pixel_size (buf->t_fourcc);

  config.dma_fd = buf->dma_fd;

  config.luma_start_offset[XLNXSYNC_PROD] = buf->t_planes[PLANE_Y].i_offset;

  /*           <------------> stride
   *           <--------> width
   * height   ^
   *          |
   *          |
   *          v         x last pixel of the image
   * end = (height - 1) * stride + width - 1 (to get the last pixel of the image)
   * total_size = height * stride
   * end = total_size - stride + width - 1
   */
  // TODO : This should be LCU and 64 aligned
  config.luma_end_offset[XLNXSYNC_PROD] =
      config.luma_start_offset[XLNXSYNC_PROD] +
      get_luma_size (buf) - buf->t_planes[PLANE_Y].i_pitch + src_row_size - 1;

  config.luma_start_offset[XLNXSYNC_CONS] = buf->t_planes[PLANE_Y].i_offset;
  config.luma_end_offset[XLNXSYNC_CONS] =
      config.luma_start_offset[XLNXSYNC_CONS] +
      get_luma_size (buf) - buf->t_planes[PLANE_Y].i_pitch + src_row_size - 1;

  /* chroma is the same, but the width depends on the format of the yuv
   * here we make the assumption that the fourcc is semi planar */
  if (!is_monochrome (buf->t_fourcc)) {
    assert (is_semi_planar (buf->t_fourcc));
    config.chroma_start_offset[XLNXSYNC_PROD] = get_offset_uv (buf);
    // TODO : This should be LCU and 64 aligned
    config.chroma_end_offset[XLNXSYNC_PROD] =
        config.chroma_start_offset[XLNXSYNC_PROD] +
        get_chroma_size (buf) - buf->t_planes[PLANE_UV].i_pitch + src_row_size -
        1;
    config.chroma_start_offset[XLNXSYNC_CONS] = get_offset_uv (buf);
    config.chroma_end_offset[XLNXSYNC_CONS] =
        config.chroma_start_offset[XLNXSYNC_CONS] +
        get_chroma_size (buf) - buf->t_planes[PLANE_UV].i_pitch + src_row_size -
        1;
  } else {
    for (int user = 0; user < XLNXSYNC_IO; user++) {
      config.chroma_start_offset[user] = 0;
      config.chroma_end_offset[user] = 0;
      config.ismono[user] = 1;
    }
  }

  for (int core = 0; core < XLNXSYNC_MAX_CORES; core++) {
    config.luma_core_offset[core] = 0;
    config.chroma_core_offset[core] = 0;
  }

  /* no margin for now (only needed for the decoder) */
  config.luma_margin = 0;
  config.chroma_margin = 0;

  config.fb_id[XLNXSYNC_PROD] = XLNXSYNC_AUTO_SEARCH;
  config.fb_id[XLNXSYNC_CONS] = XLNXSYNC_AUTO_SEARCH;
  config.channel_id = channel_id;

  return config;
}

/******************************/
/* xvfbsync sync_chan helpers */
/******************************/

static void
xvfbsync_sync_chan_listener (ChannelStatus * status)
{
  fprintf (stderr, "watchdog: %d, sync: %d, ldiff: %d, cdiff: %d\n",
      status->watchdog_error, status->sync_error, status->luma_diff_error,
      status->chroma_diff_error);
}

static int
xvfbsync_sync_chan_disable (SyncChannel * sync_chan)
{
  int ret = 0;

  if (!sync_chan->enabled)
    assert (0 == "Tried to disable a channel twice");

  sync_chan->sync->quit = true;
  ret = xvfbsync_syncip_disable_channel (sync_chan->sync, sync_chan->id);
  sync_chan->enabled = false;
  if (logs_enabled)
    fprintf (stdout, "Disable channel %d\n", sync_chan->id);

  return ret;
}

/**********************/
/* xvfbsync sync_chan */
/**********************/

static int
xvfbsync_sync_chan_populate (SyncChannel * sync_chan, SyncIp * syncip, u8 id)
{
  sync_chan->sync = syncip;
  sync_chan->id = id;
  sync_chan->enabled = false;
  xvfbsync_syncip_add_listener (syncip, id, &xvfbsync_sync_chan_listener);
  return 0;
}

static int
xvfbsync_sync_chan_depopulate (SyncChannel * sync_chan)
{
  int ret = 0;

  if (sync_chan->enabled)
    ret = xvfbsync_sync_chan_disable (sync_chan);

  xvfbsync_syncip_remove_listener (sync_chan->sync, sync_chan->id);
  return ret;
}

/**************************/
/* xvfbsync dec_sync_chan */
/**************************/

int
xvfbsync_dec_sync_chan_add_buffer (DecSyncChannel * dec_sync_chan,
    XLNXLLBuf * buf)
{
  int ret = 0;
  struct xlnxsync_chan_config config;

  config = set_dec_framebuffer_config (dec_sync_chan->sync_channel.id, buf);
  ret = xvfbsync_syncip_add_buffer (dec_sync_chan->sync_channel.sync, &config);
  if (buf)
    free (buf);

  if (!ret && logs_enabled)
    fprintf (stdout, "Decoder: Pushed buffer in sync ip\n");

  return ret;
}

int
xvfbsync_dec_sync_chan_enable (DecSyncChannel * dec_sync_chan)
{
  int ret = 0;

  ret = xvfbsync_syncip_enable_channel (dec_sync_chan->sync_channel.sync,
      dec_sync_chan->sync_channel.id);
  if (logs_enabled)
    fprintf (stdout, "Decoder: Enable channel %d\n",
        dec_sync_chan->sync_channel.id);

  dec_sync_chan->sync_channel.enabled = true;
  return ret;
}

int
xvfbsync_dec_sync_chan_populate (DecSyncChannel * dec_sync_chan,
    SyncIp * syncip, u8 id)
{
  return xvfbsync_sync_chan_populate (&(dec_sync_chan->sync_channel), syncip,
      id);
}

int
xvfbsync_dec_sync_chan_depopulate (DecSyncChannel * dec_sync_chan)
{
  return xvfbsync_sync_chan_depopulate (&(dec_sync_chan->sync_channel));
}

/**********************************/
/* xvfbsync enc_sync_chan helpers */
/**********************************/

static int
xvfbsync_enc_sync_chan_add_buffer_ (EncSyncChannel * enc_sync_chan,
    XLNXLLBuf * buf, int num_fb_to_enable)
{
  int ret = 0;
  struct xlnxsync_chan_config config;

  if (!enc_sync_chan->is_running) {
    if (buf)
      ret = xvfbsync_queue_push (&enc_sync_chan->buffers, buf);

    if (ret)
      return ret;
  } else {
    if (buf)
      ret = xvfbsync_queue_push (&enc_sync_chan->buffers, buf);

    if (ret)
      return ret;

    while (enc_sync_chan->is_running && num_fb_to_enable > 0
        && !xvfbsync_queue_empty (&enc_sync_chan->buffers)) {
      buf = xvfbsync_queue_front (&enc_sync_chan->buffers);

      config =
          set_enc_framebuffer_config (enc_sync_chan->sync_channel.id, buf,
          enc_sync_chan->hardware_horizontal_stride_alignment,
          enc_sync_chan->hardware_vertical_stride_alignment);

      if (logs_enabled)
        print_framebuffer_config (&config,
            enc_sync_chan->sync_channel.sync->max_users,
            enc_sync_chan->sync_channel.sync->max_cores);

      ret =
          xvfbsync_syncip_add_buffer (enc_sync_chan->sync_channel.sync,
          &config);

      if (!ret) {
        if (logs_enabled)
          fprintf (stdout, "Pushed buffer in sync ip\n");

        if (buf)
          free (buf);
        xvfbsync_queue_pop (&enc_sync_chan->buffers);
      }
      --num_fb_to_enable;
    }
  }

  return ret;
}

/**************************/
/* xvfbsync enc_sync_chan */
/**************************/

int
xvfbsync_enc_sync_chan_add_buffer (EncSyncChannel * enc_sync_chan,
    XLNXLLBuf * buf)
{
  int ret = 0;

  pthread_mutex_lock (&enc_sync_chan->mutex);
  ret = xvfbsync_enc_sync_chan_add_buffer_ (enc_sync_chan, buf, 1);
  pthread_mutex_unlock (&enc_sync_chan->mutex);

  return ret;
}

int
xvfbsync_enc_sync_chan_enable (EncSyncChannel * enc_sync_chan)
{
  int num_fb_to_enable;
  int ret = 0;

  pthread_mutex_lock (&enc_sync_chan->mutex);
  enc_sync_chan->is_running = true;
  num_fb_to_enable = MIN ((int) enc_sync_chan->buffers.size,
      enc_sync_chan->sync_channel.sync->max_buffers);
  ret = xvfbsync_syncip_enable_channel (enc_sync_chan->sync_channel.sync,
      enc_sync_chan->sync_channel.id);
  if (ret)
    return ret;

  ret =
      xvfbsync_enc_sync_chan_add_buffer_ (enc_sync_chan, NULL,
      num_fb_to_enable);
  enc_sync_chan->sync_channel.enabled = true;
  if (logs_enabled)
    fprintf (stdout, "Encoder: Enable channel %d\n",
        enc_sync_chan->sync_channel.id);
  pthread_mutex_unlock (&enc_sync_chan->mutex);

  return ret;
}

int
xvfbsync_enc_sync_chan_populate (EncSyncChannel * enc_sync_chan,
    SyncIp * syncip, u8 id, u32 hardware_horizontal_stride_alignment,
    u32 hardware_vertical_stride_alignment)
{
  int ret = 0;

  xvfbsync_sync_chan_populate (&(enc_sync_chan->sync_channel), syncip, id);
  enc_sync_chan->is_running = false;
  enc_sync_chan->hardware_horizontal_stride_alignment =
      hardware_horizontal_stride_alignment;
  enc_sync_chan->hardware_vertical_stride_alignment =
      hardware_vertical_stride_alignment;

  if (pthread_mutex_init (&(enc_sync_chan->mutex), NULL)) {
    fprintf (stderr, "Couldn't intialize lock");
    return -1;
  }

  ret = !xvfbsync_queue_init (&(enc_sync_chan->buffers));

  return ret;
}

int
xvfbsync_enc_sync_chan_depopulate (EncSyncChannel * enc_sync_chan)
{
  int ret = 0;

  ret = xvfbsync_sync_chan_depopulate (&enc_sync_chan->sync_channel);
  if (ret)
    return ret;

  while (!xvfbsync_queue_empty (&enc_sync_chan->buffers)) {
    XLNXLLBuf *buf = xvfbsync_queue_front (&enc_sync_chan->buffers);
    if (buf)
      free (buf);
    else
      return -1;

    ret = xvfbsync_queue_pop (&enc_sync_chan->buffers);
    if (ret)
      return ret;
  }

  return ret;
}

XLNXLLBuf *
xvfbsync_xlnxll_buf_new (void)
{
  XLNXLLBuf *buf;
  buf = (XLNXLLBuf *) calloc (1, sizeof (XLNXLLBuf));
  if (!buf)
    fprintf (stderr, "XLNXLLBuf Memory allocation failed\n");

  return buf;
}
