/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstomxh264enc.h"
#include "gstomxh264utils.h"
#include "gstomxvideo.h"

#ifdef USE_OMX_TARGET_RPI
#include <OMX_Broadcom.h>
#include <OMX_Index.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_omx_h264_enc_debug_category);
#define GST_CAT_DEFAULT gst_omx_h264_enc_debug_category

/* prototypes */
static gboolean gst_omx_h264_enc_set_format (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state);
static GstCaps *gst_omx_h264_enc_get_caps (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state);
static GstFlowReturn gst_omx_h264_enc_handle_output_frame (GstOMXVideoEnc *
    self, GstOMXPort * port, GstBuffer * outbuf, GstOMXBuffer * buf,
    GstVideoCodecFrame * frame);
static gboolean gst_omx_h264_enc_flush (GstVideoEncoder * enc);
static gboolean gst_omx_h264_enc_stop (GstVideoEncoder * enc);
static void gst_omx_h264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_omx_h264_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

enum
{
  PROP_0,
#ifdef USE_OMX_TARGET_RPI
  PROP_INLINESPSPPSHEADERS,
#endif
  PROP_PERIODICITYOFIDRFRAMES,
  PROP_PERIODICITYOFIDRFRAMES_COMPAT,
  PROP_INTERVALOFCODINGINTRAFRAMES,
  PROP_B_FRAMES,
  PROP_ENTROPY_MODE,
  PROP_CONSTRAINED_INTRA_PREDICTION,
  PROP_LOOP_FILTER_MODE,
  PROP_REF_FRAMES,
  PROP_GOP_LENGTH,
  PROP_LOOP_FILTER_BETA_OFFSET,
  PROP_LOOP_FILTER_ALPHA_C0_OFFSET,
};

#ifdef USE_OMX_TARGET_RPI
#define GST_OMX_H264_VIDEO_ENC_INLINE_SPS_PPS_HEADERS_DEFAULT      TRUE
#endif
#define GST_OMX_H264_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT    (0xffffffff)
#define GST_OMX_H264_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT (0xffffffff)
#if defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) || defined(USE_OMX_TARGET_VERSAL_GEN2)
#define GST_OMX_H264_VIDEO_ENC_B_FRAMES_DEFAULT (0)
#define ALIGNMENT "{ au, nal }"
#else
#define GST_OMX_H264_VIDEO_ENC_B_FRAMES_DEFAULT (0xffffffff)
#define ALIGNMENT "au"
#endif
#define GST_OMX_H264_VIDEO_ENC_GOP_LENGTH_DEFAULT (30)
#define GST_OMX_H264_VIDEO_ENC_ENTROPY_MODE_DEFAULT (0xffffffff)
#define GST_OMX_H264_VIDEO_ENC_CONSTRAINED_INTRA_PREDICTION_DEFAULT (FALSE)
#define GST_OMX_H264_VIDEO_ENC_LOOP_FILTER_MODE_DEFAULT (0xffffffff)
#define GST_OMX_H264_VIDEO_ENC_LOOP_FILTER_BETA_OFFSET_DEFAULT (-1)
#define GST_OMX_H264_VIDEO_ENC_LOOP_FILTER_ALPHA_C0_OFFSET_DEFAULT (-1)
#define GST_OMX_H264_VIDEO_ENC_REF_FRAMES_DEFAULT 0
#define GST_OMX_H264_VIDEO_ENC_REF_FRAMES_MIN 0
#define GST_OMX_H264_VIDEO_ENC_REF_FRAMES_MAX 16

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_h264_enc_debug_category, "omxh264enc", 0, \
      "debug category for gst-omx video encoder base class");

#define parent_class gst_omx_h264_enc_parent_class
G_DEFINE_TYPE_WITH_CODE (GstOMXH264Enc, gst_omx_h264_enc,
    GST_TYPE_OMX_VIDEO_ENC, DEBUG_INIT);

#define GST_TYPE_OMX_H264_ENC_ENTROPY_MODE (gst_omx_h264_enc_entropy_mode_get_type ())
static GType
gst_omx_h264_enc_entropy_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {FALSE, "CAVLC entropy mode", "CAVLC"},
      {TRUE, "CABAC entropy mode", "CABAC"},
      {0xffffffff, "Component Default", "default"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstOMXH264EncEntropyMode", values);
  }
  return qtype;
}

#define GST_TYPE_OMX_H264_ENC_LOOP_FILTER_MODE (gst_omx_h264_enc_loop_filter_mode_get_type ())
static GType
gst_omx_h264_enc_loop_filter_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {OMX_VIDEO_AVCLoopFilterEnable, "Enable deblocking filter", "enable"},
      {OMX_VIDEO_AVCLoopFilterDisable, "Disable deblocking filter", "disable"},
      {OMX_VIDEO_AVCLoopFilterDisableSliceBoundary,
            "Disables deblocking filter on slice boundary",
          "disable-slice-boundary"},
      {0xffffffff, "Component Default", "default"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstOMXH264EncLoopFilter", values);
  }
  return qtype;
}

static void
gst_omx_h264_enc_class_init (GstOMXH264EncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *basevideoenc_class = GST_VIDEO_ENCODER_CLASS (klass);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (klass);

  videoenc_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_h264_enc_set_format);
  videoenc_class->get_caps = GST_DEBUG_FUNCPTR (gst_omx_h264_enc_get_caps);

  gobject_class->set_property = gst_omx_h264_enc_set_property;
  gobject_class->get_property = gst_omx_h264_enc_get_property;

#ifdef USE_OMX_TARGET_RPI
  g_object_class_install_property (gobject_class, PROP_INLINESPSPPSHEADERS,
      g_param_spec_boolean ("inline-header",
          "Inline SPS/PPS headers before IDR",
          "Inline SPS/PPS header before IDR",
          GST_OMX_H264_VIDEO_ENC_INLINE_SPS_PPS_HEADERS_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
#endif

  g_object_class_install_property (gobject_class, PROP_PERIODICITYOFIDRFRAMES,
      g_param_spec_uint ("periodicity-idr", "IDR periodicity",
          "Periodicity of IDR frames (0xffffffff=component default)",
          0, G_MAXUINT,
          GST_OMX_H264_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_PERIODICITYOFIDRFRAMES_COMPAT, g_param_spec_uint ("periodicty-idr",
          "IDR periodicity",
          "Periodicity of IDR frames (0xffffffff=component default) DEPRECATED - only for backwards compat",
          0, G_MAXUINT,
          GST_OMX_H264_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_INTERVALOFCODINGINTRAFRAMES,
      g_param_spec_uint ("interval-intraframes",
          "Interval of coding Intra frames",
          "Interval of coding Intra frames (0xffffffff=component default)", 0,
          G_MAXUINT,
          GST_OMX_H264_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_B_FRAMES,
      g_param_spec_uint ("b-frames", "Number of B-frames",
#if !defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) && !defined(USE_OMX_TARGET_VERSAL_GEN2)
          "Number of B-frames between two consecutive I-frames (0xffffffff=component default)",
#else
          "Number of B-frames between two consecutive P-frames",
#endif
          0, G_MAXUINT, GST_OMX_H264_VIDEO_ENC_B_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
#if !defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) && !defined(USE_OMX_TARGET_VERSAL_GEN2)
          GST_PARAM_MUTABLE_READY));
#else
          GST_PARAM_MUTABLE_PLAYING));

  g_object_class_install_property (gobject_class, PROP_GOP_LENGTH,
      g_param_spec_uint ("gop-length",
          "Number of all frames in 1 GOP, Must be in multiple of (b-frames+1)",
          "Distance between two consecutive I frames", 0,
          1000, GST_OMX_H264_VIDEO_ENC_GOP_LENGTH_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
#endif

  g_object_class_install_property (gobject_class, PROP_ENTROPY_MODE,
      g_param_spec_enum ("entropy-mode", "Entropy Mode",
          "Entropy mode for encoding process",
          GST_TYPE_OMX_H264_ENC_ENTROPY_MODE,
          GST_OMX_H264_VIDEO_ENC_ENTROPY_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_CONSTRAINED_INTRA_PREDICTION,
      g_param_spec_boolean ("constrained-intra-prediction",
          "Constrained Intra Prediction",
          "If enabled, prediction only uses residual data and decoded samples "
          "from neighbouring coding blocks coded using intra prediction modes",
          GST_OMX_H264_VIDEO_ENC_CONSTRAINED_INTRA_PREDICTION_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_LOOP_FILTER_MODE,
      g_param_spec_enum ("loop-filter-mode", "Loop Filter mode",
          "Enable or disable the deblocking filter (0xffffffff=component default)",
          GST_TYPE_OMX_H264_ENC_LOOP_FILTER_MODE,
          GST_OMX_H264_VIDEO_ENC_LOOP_FILTER_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

#if defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) || defined(USE_OMX_TARGET_VERSAL_GEN2)
  g_object_class_install_property (gobject_class, PROP_LOOP_FILTER_BETA_OFFSET,
      g_param_spec_int ("loop-filter-beta-offset", "Loop Filter Beta Offset",
          "Beta offset for the deblocking filter, used only when loop-filter-mode is enabled",
          -6, 6, GST_OMX_H264_VIDEO_ENC_LOOP_FILTER_BETA_OFFSET_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  g_object_class_install_property (gobject_class,
      PROP_LOOP_FILTER_ALPHA_C0_OFFSET,
      g_param_spec_int ("loop-filter-alpha-c0-offset",
          "Loop Filter Alpha C0 offset",
          "Alpha C0 offset for the deblocking filter, used only when loop-filter-mode is enabled",
          -6, 6, GST_OMX_H264_VIDEO_ENC_LOOP_FILTER_ALPHA_C0_OFFSET_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
#endif

  g_object_class_install_property (gobject_class, PROP_REF_FRAMES,
      g_param_spec_uchar ("ref-frames", "Reference frames",
          "Number of reference frames used for inter-motion search (0=component default)",
          GST_OMX_H264_VIDEO_ENC_REF_FRAMES_MIN,
          GST_OMX_H264_VIDEO_ENC_REF_FRAMES_MAX,
          GST_OMX_H264_VIDEO_ENC_REF_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  basevideoenc_class->flush = gst_omx_h264_enc_flush;
  basevideoenc_class->stop = gst_omx_h264_enc_stop;

#if defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) || defined(USE_OMX_TARGET_VERSAL_GEN2)
  videoenc_class->cdata.default_sink_template_caps =
      GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_XLNX_LL,
      GST_OMX_VIDEO_ENC_SUPPORTED_FORMATS) " ; "
      GST_VIDEO_CAPS_MAKE (GST_OMX_VIDEO_ENC_SUPPORTED_FORMATS);
#endif

  videoenc_class->cdata.default_src_template_caps = "video/x-h264, "
      "width = (int) [ 16, 4096 ], height = (int) [ 16, 4096 ], "
      "framerate = (fraction) [0, MAX], stream-format=(string) byte-stream, "
      "alignment = (string) " ALIGNMENT;
  videoenc_class->handle_output_frame =
      GST_DEBUG_FUNCPTR (gst_omx_h264_enc_handle_output_frame);

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX H.264 Video Encoder",
      "Codec/Encoder/Video/Hardware",
      "Encode H.264 video streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_omx_set_default_role (&videoenc_class->cdata, "video_encoder.avc");
}

#if defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) || defined(USE_OMX_TARGET_VERSAL_GEN2)
/* We will set OMX il's nPframes & nBframes parameter as below calculation
   based on user's input of GopLength & NumBframes
   nPframes(Number of P frames between each I frame) = (GopLength -1) / (NumBframes+1)
   nBframes(Number of B frames between each I frame) = GopLength - nPframes - 1
*/
static void
compute_gop_pattern (GstOMXH264Enc * self, guint * n_p_frames,
    guint * n_b_frames)
{
  /* gop_length = 0 and 1 both represents Intra only encoding */
  if (!self->gop_length)
    self->gop_length = 1;
  if (self->gop_length == 1) {
    GST_LOG_OBJECT (self,
        "GopLength is 1 so its only Intra. Setting b_frames as 0\n");
    self->b_frames = 0;
  }

  *n_p_frames = (self->gop_length - 1) / (self->b_frames + 1);
  *n_b_frames = self->gop_length - *n_p_frames - 1;
}

static void
update_config_video_gop (GstOMXH264Enc * self)
{
  OMX_ALG_VIDEO_CONFIG_GROUP_OF_PICTURES config;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&config);
  config.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  err =
      gst_omx_component_get_config (GST_OMX_VIDEO_ENC (self)->enc,
      (OMX_INDEXTYPE) OMX_ALG_IndexConfigVideoGroupOfPictures, &config);

  if (err != OMX_ErrorNone)
    GST_ERROR_OBJECT (self,
        "Failed to get b-frames parameter: %s (0x%08x)",
        gst_omx_error_to_string (err), err);

  compute_gop_pattern (self, &config.nPFrames, &config.nBFrames);
  err =
      gst_omx_component_set_config (GST_OMX_VIDEO_ENC (self)->enc,
      (OMX_INDEXTYPE) OMX_ALG_IndexConfigVideoGroupOfPictures, &config);

  if (err != OMX_ErrorNone)
    GST_ERROR_OBJECT (self,
        "Failed to set  parameter: %s (0x%08x)",
        gst_omx_error_to_string (err), err);

}

static void
update_config_loop_filter_beta_offset (GstOMXH264Enc * self)
{
  OMX_ALG_VIDEO_CONFIG_LOOP_FILTER_BETA config;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&config);
  config.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;
  config.nLoopFilterBeta = self->loop_filter_beta_offset;

  err =
      gst_omx_component_set_config (GST_OMX_VIDEO_ENC (self)->enc,
      (OMX_INDEXTYPE) OMX_ALG_IndexConfigVideoLoopFilterBeta, &config);
  if (err != OMX_ErrorNone)
    GST_ERROR_OBJECT (self,
        "Failed to set  parameter: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
}

static void
update_config_loop_filter_alpha_c0_offset (GstOMXH264Enc * self)
{
  OMX_ALG_VIDEO_CONFIG_LOOP_FILTER_TC config;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&config);
  config.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;
  config.nLoopFilterTc = self->loop_filter_alpha_c0_offset;

  err =
      gst_omx_component_set_config (GST_OMX_VIDEO_ENC (self)->enc,
      (OMX_INDEXTYPE) OMX_ALG_IndexConfigVideoLoopFilterTc, &config);
  if (err != OMX_ErrorNone)
    GST_ERROR_OBJECT (self,
        "Failed to set  parameter: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
}
#endif

static void
gst_omx_h264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (object);

  switch (prop_id) {
#ifdef USE_OMX_TARGET_RPI
    case PROP_INLINESPSPPSHEADERS:
      self->inline_sps_pps_headers = g_value_get_boolean (value);
      break;
#endif
    case PROP_PERIODICITYOFIDRFRAMES:
    case PROP_PERIODICITYOFIDRFRAMES_COMPAT:
      self->periodicty_idr = g_value_get_uint (value);
      break;
    case PROP_INTERVALOFCODINGINTRAFRAMES:
      self->interval_intraframes = g_value_get_uint (value);
      break;
    case PROP_B_FRAMES:
      self->b_frames = g_value_get_uint (value);
#if defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) || defined(USE_OMX_TARGET_VERSAL_GEN2)
      if (GST_OMX_VIDEO_ENC (self)->enc)
        update_config_video_gop (self);
#endif
      break;
#if defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) || defined(USE_OMX_TARGET_VERSAL_GEN2)
    case PROP_GOP_LENGTH:
      self->gop_length = g_value_get_uint (value);
      if (GST_OMX_VIDEO_ENC (self)->enc)
        update_config_video_gop (self);
      break;
#endif
    case PROP_ENTROPY_MODE:
      self->entropy_mode = g_value_get_enum (value);
      break;
    case PROP_CONSTRAINED_INTRA_PREDICTION:
      self->constrained_intra_prediction = g_value_get_boolean (value);
      break;
    case PROP_LOOP_FILTER_MODE:
      self->loop_filter_mode = g_value_get_enum (value);
      break;
#if defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) || defined(USE_OMX_TARGET_VERSAL_GEN2)
    case PROP_LOOP_FILTER_BETA_OFFSET:
      self->loop_filter_beta_offset = g_value_get_int (value);
      if (self->loop_filter_mode != OMX_VIDEO_AVCLoopFilterEnable)
        GST_WARNING_OBJECT (self,
            "Loop Filter Mode not set to enabled. Setting beta offset may do nothing.");

      if (GST_OMX_VIDEO_ENC (self)->enc)
        update_config_loop_filter_beta_offset (self);
      break;
    case PROP_LOOP_FILTER_ALPHA_C0_OFFSET:
      self->loop_filter_alpha_c0_offset = g_value_get_int (value);
      if (self->loop_filter_mode != OMX_VIDEO_AVCLoopFilterEnable)
        GST_WARNING_OBJECT (self,
            "Loop Filter Mode not set to enabled. Setting alpha c0 offset may do nothing.");

      if (GST_OMX_VIDEO_ENC (self)->enc)
        update_config_loop_filter_alpha_c0_offset (self);
      break;
#endif
    case PROP_REF_FRAMES:
      self->ref_frames = g_value_get_uchar (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_h264_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (object);

  switch (prop_id) {
#ifdef USE_OMX_TARGET_RPI
    case PROP_INLINESPSPPSHEADERS:
      g_value_set_boolean (value, self->inline_sps_pps_headers);
      break;
#endif
    case PROP_PERIODICITYOFIDRFRAMES:
    case PROP_PERIODICITYOFIDRFRAMES_COMPAT:
      g_value_set_uint (value, self->periodicty_idr);
      break;
    case PROP_INTERVALOFCODINGINTRAFRAMES:
      g_value_set_uint (value, self->interval_intraframes);
      break;
    case PROP_B_FRAMES:
      g_value_set_uint (value, self->b_frames);
      break;
#if defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) || defined(USE_OMX_TARGET_VERSAL_GEN2)
    case PROP_GOP_LENGTH:
      g_value_set_uint (value, self->gop_length);
      break;
#endif
    case PROP_ENTROPY_MODE:
      g_value_set_enum (value, self->entropy_mode);
      break;
    case PROP_CONSTRAINED_INTRA_PREDICTION:
      g_value_set_boolean (value, self->constrained_intra_prediction);
      break;
    case PROP_LOOP_FILTER_MODE:
      g_value_set_enum (value, self->loop_filter_mode);
      break;
#if defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) || defined(USE_OMX_TARGET_VERSAL_GEN2)
    case PROP_LOOP_FILTER_BETA_OFFSET:
      g_value_set_int (value, self->loop_filter_beta_offset);
      break;
    case PROP_LOOP_FILTER_ALPHA_C0_OFFSET:
      g_value_set_int (value, self->loop_filter_alpha_c0_offset);
      break;
#endif
    case PROP_REF_FRAMES:
      g_value_set_uchar (value, self->ref_frames);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_h264_enc_init (GstOMXH264Enc * self)
{
#ifdef USE_OMX_TARGET_RPI
  self->inline_sps_pps_headers =
      GST_OMX_H264_VIDEO_ENC_INLINE_SPS_PPS_HEADERS_DEFAULT;
#endif
  self->periodicty_idr =
      GST_OMX_H264_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT;
  self->interval_intraframes =
      GST_OMX_H264_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT;
  self->b_frames = GST_OMX_H264_VIDEO_ENC_B_FRAMES_DEFAULT;
#if defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) || defined(USE_OMX_TARGET_VERSAL_GEN2)
  self->gop_length = GST_OMX_H264_VIDEO_ENC_GOP_LENGTH_DEFAULT;
  self->loop_filter_beta_offset =
      GST_OMX_H264_VIDEO_ENC_LOOP_FILTER_BETA_OFFSET_DEFAULT;
  self->loop_filter_alpha_c0_offset =
      GST_OMX_H264_VIDEO_ENC_LOOP_FILTER_ALPHA_C0_OFFSET_DEFAULT;
#endif
  self->entropy_mode = GST_OMX_H264_VIDEO_ENC_ENTROPY_MODE_DEFAULT;
  self->constrained_intra_prediction =
      GST_OMX_H264_VIDEO_ENC_CONSTRAINED_INTRA_PREDICTION_DEFAULT;
  self->loop_filter_mode = GST_OMX_H264_VIDEO_ENC_LOOP_FILTER_MODE_DEFAULT;
  self->ref_frames = GST_OMX_H264_VIDEO_ENC_REF_FRAMES_DEFAULT;
}

static gboolean
gst_omx_h264_enc_flush (GstVideoEncoder * enc)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);

  g_list_free_full (self->headers, (GDestroyNotify) gst_buffer_unref);
  self->headers = NULL;

  return GST_VIDEO_ENCODER_CLASS (parent_class)->flush (enc);
}

static gboolean
gst_omx_h264_enc_stop (GstVideoEncoder * enc)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);

  g_list_free_full (self->headers, (GDestroyNotify) gst_buffer_unref);
  self->headers = NULL;

  return GST_VIDEO_ENCODER_CLASS (parent_class)->stop (enc);
}

/* Update OMX_VIDEO_PARAM_PROFILELEVELTYPE.{eProfile,eLevel}
 *
 * Returns TRUE if succeeded or if not supported, FALSE if failed */
static gboolean
update_param_profile_level (GstOMXH264Enc * self,
    OMX_VIDEO_AVCPROFILETYPE profile, OMX_VIDEO_AVCLEVELTYPE level)
{
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self,
        "Getting OMX_IndexParamVideoProfileLevelCurrent not supported by component");
    return TRUE;
  }

  if (profile != OMX_VIDEO_AVCProfileMax)
    param.eProfile = profile;
  if (level != OMX_VIDEO_AVCLevelMax)
    param.eLevel = level;

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err == OMX_ErrorUnsupportedIndex) {
    GST_WARNING_OBJECT (self,
        "Setting OMX_IndexParamVideoProfileLevelCurrent not supported by component");
    return TRUE;
  } else if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Error setting profile %u and level %u: %s (0x%08x)",
        (guint) param.eProfile, (guint) param.eLevel,
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

/* Update OMX_VIDEO_PARAM_AVCTYPE
 *
 * Returns TRUE if succeeded or if not supported, FALSE if failed */
static gboolean
update_param_avc (GstOMXH264Enc * self,
    OMX_VIDEO_AVCPROFILETYPE profile, OMX_VIDEO_AVCLEVELTYPE level)
{
  OMX_VIDEO_PARAM_AVCTYPE param;
  OMX_ERRORTYPE err;
#if defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) || defined(USE_OMX_TARGET_VERSAL_GEN2)
  guint32 p_frames;
  guint32 b_frames;
#endif

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  /* On Android the param struct is initialized manually with default
   * settings rather than using GetParameter() to retrieve them.
   * We should probably do the same when we'll add Android as target.
   * See bgo#783862 for details. */

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoAvc, &param);
  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self,
        "Getting OMX_IndexParamVideoAvc not supported by component");
    return TRUE;
  }

  if (profile != OMX_VIDEO_AVCProfileMax)
    param.eProfile = profile;
  if (level != OMX_VIDEO_AVCLevelMax)
    param.eLevel = level;

#if !defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) && !defined(USE_OMX_TARGET_VERSAL_GEN2)
  /* GOP pattern */
  if (self->interval_intraframes !=
      GST_OMX_H264_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT) {
    param.nPFrames = self->interval_intraframes;

    /* If user specified a specific number of B-frames, reduce the number of
     * P-frames by this amount. If not ensure there is no B-frame to have the
     * requested GOP length. */
    if (self->b_frames != GST_OMX_H264_VIDEO_ENC_B_FRAMES_DEFAULT) {
      if (self->b_frames > self->interval_intraframes) {
        GST_ERROR_OBJECT (self,
            "The interval_intraframes perdiod (%u) needs to be higher than the number of B-frames (%u)",
            self->interval_intraframes, self->b_frames);
        return FALSE;
      }
      param.nPFrames -= self->b_frames;
    } else {
      param.nBFrames = 0;
    }
  }

  if (self->b_frames != GST_OMX_H264_VIDEO_ENC_B_FRAMES_DEFAULT) {
    if (profile == OMX_VIDEO_AVCProfileBaseline && self->b_frames > 0) {
      GST_ERROR_OBJECT (self,
          "Baseline profile doesn't support B-frames (%u requested)",
          self->b_frames);
      return FALSE;
    }
    param.nBFrames = self->b_frames;
  }
#else

  if (profile == OMX_VIDEO_AVCProfileBaseline && self->b_frames > 0) {
    GST_ERROR_OBJECT (self,
        "Baseline profile doesn't support B-frames (%u requested)",
        self->b_frames);
    return FALSE;
  }

  compute_gop_pattern (self, &p_frames, &b_frames);

  if (p_frames != param.nPFrames) {
    GST_LOG_OBJECT (self, "Changing number of P-Frame to %d", p_frames);
    param.nPFrames = p_frames;
  }
  if (b_frames != param.nBFrames) {
    GST_LOG_OBJECT (self, "Changing number of B-Frame to %d", b_frames);
    param.nBFrames = b_frames;
  }
#endif

  if (self->ref_frames != GST_OMX_H264_VIDEO_ENC_REF_FRAMES_DEFAULT)
    param.nRefFrames = self->ref_frames;

  if (self->entropy_mode != GST_OMX_H264_VIDEO_ENC_ENTROPY_MODE_DEFAULT) {
    param.bEntropyCodingCABAC = self->entropy_mode;
  }

  param.bconstIpred = self->constrained_intra_prediction;

  if (self->loop_filter_mode != GST_OMX_H264_VIDEO_ENC_LOOP_FILTER_MODE_DEFAULT) {
    param.eLoopFilterMode = self->loop_filter_mode;
  }

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoAvc, &param);
  if (err == OMX_ErrorUnsupportedIndex) {
    GST_WARNING_OBJECT (self,
        "Setting OMX_IndexParamVideoAvc not supported by component");
    return TRUE;
  } else if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Error setting AVC settings (profile %u and level %u): %s (0x%08x)",
        (guint) param.eProfile, (guint) param.eLevel,
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

#if !defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) && !defined(USE_OMX_TARGET_VERSAL_GEN2)
static gboolean
set_avc_intra_period (GstOMXH264Enc * self)
{
  OMX_VIDEO_CONFIG_AVCINTRAPERIOD config_avcintraperiod;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&config_avcintraperiod);
  config_avcintraperiod.nPortIndex =
      GST_OMX_VIDEO_ENC (self)->enc_out_port->index;
  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexConfigVideoAVCIntraPeriod, &config_avcintraperiod);
  if (err == OMX_ErrorUnsupportedIndex) {
    GST_WARNING_OBJECT (self,
        "OMX_IndexConfigVideoAVCIntraPeriod  not supported by component");
    return TRUE;
  } else if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "can't get OMX_IndexConfigVideoAVCIntraPeriod %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "default nPFrames:%u, nIDRPeriod:%u",
      (guint) config_avcintraperiod.nPFrames,
      (guint) config_avcintraperiod.nIDRPeriod);

  if (self->periodicty_idr !=
      GST_OMX_H264_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT) {
    config_avcintraperiod.nIDRPeriod = self->periodicty_idr;
  }

  if (self->interval_intraframes !=
      GST_OMX_H264_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT) {
    /* This OMX API doesn't allow us to specify the number of B-frames.
     * So if user requested one we have to rely on update_param_avc()
     * to configure the intraframes interval so it can take the
     * B-frames into account. */
    if (self->b_frames == GST_OMX_H264_VIDEO_ENC_B_FRAMES_DEFAULT)
      config_avcintraperiod.nPFrames = self->interval_intraframes;
  }

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexConfigVideoAVCIntraPeriod, &config_avcintraperiod);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "can't set OMX_IndexConfigVideoAVCIntraPeriod %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}
#else
static gboolean
set_alg_decoding_refresh (GstOMXH264Enc * self)
{
  OMX_ERRORTYPE err;

  if (GST_OMX_VIDEO_ENC (self)->gdr_mode == OMX_ALG_GDR_HORIZONTAL ||
      GST_OMX_VIDEO_ENC (self)->gdr_mode == OMX_ALG_GDR_VERTICAL) {
    OMX_ALG_VIDEO_PARAM_RECOVERY_POINT config_rp;
    GST_OMX_INIT_STRUCT (&config_rp);
    config_rp.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

    GST_DEBUG_OBJECT (self, "nIDRPeriod:%u",
        (guint) config_rp.nRecoveryPointFrequency);

    config_rp.nRecoveryPointFrequency = self->periodicty_idr;

    err =
        gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoRecoveryPoint,
        &config_rp);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self,
          "can't set OMX_ALG_IndexParamVideoRecoveryPoint %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      return FALSE;
    }
  }
  else {
    OMX_ALG_VIDEO_PARAM_INSTANTANEOUS_DECODING_REFRESH config_idr;
    GST_OMX_INIT_STRUCT (&config_idr);
    config_idr.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

    GST_DEBUG_OBJECT (self, "nIDRPeriod:%u",
        (guint) config_idr.nInstantaneousDecodingRefreshFrequency);

    config_idr.nInstantaneousDecodingRefreshFrequency = self->periodicty_idr;

    err =
        gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoInstantaneousDecodingRefresh,
        &config_idr);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self,
          "can't set OMX_ALG_IndexParamVideoInstantaneousDecodingRefresh %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      return FALSE;
    }
  }
  return TRUE;
}
#endif



#ifdef USE_OMX_TARGET_RPI
static gboolean
set_brcm_video_intra_period (GstOMXH264Enc * self)
{
  OMX_PARAM_U32TYPE intra_period;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&intra_period);

  intra_period.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;
  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexConfigBrcmVideoIntraPeriod, &intra_period);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "can't get OMX_IndexConfigBrcmVideoIntraPeriod %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "default OMX_IndexConfigBrcmVideoIntraPeriod: %u",
      (guint) intra_period.nU32);

  if (self->interval_intraframes ==
      GST_OMX_H264_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT)
    return TRUE;

  intra_period.nU32 = self->interval_intraframes;

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexConfigBrcmVideoIntraPeriod, &intra_period);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "can't set OMX_IndexConfigBrcmVideoIntraPeriod %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "OMX_IndexConfigBrcmVideoIntraPeriod set to %u",
      (guint) intra_period.nU32);

  return TRUE;
}
#endif

#if defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) || defined(USE_OMX_TARGET_VERSAL_GEN2)
static gboolean
set_alg_loop_filter_beta (GstOMXH264Enc * self)
{
  OMX_ALG_VIDEO_PARAM_LOOP_FILTER_BETA config;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&config);
  config.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;
  config.nLoopFilterBeta = self->loop_filter_beta_offset;

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoLoopFilterBeta, &config);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "can't set OMX_ALG_IndexParamVideoLoopFilterBeta %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "OMX_ALG_IndexParamVideoLoopFilterBeta set to %d",
      config.nLoopFilterBeta);

  return TRUE;
}

static gboolean
set_alg_loop_filter_alpha_c0 (GstOMXH264Enc * self)
{
  OMX_ALG_VIDEO_CONFIG_LOOP_FILTER_TC config;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&config);
  config.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;
  config.nLoopFilterTc = self->loop_filter_alpha_c0_offset;

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoLoopFilterTc, &config);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "can't set OMX_ALG_IndexParamVideoLoopFilterTc %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "OMX_ALG_IndexParamVideoLoopFilterTc set to %d",
      config.nLoopFilterTc);

  return TRUE;

}
#endif

static gboolean
gst_omx_h264_enc_set_format (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoCodecState * state)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);
  GstCaps *peercaps;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
#ifdef USE_OMX_TARGET_RPI
  OMX_CONFIG_PORTBOOLEANTYPE config_inline_header;
#endif
  OMX_ERRORTYPE err;
  const gchar *profile_string, *level_string;
  OMX_VIDEO_AVCPROFILETYPE profile = OMX_VIDEO_AVCProfileMax;
  OMX_VIDEO_AVCLEVELTYPE level = OMX_VIDEO_AVCLevelMax;
  gboolean enable_subframe = FALSE;

#ifdef USE_OMX_TARGET_RPI
  GST_OMX_INIT_STRUCT (&config_inline_header);
  config_inline_header.nPortIndex =
      GST_OMX_VIDEO_ENC (self)->enc_out_port->index;
  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamBrcmVideoAVCInlineHeaderEnable, &config_inline_header);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "can't get OMX_IndexParamBrcmVideoAVCInlineHeaderEnable %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  if (self->inline_sps_pps_headers) {
    config_inline_header.bEnabled = OMX_TRUE;
  } else {
    config_inline_header.bEnabled = OMX_FALSE;
  }

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamBrcmVideoAVCInlineHeaderEnable, &config_inline_header);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "can't set OMX_IndexParamBrcmVideoAVCInlineHeaderEnable %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }
#endif

#if !defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) && !defined(USE_OMX_TARGET_VERSAL_GEN2)
  /* Configure GOP pattern */
  if (self->periodicty_idr !=
      GST_OMX_H264_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT
      || self->interval_intraframes !=
      GST_OMX_H264_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT) {
    set_avc_intra_period (self);
  }
#else
  if (self->periodicty_idr !=
      GST_OMX_H264_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT) {
    set_alg_decoding_refresh (self);
  }
#endif

#ifdef USE_OMX_TARGET_RPI
  /* The Pi uses a specific OMX setting to configure the intra period */

  if (self->interval_intraframes)
    set_brcm_video_intra_period (self);
#endif

#if defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) || defined(USE_OMX_TARGET_VERSAL_GEN2)
  if (self->loop_filter_mode == OMX_VIDEO_AVCLoopFilterEnable) {
    set_alg_loop_filter_beta (self);
    set_alg_loop_filter_alpha_c0 (self);
  }
#endif

  gst_omx_port_get_port_definition (GST_OMX_VIDEO_ENC (self)->enc_out_port,
      &port_def);
  port_def.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
  err =
      gst_omx_port_update_port_definition (GST_OMX_VIDEO_ENC
      (self)->enc_out_port, &port_def);
  if (err != OMX_ErrorNone)
    return FALSE;

  /* Set profile and level */
  peercaps = gst_pad_peer_query_caps (GST_VIDEO_ENCODER_SRC_PAD (enc),
      gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (enc)));
  if (peercaps) {
    GstStructure *s;
    const gchar *alignment_string;

    if (gst_caps_is_empty (peercaps)) {
      gst_caps_unref (peercaps);
      GST_ERROR_OBJECT (self, "Empty caps");
      return FALSE;
    }

    s = gst_caps_get_structure (peercaps, 0);
    profile_string = gst_structure_get_string (s, "profile");
    if (profile_string) {
      profile = gst_omx_h264_utils_get_profile_from_str (profile_string);
      if (profile == OMX_VIDEO_AVCProfileMax)
        goto unsupported_profile;
    }
    level_string = gst_structure_get_string (s, "level");
    if (level_string) {
      level = gst_omx_h264_utils_get_level_from_str (level_string);
      if (level == OMX_VIDEO_AVCLevelMax)
        goto unsupported_level;
    }

    alignment_string = gst_structure_get_string (s, "alignment");
    if (alignment_string && g_str_equal (alignment_string, "nal"))
      enable_subframe = TRUE;

    gst_caps_unref (peercaps);
  }

  if (profile != OMX_VIDEO_AVCProfileMax || level != OMX_VIDEO_AVCLevelMax) {
    /* OMX provides 2 API to set the profile and level. We try using the
     * generic one here and the H264 specific when calling
     * update_param_avc() */
    if (!update_param_profile_level (self, profile, level))
      return FALSE;
  }

  gst_omx_port_set_subframe (GST_OMX_VIDEO_ENC (self)->enc_out_port,
      enable_subframe);

  if (!update_param_avc (self, profile, level))
    return FALSE;

  return TRUE;

unsupported_profile:
  GST_ERROR_OBJECT (self, "Unsupported profile %s", profile_string);
  gst_caps_unref (peercaps);
  return FALSE;

unsupported_level:
  GST_ERROR_OBJECT (self, "Unsupported level %s", level_string);
  gst_caps_unref (peercaps);
  return FALSE;
}

static GstCaps *
gst_omx_h264_enc_get_caps (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoCodecState * state)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);
  GstCaps *caps;
  OMX_ERRORTYPE err;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  const gchar *profile, *level, *alignment;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err != OMX_ErrorNone && err != OMX_ErrorUnsupportedIndex)
    return NULL;

  if (gst_omx_port_get_subframe (GST_OMX_VIDEO_ENC (self)->enc_out_port))
    alignment = "nal";
  else
    alignment = "au";

  caps = gst_caps_new_simple ("video/x-h264",
      "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, alignment, NULL);

  if (err == OMX_ErrorNone) {
    profile = gst_omx_h264_utils_get_profile_from_enum (param.eProfile);
    if (!profile) {
      g_assert_not_reached ();
      gst_caps_unref (caps);
      return NULL;
    }

    switch (param.eLevel) {
      case OMX_VIDEO_AVCLevel1:
        level = "1";
        break;
      case OMX_VIDEO_AVCLevel1b:
        level = "1b";
        break;
      case OMX_VIDEO_AVCLevel11:
        level = "1.1";
        break;
      case OMX_VIDEO_AVCLevel12:
        level = "1.2";
        break;
      case OMX_VIDEO_AVCLevel13:
        level = "1.3";
        break;
      case OMX_VIDEO_AVCLevel2:
        level = "2";
        break;
      case OMX_VIDEO_AVCLevel21:
        level = "2.1";
        break;
      case OMX_VIDEO_AVCLevel22:
        level = "2.2";
        break;
      case OMX_VIDEO_AVCLevel3:
        level = "3";
        break;
      case OMX_VIDEO_AVCLevel31:
        level = "3.1";
        break;
      case OMX_VIDEO_AVCLevel32:
        level = "3.2";
        break;
      case OMX_VIDEO_AVCLevel4:
        level = "4";
        break;
      case OMX_VIDEO_AVCLevel41:
        level = "4.1";
        break;
      case OMX_VIDEO_AVCLevel42:
        level = "4.2";
        break;
      case OMX_VIDEO_AVCLevel5:
        level = "5";
        break;
      case OMX_VIDEO_AVCLevel51:
        level = "5.1";
        break;
#if defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS) || defined(USE_OMX_TARGET_VERSAL_GEN2)
      case OMX_ALG_VIDEO_AVCLevel52:
        level = "5.2";
        break;
      case OMX_ALG_VIDEO_AVCLevel60:
        level = "6.0";
        break;
      case OMX_ALG_VIDEO_AVCLevel61:
        level = "6.1";
        break;
      case OMX_ALG_VIDEO_AVCLevel62:
        level = "6.2";
        break;
#endif
      default:
        g_assert_not_reached ();
        gst_caps_unref (caps);
        return NULL;
    }
    gst_caps_set_simple (caps,
        "profile", G_TYPE_STRING, profile, "level", G_TYPE_STRING, level, NULL);
  }

  return caps;
}

static GstFlowReturn
gst_omx_h264_enc_handle_output_frame (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstBuffer * outbuf, GstOMXBuffer * buf, GstVideoCodecFrame * frame)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);

  if (buf->omx_buf->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
    /* The codec data is SPS/PPS but our output is stream-format=byte-stream.
     * For bytestream stream format the SPS/PPS is only in-stream and not
     * in the caps!
     */
    GstBuffer *hdrs;
    GstFlowReturn flow_ret;

    GST_DEBUG_OBJECT (self, "got codecconfig in byte-stream format");

    if (outbuf->pool) {
      /* if the buffer is from the pool, copy it to avoid holding an OMX
       * buffer forever in the codec data; there is a danger of starvation */
      hdrs = gst_buffer_copy_deep (outbuf);
      gst_buffer_unref (outbuf);
    } else {
      hdrs = outbuf;
    }
    GST_BUFFER_FLAG_SET (hdrs, GST_BUFFER_FLAG_HEADER);

    self->headers = g_list_append (self->headers, gst_buffer_ref (hdrs));
    frame->output_buffer = hdrs;
    flow_ret =
        gst_video_encoder_finish_subframe (GST_VIDEO_ENCODER (self), frame);
    gst_video_codec_frame_unref (frame);

    return flow_ret;
  } else if (self->headers) {
    gst_video_encoder_set_headers (GST_VIDEO_ENCODER (self), self->headers);
    self->headers = NULL;
  }

  return
      GST_OMX_VIDEO_ENC_CLASS
      (gst_omx_h264_enc_parent_class)->handle_output_frame (enc, port, outbuf,
      buf, frame);
}
