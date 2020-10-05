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
#include <gst/gst.h>
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

GST_DEBUG_CATEGORY (xvfbsync_debug);
#define GST_CAT_DEFAULT xvfbsync_debug


/**********************************/
/* xvfbsync private structs/enums */
/**********************************/

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
    GST_ERROR ("Queue: Queue is NULL");
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
    GST_ERROR ("Queue: Queue is NULL");
    return false;
  }

  return q->size == 0;
}

static XLNXLLBuf *
xvfbsync_queue_front (Queue * q)
{
  if (!q) {
    GST_ERROR ("Queue: Queue is NULL");
    return NULL;
  }

  return q->front->buf;
}

static bool
xvfbsync_queue_pop (Queue * q)
{
  struct Node *temp;

  if (!q || xvfbsync_queue_empty (q)) {
    GST_ERROR ("Queue: Queue is NULL or empty");
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
    GST_ERROR ("Queue: Queue is NULL");
    return false;
  }

  q->size++;

  if (q->front == NULL) {
    q->front = (struct Node *) calloc (1, sizeof (struct Node));
    if (!q->front) {
      GST_ERROR ("Queue: Memory allocation failed");
      return false;
    }

    q->front->buf = buf_ptr;
    q->front->next = NULL;
    q->last = q->front;
  } else {
    q->last->next = (struct Node *) calloc (1, sizeof (struct Node));
    if (!q->last->next) {
      GST_ERROR ("Queue: Memory allocation failed");
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
    ChannelStatus * channel_status, u8 max_users, u8 max_buffers)
{
  for (u8 buffer = 0; buffer < max_buffers; ++buffer) {
    for (u8 user = 0; user < max_users; ++user)
      channel_status->fb_avail[buffer][user] = status->fbdone[buffer][user];
  }
  channel_status->enable = status->enable;
  channel_status->err.prod_sync = status->prod_sync_err;
  channel_status->err.prod_wdg = status->prod_wdg_err;
  channel_status->err.cons_sync = status->cons_sync_err;
  channel_status->err.cons_wdg = status->cons_wdg_err;
  channel_status->err.ldiff = status->ldiff_err;
  channel_status->err.cdiff = status->cdiff_err;

  GST_INFO
      ("prod_wdog: %d, prod_sync: %d, cons_wdog: %d, cons_sync: %d, ldiff: %d, cdiff: %d",
      channel_status->err.prod_wdg, channel_status->err.prod_sync,
      channel_status->err.cons_wdg, channel_status->err.cons_sync,
      channel_status->err.ldiff, channel_status->err.cdiff);

}

static int
xvfbsync_syncip_get_latest_chan_status (SyncChannel * sync_chan)
{
  struct xlnxsync_stat chan_status = { 0 };
  int ret = 0;

  chan_status.hdr_ver = XLNXSYNC_IOCTL_HDR_VER;
  ret = ioctl (sync_chan->sync->fd, XLNXSYNC_CHAN_GET_STATUS, &chan_status);

  if (ret)
    GST_ERROR ("SyncIp: Couldn't get sync ip channel status");
  else
    xvfbsync_syncip_parse_chan_status (&chan_status, sync_chan->channel_status,
        sync_chan->sync->max_users, sync_chan->sync->max_buffers);

  return ret;
}

static int
xvfbsync_syncip_reset_status (SyncIp * syncip)
{
  struct xlnxsync_clr_err clr = { 0 };
  int ret = 0;

  clr.hdr_ver = XLNXSYNC_IOCTL_HDR_VER;
  clr.prod_sync_err = 1;
  clr.prod_wdg_err = 1;
  clr.cons_sync_err = 1;
  clr.cons_wdg_err = 1;
  clr.ldiff_err = 1;
  clr.cdiff_err = 1;

  ret = ioctl (syncip->fd, XLNXSYNC_CHAN_CLR_ERR, &clr);

  return ret;
}

static int
xvfbsync_syncip_enable_channel (SyncIp * syncip)
{
  int ret = 0;

  ret = ioctl (syncip->fd, XLNXSYNC_CHAN_ENABLE);

  return ret;
}

static int
xvfbsync_syncip_disable_channel (SyncIp * syncip)
{
  int ret = 0;

  ret = ioctl (syncip->fd, XLNXSYNC_CHAN_DISABLE);

  return ret;
}

static int
xvfbsync_syncip_set_intr_mask (SyncIp * syncip, struct xlnxsync_intr *intr_mask)
{
  int ret = 0;

  intr_mask->hdr_ver = XLNXSYNC_IOCTL_HDR_VER;
  ret = ioctl (syncip->fd, XLNXSYNC_CHAN_SET_INTR_MASK, intr_mask);

  return ret;
}

static int
xvfbsync_syncip_add_buffer (SyncIp * syncip,
    struct xlnxsync_chan_config *fb_config)
{
  int ret = 0;

  fb_config->hdr_ver = XLNXSYNC_IOCTL_HDR_VER;
  ret = ioctl (syncip->fd, XLNXSYNC_CHAN_SET_CONFIG, fb_config);

  if (ret)
    GST_ERROR ("SyncIp: Couldn't add buffer");

  return ret;
}

static void
xvfbsync_syncip_poll_errors (SyncChannel * sync_channel, int timeout)
{
  EPollError ret_code;
  struct pollfd poll_data;
  int ret = 0;
  ChannelStatus *status;

  poll_data.events = POLLPRI;
  poll_data.fd = (int) (intptr_t) sync_channel->sync->fd;

  ret_code = poll (&poll_data, 1, timeout);

  if (ret_code == POLL_TIMEOUT)
    return;

  xvfbsync_syncip_get_latest_chan_status (sync_channel);
  status = sync_channel->channel_status;

  if (status->err.prod_sync || status->err.prod_wdg
      || status->err.cons_sync || status->err.cons_wdg
      || status->err.ldiff || status->err.cdiff) {
    GST_ERROR
        ("prod_wdog: %d, prod_sync: %d, cons_wdog: %d, cons_sync: %d, ldiff: %d, cdiff: %d",
        status->err.prod_wdg, status->err.prod_sync,
        status->err.cons_wdg, status->err.cons_sync,
        status->err.ldiff, status->err.cdiff);
    ret = xvfbsync_syncip_reset_status (sync_channel->sync);
    if (ret)
      GST_ERROR ("SyncIp: Couldnt reset status of channel %d",
          sync_channel->id);
  }

}

static void *
xvfbsync_syncip_polling_routine (void *arg)
{
  SyncChannel *sync_channel = ((ThreadInfo *) arg)->sync_channel;
  while (true) {
    if (sync_channel->quit) {
      break;
    }
    xvfbsync_syncip_poll_errors (sync_channel, 500);
  }


  xvfbsync_syncip_poll_errors (sync_channel, 0);
  if (arg)
    free ((ThreadInfo *) arg);

  return NULL;
}

/*******************/
/* xvfbsync syncIP */
/*******************/

int
xvfbsync_syncip_chan_populate (SyncIp * syncip, SyncChannel * sync_channel,
    u32 fd)
{
  ThreadInfo *t_info;
  struct xlnxsync_config config = { 0 };
  int ret = 0;

  GST_DEBUG_CATEGORY_INIT (xvfbsync_debug, "xvfbsync", 0,
      "Xilinx Frame Synchronizer IP");

  t_info = calloc (1, sizeof (ThreadInfo));
  if (!t_info) {
    GST_ERROR ("SyncIp: Memory allocation failed");
    return -1;
  }

  t_info->sync_channel = sync_channel;
  syncip->fd = fd;
  sync_channel->quit = false;
  config.hdr_ver = XLNXSYNC_IOCTL_HDR_VER;
  ret = ioctl (syncip->fd, XLNXSYNC_GET_CFG, &config);
  if (ret) {
    GST_ERROR ("SyncIp: Couldn't get sync ip configuration");
    return ret;
  }

  GST_DEBUG ("[fd: %d] mode: %s, max channel number: %d, active channel %d",
      fd, config.encode ? "encode" : "decode", config.max_channels,
      config.active_channels);

  syncip->max_channels = config.max_channels;
  syncip->active_channels = config.active_channels;
  syncip->max_users = XLNXSYNC_IO;
  syncip->max_buffers = XLNXSYNC_BUF_PER_CHAN;
  syncip->max_cores = XLNXSYNC_MAX_CORES;
  sync_channel->sync = syncip;
  sync_channel->channel_status = calloc (1, sizeof (ChannelStatus));

  if (!sync_channel->channel_status) {
    GST_ERROR ("SyncIp: Memory allocation for channel status failed");
    return -1;
  }

  sync_channel->id = config.reserved_id;

  ret = pthread_create (&(sync_channel->polling_thread), NULL,
      &xvfbsync_syncip_polling_routine, t_info);
  if (ret) {
    GST_ERROR ("SyncIp: Couldn't create thread");
    return ret;
  }

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
  GST_TRACE ("************xvfbsync*************");
  GST_TRACE ("luma_margin:%d", config->luma_margin);
  GST_TRACE ("chroma_margin:%d", config->chroma_margin);

  for (u8 user = 0; user < max_users; ++user) {
    GST_TRACE ("%s[%d]:",
        (user == XLNXSYNC_PROD) ? "prod" : (user ==
            XLNXSYNC_CONS) ? "cons" : "unknown", user);
    GST_TRACE ("\t-fb_id:%d %s", config->fb_id[user],
        config->fb_id[user] == XLNXSYNC_AUTO_SEARCH ? "(auto_search)" : "");
    GST_TRACE ("\t-ismono:%s", (config->ismono[user] == 0) ? "false" : "true");
    GST_TRACE ("\t-luma_start_offset:%" PRIx64,
        config->luma_start_offset[user]);
    GST_TRACE ("\t-luma_end_offset:%" PRIx64, config->luma_end_offset[user]);
    GST_TRACE ("\t-chroma_start_offset:%" PRIx64,
        config->chroma_start_offset[user]);
    GST_TRACE ("\t-chroma_end_offset:%" PRIx64,
        config->chroma_end_offset[user]);
  }

  for (int core = 0; core < max_cores; ++core) {
    GST_TRACE ("core[%i]:", core);
    GST_TRACE ("\t-luma_core_offset:%d", config->luma_core_offset[core]);
    GST_TRACE ("\t-chroma_core_offset:%d", config->chroma_core_offset[core]);
  }

  GST_TRACE ("********************************");
}

static struct xlnxsync_chan_config
set_enc_framebuffer_config (XLNXLLBuf * buf,
    u32 hardware_horizontal_stride_alignment,
    u32 hardware_vertical_stride_alignment)
{
  struct xlnxsync_chan_config config = { 0 };
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
    config.ismono[XLNXSYNC_PROD] = 0;
    config.ismono[XLNXSYNC_CONS] = 0;
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

  return config;
}

static struct xlnxsync_chan_config
set_dec_framebuffer_config (XLNXLLBuf * buf)
{
  struct xlnxsync_chan_config config = { 0 };
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

  return config;
}

/******************************/
/* xvfbsync sync_chan helpers */
/******************************/

static int
xvfbsync_sync_chan_disable (SyncChannel * sync_chan)
{
  int ret = 0;

  if (!sync_chan->enabled)
    assert (0 == "Tried to disable a channel twice");

  ret = xvfbsync_syncip_disable_channel (sync_chan->sync);
  if (ret) {
    GST_ERROR ("SyncIp: Couldn't disable channel %d", sync_chan->id);
    goto err;
  }

  sync_chan->quit = true;
  sync_chan->enabled = false;
  GST_DEBUG ("Disable channel %d", sync_chan->id);

err:
  return ret;
}

/**********************/
/* xvfbsync sync_chan */
/**********************/

static int
xvfbsync_sync_chan_populate (SyncChannel * sync_chan, SyncIp * syncip)
{
  sync_chan->sync = syncip;
  sync_chan->enabled = false;
  return 0;
}

static int
xvfbsync_sync_chan_depopulate (SyncChannel * sync_chan)
{
  int ret = 0;

  if (sync_chan->enabled)
    ret = xvfbsync_sync_chan_disable (sync_chan);

  pthread_join (sync_chan->polling_thread, NULL);
  if (sync_chan->channel_status)
    free (sync_chan->channel_status);

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
  struct xlnxsync_chan_config config = { 0 };

  config = set_dec_framebuffer_config (buf);
  ret = xvfbsync_syncip_add_buffer (dec_sync_chan->sync_channel.sync, &config);
  if (buf)
    free (buf);

  if (!ret)
    GST_DEBUG ("Decoder: Pushed buffer in sync ip");

  return ret;
}

int
xvfbsync_dec_sync_chan_enable (DecSyncChannel * dec_sync_chan)
{
  int ret = 0;

  ret = xvfbsync_syncip_enable_channel (dec_sync_chan->sync_channel.sync);
  GST_DEBUG ("Decoder: Enable channel %d", dec_sync_chan->sync_channel.id);

  dec_sync_chan->sync_channel.enabled = true;
  return ret;
}

int
xvfbsync_dec_sync_chan_populate (DecSyncChannel * dec_sync_chan,
    SyncIp * syncip)
{
  return xvfbsync_sync_chan_populate (&(dec_sync_chan->sync_channel), syncip);
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
  struct xlnxsync_chan_config config = { 0 };

  if (!enc_sync_chan->sync_channel->enabled) {

    if (buf)
      ret = xvfbsync_queue_push (&enc_sync_chan->buffers, buf);

    if (ret)
      return ret;
  } else {
    if (buf)
      ret = xvfbsync_queue_push (&enc_sync_chan->buffers, buf);

    if (ret)
      return ret;

    while (enc_sync_chan->sync_channel->enabled && num_fb_to_enable > 0
        && !xvfbsync_queue_empty (&enc_sync_chan->buffers)) {
      buf = xvfbsync_queue_front (&enc_sync_chan->buffers);

      config =
          set_enc_framebuffer_config (buf,
          enc_sync_chan->hardware_horizontal_stride_alignment,
          enc_sync_chan->hardware_vertical_stride_alignment);

      print_framebuffer_config (&config,
          enc_sync_chan->sync_channel->sync->max_users,
          enc_sync_chan->sync_channel->sync->max_cores);

      ret =
          xvfbsync_syncip_add_buffer (enc_sync_chan->sync_channel->sync,
          &config);

      if (!ret) {
        GST_DEBUG ("Encoder: Pushed buffer in sync ip");

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

  ret = xvfbsync_enc_sync_chan_add_buffer_ (enc_sync_chan, buf, 1);

  return ret;
}

int
xvfbsync_enc_sync_chan_enable (EncSyncChannel * enc_sync_chan)
{
  int num_fb_to_enable;
  int ret = 0;

  num_fb_to_enable = MIN ((int) enc_sync_chan->buffers.size,
      enc_sync_chan->sync_channel->sync->max_buffers);
  ret = xvfbsync_syncip_enable_channel (enc_sync_chan->sync_channel->sync);
  if (ret) {
    GST_ERROR ("SyncIp: Couldn't enable channel %d",
        enc_sync_chan->sync_channel->id);
    return ret;
  }

  GST_DEBUG ("Encoder: Enable channel %d", enc_sync_chan->sync_channel->id);
  enc_sync_chan->sync_channel->enabled = true;
  ret =
      xvfbsync_enc_sync_chan_add_buffer_ (enc_sync_chan, NULL,
      num_fb_to_enable);

  return ret;
}


int
xvfbsync_enc_sync_chan_set_intr_mask (EncSyncChannel * enc_sync_chan,
    ChannelIntr * intr)
{
  int ret = 0;
  struct xlnxsync_intr intr_mask = { 0 };

  intr_mask.prod_lfbdone = intr->prod_lfbdone;
  intr_mask.prod_cfbdone = intr->prod_cfbdone;
  intr_mask.cons_lfbdone = intr->cons_lfbdone;
  intr_mask.cons_cfbdone = intr->cons_cfbdone;
  intr_mask.err.prod_sync = intr->err.prod_sync;
  intr_mask.err.cons_sync = intr->err.cons_sync;
  intr_mask.err.prod_wdg = intr->err.prod_wdg;
  intr_mask.err.cons_wdg = intr->err.cons_wdg;
  intr_mask.err.ldiff = intr->err.ldiff;
  intr_mask.err.cdiff = intr->err.cdiff;

  ret =
      xvfbsync_syncip_set_intr_mask (enc_sync_chan->sync_channel->sync,
      &intr_mask);
  if (ret)
    GST_ERROR ("SyncIp: Couldn't set interrupt mask for channel %d",
        enc_sync_chan->sync_channel->id);
  return ret;
}

int
xvfbsync_enc_sync_chan_populate (EncSyncChannel * enc_sync_chan,
    SyncChannel * sync_chan, u32 hardware_horizontal_stride_alignment,
    u32 hardware_vertical_stride_alignment)
{
  int ret = 0;

  enc_sync_chan->hardware_horizontal_stride_alignment =
      hardware_horizontal_stride_alignment;
  enc_sync_chan->hardware_vertical_stride_alignment =
      hardware_vertical_stride_alignment;
  enc_sync_chan->sync_channel = sync_chan;

  ret = !xvfbsync_queue_init (&(enc_sync_chan->buffers));

  return ret;
}

int
xvfbsync_enc_sync_chan_depopulate (EncSyncChannel * enc_sync_chan)
{
  int ret = 0;

  ret = xvfbsync_sync_chan_depopulate (enc_sync_chan->sync_channel);
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
    GST_ERROR ("XLNXLLBuf: Memory allocation failed");

  return buf;
}
