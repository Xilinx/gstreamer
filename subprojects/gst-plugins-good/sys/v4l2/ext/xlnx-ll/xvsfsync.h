/******************************************************************************
*
* Copyright (C) 2019 Allegro DVT2.  All rights reserved.
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
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX OR ALLEGRO DVT2 BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of  Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
*
* Except as contained in this notice, the name of Allegro DVT2 shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Allegro DVT2.
*
******************************************************************************/

#ifndef __XVCUSYNCLL2_H__
#define __XVCUSYNCLL2_H__

/*
 * This is set in the fb_id or channel_id of struct xvsfsync_chan_config when
 * configuring the channel. This makes the driver auto search for the free
 * framebuffer or channel slot.
 */
#define XVSFSYNC_AUTO_SEARCH 0xFF

#define XVSFSYNC_MAX_ENC_CHANNEL 4
#define XVSFSYNC_MAX_DEC_CHANNEL 2
#define XVSFSYNC_BUF_PER_CHANNEL 3

#define XVSFSYNC_PROD 0
#define XVSFSYNC_CONS 1
#define XVSFSYNC_IO 2

#define XVSFSYNC_MAX_CORES 4

/**
 * struct xvsfsync_chan_config - Synchronizer channel configuration struct
 * @luma_start_offset: Start offset of Luma buffer
 * @chroma_start_offset: Start offset of Chroma buffer
 * @luma_end_offset: End offset of Luma buffer
 * @chroma_end_offset: End offset of Chroma buffer
 * @luma_margin: Margin for Luma buffer
 * @chroma_margin: Margin for Chroma buffer
 * @luma_core_offset: Array of 4 offsets for luma
 * @chroma_core_offset: Array of 4 offsets for chroma
 * @dma_fd: File descriptor of dma
 * @fb_id: Framebuffer index. Valid values 0/1/2/XVSFSYNC_AUTO_SEARCH
 * @ismono: Flag to indicate if buffer is Luma only.
 * @channel_id: Channel index to be configured.
 * Valid 0..3 & XVSFSYNC_AUTO_SEARCH
 *
 * This structure contains the configuration for monitoring a particular
 * framebuffer on a particular channel.
 */
struct xvsfsync_chan_config
{
  u64 luma_start_offset[XVSFSYNC_IO];
  u64 chroma_start_offset[XVSFSYNC_IO];
  u64 luma_end_offset[XVSFSYNC_IO];
  u64 chroma_end_offset[XVSFSYNC_IO];
  u32 luma_margin;
  u32 chroma_margin;
  u32 luma_core_offset[XVSFSYNC_MAX_CORES];
  u32 chroma_core_offset[XVSFSYNC_MAX_CORES];
  u32 dma_fd;
  u8 fb_id[XVSFSYNC_IO];
  u8 ismono[XVSFSYNC_IO];
  u8 channel_id;
};

/**
 * struct xvsfsync_clr_err - Clear channel error
 * @channel_id: Channel id whose error needs to be cleared
 * @sync_err: Set this to clear sync error
 * @wdg_err: Set this to clear watchdog error
 * @ldiff_err: Set this to clear luma difference error
 * @cdiff_err: Set this to clear chroma difference error
 */
struct xvsfsync_clr_err
{
  u8 channel_id;
  u8 sync_err;
  u8 wdg_err;
  u8 ldiff_err;
  u8 cdiff_err;
};

/** struct xvsfsync_fbdone - Framebuffer Done
 * @fbdone: Framebuffer Done status
 */
struct xvsfsync_fbdone
{
  u8 status[XVSFSYNC_MAX_ENC_CHANNEL][XVSFSYNC_BUF_PER_CHANNEL][XVSFSYNC_IO];
};

/**
 * struct xvsfsync_config - Synchronizer IP configuration
 * @encode: true if encoder type, false for decoder type
 * @max_channels: Maximum channels this IP supports
 */
struct xvsfsync_config
{
  bool encode;
  u8 max_channels;
};

/**
 * struct xvsfsync_stat - Sync IP status
 * @fbdone: for every pair of luma/chroma buffer for every producer/consumer
 * @enable: channel enable
 * @sync_err: Synchronization error
 * @wdg_err: Watchdog error
 * @ldiff_err: Luma difference > 1 for channel
 * @cdiff_err: Chroma difference > 1 for channel
 */
struct xvsfsync_stat
{
  u8 fbdone[XVSFSYNC_MAX_ENC_CHANNEL][XVSFSYNC_BUF_PER_CHANNEL][XVSFSYNC_IO];
  u8 enable[XVSFSYNC_MAX_ENC_CHANNEL];
  u8 sync_err[XVSFSYNC_MAX_ENC_CHANNEL];
  u8 wdg_err[XVSFSYNC_MAX_ENC_CHANNEL];
  u8 ldiff_err[XVSFSYNC_MAX_ENC_CHANNEL];
  u8 cdiff_err[XVSFSYNC_MAX_ENC_CHANNEL];
};

#define XVSFSYNC_MAGIC 'X'

/*
 * This ioctl is used to get the IP config (i.e. encode / decode)
 * and max number of channels
 */
#define XVSFSYNC_GET_CFG _IOR(XVSFSYNC_MAGIC, 1, \
                              struct xvsfsync_config*)
/* This ioctl is used to get the channel status */
#define XVSFSYNC_GET_CHAN_STATUS _IOR(XVSFSYNC_MAGIC, 2, u32*)
/* This is used to set the framebuffer address for a channel */
#define XVSFSYNC_SET_CHAN_CONFIG _IOW(XVSFSYNC_MAGIC, 3, \
                                      struct xvsfsync_chan_config*)
/* Enable a channel. The argument is channel number between 0 and 3 */
#define XVSFSYNC_CHAN_ENABLE _IOR(XVSFSYNC_MAGIC, 4, u8)
/* Enable a channel. The argument is channel number between 0 and 3 */
#define XVSFSYNC_CHAN_DISABLE _IOR(XVSFSYNC_MAGIC, 5, u8)
/* This is used to clear the Sync and Watchdog errors  for a channel */
#define XVSFSYNC_CLR_CHAN_ERR _IOW(XVSFSYNC_MAGIC, 6, \
                                   struct xvsfsync_clr_err*)
/* This is used to get the framebuffer done status for a channel */
#define XVSFSYNC_GET_CHAN_FBDONE_STAT _IOR(XVSFSYNC_MAGIC, 7, \
                                           struct xvsfsync_fbdone*)
/* This is used to clear the framebuffer done status for a channel */
#define XVSFSYNC_CLR_CHAN_FBDONE_STAT _IOW(XVSFSYNC_MAGIC, 8, \
                                           struct xvsfsync_fbdone*)
/* Get channel ID.*/
#define XVSFSYNC_RESERVE_GET_CHAN_ID           _IOR(XVSFSYNC_MAGIC, 9, u8 *)

#endif
