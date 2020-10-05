#ifndef __XVFBSYNC_H__
#define __XVFBSYNC_H__

#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>

/* Number of slots/frame buffers allowed in one sync channel  */
#define XVFBSYNC_MAX_FB_NUMBER 3

/**
 * Number of users allowed in one sync channel
 * One producer and one consumer
 */
#define XVFBSYNC_MAX_USER 2

typedef enum
{
  PLANE_Y,
  PLANE_UV,
  PLANE_MAP_Y,
  PLANE_MAP_UV,
  PLANE_MAX_ENUM,               /* sentinel */
} EPlaneId;

/**
 * struct TDimension - Dimension of frame
 * @i_width: Width in pixels
 * @i_height: Height in pixels
 */
typedef struct
{
  uint32_t i_width;
  uint32_t i_height;
} TDimension;

/**
 * struct TPlane - Color Plane
 * @i_offset: Offset of the plane from beginning of the buffer (in bytes)
 * @i_pitch: Pitch/Stride of the plane (in bytes)
 */
typedef struct
{
  uint32_t i_offset;
  uint32_t i_pitch;
} TPlane;

/**
 * struct XLNXLLBuf - Buffer configuration
 * @dma_fd: File descriptor of dma for framebuffer
 * @t_fourcc: Color format
 * @t_dim: Dimension of frame in pixels
 * @t_planes: Array of color plane parameters
 */
typedef struct
{
  uint32_t dma_fd;
  uint32_t t_fourcc;
  TDimension t_dim;
  TPlane t_planes[PLANE_MAX_ENUM];
} XLNXLLBuf;

/**
 * struct ChannelErrIntr - Channel Error Interrupt Status from SyncIp driver
 * @prod_sync: True if producer have synchronization error
 * @prod_wdg: True if producer have watchdog error
 * @cons_sync: True if consumer have synchronization error
 * @cons_wdg: True if consumer have watchdog error
 * @ldiff: True if luma difference > 1
 * @cdiff: True if chroma difference > 1
 */
typedef struct
{
  bool prod_sync;
  bool prod_wdg;
  bool cons_sync;
  bool cons_wdg;
  bool ldiff;
  bool cdiff;
} ChannelErrIntr;

/**
 * struct ChannelIntr - Channel Interrupt types
 * @err: Structure for error interrupts
 * @prod_lfbdone: Producer luma frame buffer done interrupt
 * @prod_cfbdone: Producer chroma frame buffer done interrupt
 * @cons_lfbdone: Consumer luma frame buffer done interrupt
 * @cons_cfbdone: Consumer chroma frame buffer done interrupt
 */
typedef struct {
  ChannelErrIntr err;
  bool prod_lfbdone;
  bool prod_cfbdone;
  bool cons_lfbdone;
  bool cons_cfbdone;
} ChannelIntr;

/**
 * struct ChannelStatus - Channel Status from SyncIp driver
 * @fb_avail: True if framebuffer free to be pushed to
 * @enable: True if channel is enabled
 * @err: Channel error interrupt status
 */
typedef struct
{
  bool fb_avail[XVFBSYNC_MAX_FB_NUMBER][XVFBSYNC_MAX_USER];
  bool enable;
  ChannelErrIntr err;
} ChannelStatus;

/**
 * struct Node - Node structure for Queue
 * @buf: Buffer configuration to wrap
 * @next: Next Node
 */
struct Node
{
  XLNXLLBuf *buf;
  struct Node *next;
};

/**
 * struct Queue - Internal Queue for Encoder Channel
 * @front: Front of queue
 * @last: End of queue
 * @size: Size of queue
 */
typedef struct
{
  struct Node *front;
  struct Node *last;
  uint64_t size;
} Queue;

/**
 * struct SyncIp - SyncIp Hardware Configuration
 * @max_channels: Maximum supported channels
 * @active_channels: Number of active Sync IP channels
 * @max_users: Maximum supported users
 * @max_buffers: Maximum frame buffers
 * @max_cores: Maximum cores to use
 * @fd: File descriptor of SyncIp driver
 */
typedef struct
{
  uint8_t max_channels;
  uint8_t active_channels;
  uint8_t max_users;
  uint8_t max_buffers;
  uint8_t max_cores;
  uint32_t fd;
} SyncIp;

/**
 * struct SyncChannel - Base SyncIp Channel
 * @id: Channel number
 * @enabled: True if channel enabled bit is on
 * @sync: SyncIp configuration to be followed
 * @polling_thread: Thread for polling for errors
 * @mutex: Mutex for locking in Sync IP channel specific context
 * @channel_status: Channel status for current channel
 * @quit: True if SyncIp Channel exited
*/
typedef struct
{
  uint8_t id;
  bool enabled;
  SyncIp *sync;
  pthread_t polling_thread;
  ChannelStatus *channel_status;
  bool quit;
} SyncChannel;

/**
 * struct EncSyncChannel - Encoder SyncIp Channel
 * @sync_channel: Base SyncIp channel
 * @buffers: Internal queue for buffers in a channel
 * @hardware_horizontal_stride_alignment: VCU horizontal requirement
 * @hardware_vertical_stride_alignment: VCU vertical requirement
 */
typedef struct
{
  SyncChannel *sync_channel;
  Queue buffers;
  uint32_t hardware_horizontal_stride_alignment;
  uint32_t hardware_vertical_stride_alignment;
} EncSyncChannel;

/**
 * struct DecSyncChannel - Decoder SyncIp Channel
 * @sync_channel: Base SyncIp channel
 */
typedef struct
{
  SyncChannel sync_channel;
} DecSyncChannel;

/**
 * struct ThreadInfo - Wrap SyncChannel for polling thread
 * @syncchannel: SyncChannel configuration to wrapped
 */
typedef struct
{
  SyncChannel *sync_channel;
} ThreadInfo;


/**
 * xvfbsync_syncip_chan_populate - Initialize SyncIp configuration
 * @syncip_chan: Configuration to be initialized
 * @fd: fd of SyncIp device
 * Returns 0 if no errors, else -1
 */
int xvfbsync_syncip_chan_populate (SyncIp *syncip, SyncChannel * syncip_chan, uint32_t fd);

/**
 * xvfbsync_dec_sync_chan_add_buffer - Push buffer to SyncIp channel
 * @dec_sync_chan: Channel to push to
 * @buf: Buffer to be pushed
 * Returns 0 if no errors, else -1
 */
int xvfbsync_dec_sync_chan_add_buffer (DecSyncChannel * dec_sync_chan,
    XLNXLLBuf * buf);

/**
 * xvfbsync_dec_sync_chan_enable - Enable SyncIp channel
 * @dec_sync_chan: Decoder SyncIp Channel to be enabled
 * Returns 0 if no errors, else -1
 */
int xvfbsync_dec_sync_chan_enable (DecSyncChannel * dec_sync_chan);

/**
 * xvfbsync_dec_sync_chan_populate - Initialize SyncIp channel
 * @dec_sync_chan: Decoder SyncIP channel to be created
 * @syncip: SyncIp configuration to be followed
 * Returns 0 if no errors, else -1
 */
int xvfbsync_dec_sync_chan_populate (DecSyncChannel * dec_sync_chan, SyncIp * syncip);

/**
 * xvfbsync_dec_sync_chan_depopulate - Clean up SyncIp channel
 * @dec_sync_chan: Channel to be destroyed
 * Returns 0 if no errors, else -1
 */
int xvfbsync_dec_sync_chan_depopulate (DecSyncChannel * dec_sync_chan);

/**
 * xvfbsync_enc_sync_chan_add_buffer - Push buffer to internal queue/channel
 * @enc_sync_chan: Channel to push to
 * @buf: Buffer to be pushed
 * Returns 0 if no errors, else -1
 */
int xvfbsync_enc_sync_chan_add_buffer (EncSyncChannel * enc_sync_chan,
    XLNXLLBuf * buf);

/**
 * xvfbsync_enc_sync_chan_enable - Enable SyncIp channel
 * @enc_sync_chan: Channel to be enabled
 * Returns 0 if no errors, else -1
 */
int xvfbsync_enc_sync_chan_enable (EncSyncChannel * enc_sync_chan);

/**
 * xvfbsync_enc_sync_chan_set_intr_mask - Set SyncIp interrupt mask
 * @enc_sync_chan: Channel to mask interrupt
 * @intr_mask: Interrupts that should be masked
 * Returns 0 if no errors, else -1
 */
int xvfbsync_enc_sync_chan_set_intr_mask (EncSyncChannel * enc_sync_chan, ChannelIntr * intr_mask);

/**
 * xvfbsync_enc_sync_chan_populate - Initialize Encoder SyncIp channel
 * @enc_sync_chan: Encoder SYncIP channel to be created
 * @sync_chan: SyncIp channel configuration to be used as base
 * @hardware_horizontal_stride_alignment: VCU horizontal stride alignment
 * @hardware_vertical_stride_alignment: VCU vertical stride alignment
 * Returns 0 if no errors, else -1
 */
int xvfbsync_enc_sync_chan_populate (EncSyncChannel * enc_sync_chan, SyncChannel *sync_chan,
    uint32_t hardware_horizontal_stride_alignment, uint32_t hardware_vertical_stride_alignment);

/**
 * xvfbsync_encSyncChan_depopulate - Clean up SyncIp channel
 * @enc_sync_chan: Channel to be destroyed
 * Returns 0 if no errors, else -1
 */
int xvfbsync_enc_sync_chan_depopulate (EncSyncChannel * enc_sync_chan);

/**
 * xvfbsync_xlnxll_buf_new - Allocate new XLNXLLBuf
 * Returns pointer to buffer or NULL if failed
 */
XLNXLLBuf *xvfbsync_xlnxll_buf_new (void);

#endif
