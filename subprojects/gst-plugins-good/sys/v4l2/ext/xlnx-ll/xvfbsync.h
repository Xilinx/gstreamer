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
 * struct ChannelStatus - Channel Status from SyncIp driver
 * @fb_avail: True if framebuffer free to be pushed to
 * @enable: True if channel is enabled
 * @sync_error: True if have synchronization error
 * @watchdog_error: True if have watchdog error
 * @luma_diff_error: True if luma difference > 1
 * @chroma_diff_error: True if chroma difference > 1
 */
typedef struct
{
  bool fb_avail[XVFBSYNC_MAX_FB_NUMBER][XVFBSYNC_MAX_USER];
  bool enable;
  bool sync_error;
  bool watchdog_error;
  bool luma_diff_error;
  bool chroma_diff_error;
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
 * @max_users: Maximum supported users
 * @max_buffers: Maximum frame buffers
 * @max_cores: Maximum cores to use
 * @fd: File descriptor of SyncIp driver
 * @quit: True if SyncIp exited
 * @polling_thread: Thread for polling for errors
 * @mutex: Mutex for locking
 * @event_listeners: Listening functions to output errors
 * @channel_statuses: Channel statuses for all channels
 */
typedef struct
{
  uint8_t max_channels;
  uint8_t max_users;
  uint8_t max_buffers;
  uint8_t max_cores;
  uint32_t fd;
  bool quit;
  pthread_t polling_thread;
  pthread_mutex_t mutex;
  void (*(*event_listeners)) (ChannelStatus *);
  ChannelStatus *channel_statuses;
} SyncIp;

/**
 * struct SyncChannel - Base SyncIp Channel
 * @id: Channel number
 * @enabled: True if channel enabled bit is on
 * @sync: SyncIp configuration to be followed
 */
typedef struct
{
  uint8_t id;
  bool enabled;
  SyncIp *sync;
} SyncChannel;

/**
 * struct EncSyncChannel - Encoder SyncIp Channel
 * @sync_channel: Base SyncIp channel
 * @buffers: Internal queue for channel
 * @mutex: Mutex for locking
 * @is_running: True if channel is operating correctly
 * @hardware_horizontal_stride_alignment: VCU horizontal requirement
 * @hardware_vertical_stride_alignment: VCU vertical requirement
 */
typedef struct
{
  SyncChannel sync_channel;
  Queue buffers;
  pthread_mutex_t mutex;
  bool is_running;
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
 * struct ThreadInfo - Wrap SyncIp for polling thread
 * @syncip: SyncIp configuration to wrapped
 */
typedef struct
{
  SyncIp *syncip;
} ThreadInfo;

/**
 * xvfbsync_syncip_get_free_channel - Reserve channel from SyncIp driver
 * @syncip: SyncIp configuration to be queried
 * Returns 0 if no errors, else -1
 */
int xvfbsync_syncip_get_free_channel (SyncIp * syncip);

/**
 * xvfbsync_syncip_get_status - Obtain status from SyncIp driver
 * @syncip: SyncIp configuration to be queried
 * @chan_id: Channel to be queried
 * Returns pointer if no errors, else NULL
 */
ChannelStatus *xvfbsync_syncip_get_status (SyncIp * syncip, uint8_t chan_id);

/**
 * xvfbsync_syncip_populate - Initialize SyncIp configuration
 * @syncip: Configuration to be initialized
 * @fd: fd of SyncIp device
 * Returns 0 if no errors, else -1
 */
int xvfbsync_syncip_populate (SyncIp * syncip, uint32_t fd);

/**
 * xvfbsync_syncip_depopulate - Clean up SyncIp configuration
 * @syncip: Configuration to be destroyed
 * Returns 0 if no errors, else -1
 */
int xvfbsync_syncip_depopulate (SyncIp * syncip);

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
 * @dec_sync_chan: Channel to be enabled
 * Returns 0 if no errors, else -1
 */
int xvfbsync_dec_sync_chan_enable (DecSyncChannel * dec_sync_chan);

/**
 * xvfbsync_dec_sync_chan_populate - Initialize SyncIp channel
 * @dec_sync_chan: Channel to be created
 * @syncip: SyncIp configuration to be followed
 * @id: Channel number
 * Returns 0 if no errors, else -1
 */
int xvfbsync_dec_sync_chan_populate (DecSyncChannel * dec_sync_chan,
    SyncIp * syncip, uint8_t id);

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
 * xvfbsync_enc_sync_chan_populate - Initialize SyncIp channel
 * @enc_sync_chan: Channel to be created
 * @syncip: SyncIp configuration to be followed
 * @id: Channel number
 * @hardware_horizontal_stride_alignment: VCU horizontal stride alignment
 * @hardware_vertical_stride_alignment: VCU vertical stride alignment
 * Returns 0 if no errors, else -1
 */
int xvfbsync_enc_sync_chan_populate (EncSyncChannel * enc_sync_chan,
    SyncIp * syncip, uint8_t id,
    uint32_t hardware_horizontal_stride_alignment,
    uint32_t hardware_vertical_stride_alignment);

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
