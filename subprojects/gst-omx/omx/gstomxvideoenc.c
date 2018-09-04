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
#include <gst/video/gstvideometa.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/video/video.h>
#include <string.h>
#include <stdlib.h>

#include "gstomxbufferpool.h"
#include "gstomxvideo.h"
#include "gstomxvideoenc.h"

#ifdef USE_OMX_TARGET_RPI
#include <OMX_Broadcom.h>
#include <OMX_Index.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_omx_video_enc_debug_category);
#define GST_CAT_DEFAULT gst_omx_video_enc_debug_category

#define GST_TYPE_OMX_VIDEO_ENC_CONTROL_RATE (gst_omx_video_enc_control_rate_get_type ())
static GType
gst_omx_video_enc_control_rate_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {OMX_Video_ControlRateDisable, "Disable", "disable"},
      {OMX_Video_ControlRateVariable, "Variable", "variable"},
      {OMX_Video_ControlRateConstant, "Constant", "constant"},
      {OMX_Video_ControlRateVariableSkipFrames, "Variable Skip Frames",
          "variable-skip-frames"},
      {OMX_Video_ControlRateConstantSkipFrames, "Constant Skip Frames",
          "constant-skip-frames"},
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
      {OMX_ALG_Video_ControlRateVariableCapped, "Capped Variable",
          "capped-variable"},
      {OMX_ALG_Video_ControlRateLowLatency, "Low Latency", "low-latency"},
#endif
      {0xffffffff, "Component Default", "default"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstOMXVideoEncControlRate", values);
  }
  return qtype;
}

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
#define GST_TYPE_OMX_VIDEO_ENC_QP_MODE (gst_omx_video_enc_qp_mode_get_type ())
typedef enum
{
  UNIFORM_QP,
  ROI_QP,
  AUTO_QP,
  LOAD_QP_ABSOLUTE,
  LOAD_QP_RELATIVE,
} GstOMXVideoEncQpMode;


static GType
gst_omx_video_enc_qp_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {UNIFORM_QP, "Use the same QP for all coding units of the frame",
          "uniform"},
      {ROI_QP,
            "Adjust QP according to the regions of interest defined on each frame. Must be set to handle ROI metadata.",
          "roi"},
      {AUTO_QP,
            "Let the VCU encoder change the QP for each coding unit according to its content",
          "auto"},
      {LOAD_QP_ABSOLUTE,
            "Uses absolute QP values set by user. Must be set to use External QP buffer",
          "load-qp-absolute"},
      {LOAD_QP_RELATIVE,
            "Uses Relative/Delta QP values set by user. Must be set to use External QP buffer",
          "load-qp-relative"},
      {0xffffffff, "Component Default", "default"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstOMXVideoEncQpMode", values);
  }
  return qtype;
}

#define GST_TYPE_OMX_VIDEO_ENC_GOP_MODE (gst_omx_video_enc_gop_mode_get_type ())
static GType
gst_omx_video_enc_gop_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {OMX_ALG_GOP_MODE_DEFAULT, "Basic GOP settings", "basic"},
      {OMX_ALG_GOP_MODE_DEFAULT_B, "Basic GOP settings, includes only B-frames",
          "basic-b"},
      {OMX_ALG_GOP_MODE_PYRAMIDAL,
          "Advanced GOP pattern with hierarchical B-frames", "pyramidal"},
      {OMX_ALG_GOP_MODE_PYRAMIDAL_B,
            "Advanced GOP pattern with hierarchical B-frames, includes only B-frames",
          "pyramidal-b"},
      {OMX_ALG_GOP_MODE_LOW_DELAY_P, "Single I-frame followed by P-frames only",
          "low-delay-p"},
      {OMX_ALG_GOP_MODE_LOW_DELAY_B, "Single I-frame followed by B-frames only",
          "low-delay-b"},
      {OMX_ALG_GOP_MODE_ADAPTIVE, "Advanced GOP pattern with adaptive B-frames",
          "adaptive"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstOMXVideoEncGopMode", values);
  }
  return qtype;
}

#define GST_TYPE_OMX_VIDEO_ENC_GDR_MODE (gst_omx_video_enc_gdr_mode_get_type ())
static GType
gst_omx_video_enc_gdr_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {OMX_ALG_GDR_OFF, "No GDR", "disabled"},
      {OMX_ALG_GDR_VERTICAL,
            "Gradual refresh using a vertical bar moving from left to right",
          "vertical"},
      {OMX_ALG_GDR_HORIZONTAL,
            "Gradual refresh using a horizontal bar moving from top to bottom",
          "horizontal"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstOMXVideoEncGdrMode", values);
  }
  return qtype;
}

#define GST_TYPE_OMX_VIDEO_ENC_SCALING_LIST (gst_omx_video_enc_scaling_list_get_type ())
static GType
gst_omx_video_enc_scaling_list_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {OMX_ALG_SCL_DEFAULT, "Default scaling list mode", "default"},
      {OMX_ALG_SCL_FLAT, "Flat scaling list mode", "flat"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstOMXVideoEncScalingList", values);
  }
  return qtype;
}

#define GST_TYPE_OMX_VIDEO_ENC_ASPECT_RATIO (gst_omx_video_enc_aspect_ratio_get_type ())
static GType
gst_omx_video_enc_aspect_ratio_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {OMX_ALG_ASPECT_RATIO_AUTO,
            "4:3 for SD video,16:9 for HD video,unspecified for unknown format",
          "auto"},
      {OMX_ALG_ASPECT_RATIO_4_3, "4:3 aspect ratio", "4-3"},
      {OMX_ALG_ASPECT_RATIO_16_9, "16:9 aspect ratio", "16-9"},
      {OMX_ALG_ASPECT_RATIO_NONE,
          "Aspect ratio information is not present in the stream", "none"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstOMXVideoEncAspectRatio", values);
  }
  return qtype;
}

#define GST_TYPE_OMX_VIDEO_ENC_ROI_QUALITY (gst_omx_video_enc_roi_quality_type ())
static GType
gst_omx_video_enc_roi_quality_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {OMX_ALG_ROI_QUALITY_HIGH, "Delta QP of -5", "high"},
      {OMX_ALG_ROI_QUALITY_MEDIUM, "Delta QP of 0", "medium"},
      {OMX_ALG_ROI_QUALITY_LOW, "Delta QP of +5", "low"},
      {OMX_ALG_ROI_QUALITY_DONT_CARE, "Maximum delta QP value", "dont-care"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstOMXVideoEncRoiQuality", values);
  }
  return qtype;
}

#define LATENCY_MODE_DEPRECATION_MESSAGE  \
  "Use a caps filter with 'alignment' field set to 'au' for " \
  "normal latency and 'nal' for subframe latency " \
  "(e.g.  ... ! omxh264enc ! video/x-h264,alignment=nal ! ...)."
#define GST_TYPE_OMX_VIDEO_ENC_LATENCY_MODE (gst_omx_video_enc_latency_mode_get_type ())
typedef enum
{
  ENC_LATENCY_MODE_NORMAL,
  ENC_LATENCY_MODE_LOW,
} GstOMXVideoEncLatencyMode;

static GType
gst_omx_video_enc_latency_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {ENC_LATENCY_MODE_NORMAL, "Normal mode", "normal"},
      {ENC_LATENCY_MODE_LOW, "Low latency mode", "low-latency"},
      {0xffffffff, "Component Default", "default"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstOMXVideoEncLatencyMode", values);
  }
  return qtype;
}
#endif

/* prototypes */
static void gst_omx_video_enc_finalize (GObject * object);
static void gst_omx_video_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_omx_video_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static GstStateChangeReturn
gst_omx_video_enc_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_omx_video_enc_open (GstVideoEncoder * encoder);
static gboolean gst_omx_video_enc_close (GstVideoEncoder * encoder);
static gboolean gst_omx_video_enc_start (GstVideoEncoder * encoder);
static gboolean gst_omx_video_enc_stop (GstVideoEncoder * encoder);
static gboolean gst_omx_video_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static gboolean gst_omx_video_enc_flush (GstVideoEncoder * encoder);
static GstFlowReturn gst_omx_video_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static gboolean gst_omx_video_enc_finish (GstVideoEncoder * encoder);
static gboolean gst_omx_video_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static GstCaps *gst_omx_video_enc_getcaps (GstVideoEncoder * encoder,
    GstCaps * filter);
static gboolean gst_omx_video_enc_decide_allocation (GstVideoEncoder * encoder,
    GstQuery * query);

static GstFlowReturn gst_omx_video_enc_drain (GstOMXVideoEnc * self);

static GstFlowReturn gst_omx_video_enc_handle_output_frame (GstOMXVideoEnc *
    self, GstOMXPort * port, GstOMXBuffer * buf, GstVideoCodecFrame * frame);

static gboolean gst_omx_video_enc_sink_event (GstVideoEncoder * encoder,
    GstEvent * event);

enum
{
  PROP_0,
  PROP_CONTROL_RATE,
  PROP_TARGET_BITRATE,
  PROP_QUANT_I_FRAMES,
  PROP_QUANT_P_FRAMES,
  PROP_QUANT_B_FRAMES,
  PROP_QP_MODE,
  PROP_MIN_QP,
  PROP_MAX_QP,
  PROP_GOP_MODE,
  PROP_GDR_MODE,
  PROP_INITIAL_DELAY,
  PROP_CPB_SIZE,
  PROP_SCALING_LIST,
  PROP_LOW_BANDWIDTH,
  PROP_MAX_BITRATE,
  PROP_MAX_QUALITY,
  PROP_ASPECT_RATIO,
  PROP_FILLER_DATA,
  PROP_NUM_SLICES,
  PROP_SLICE_SIZE,
  PROP_DEPENDENT_SLICE,
  PROP_DEFAULT_ROI_QUALITY,
  PROP_PREFETCH_BUFFER,
  PROP_LATENCY_MODE,
  PROP_LONGTERM_REF,
  PROP_LONGTERM_FREQUENCY,
  PROP_LOOK_AHEAD,
  PROP_SKIP_FRAME,
  PROP_MAX_PICTURE_SIZE,
  PROP_MAX_PICTURE_SIZES,
};

/* FIXME: Better defaults */
#define GST_OMX_VIDEO_ENC_CONTROL_RATE_DEFAULT (0xffffffff)
#define GST_OMX_VIDEO_ENC_TARGET_BITRATE_DEFAULT (0xffffffff)
#define GST_OMX_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT (0xffffffff)
#define GST_OMX_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT (0xffffffff)
#define GST_OMX_VIDEO_ENC_QUANT_B_FRAMES_DEFAULT (0xffffffff)
#define GST_OMX_VIDEO_ENC_QP_MODE_DEFAULT (0xffffffff)
#define GST_OMX_VIDEO_ENC_MIN_QP_DEFAULT (10)
#define GST_OMX_VIDEO_ENC_MAX_QP_DEFAULT (51)
#define GST_OMX_VIDEO_ENC_GOP_MODE_DEFAULT (OMX_ALG_GOP_MODE_DEFAULT)
#define GST_OMX_VIDEO_ENC_GDR_MODE_DEFAULT (OMX_ALG_GDR_OFF)
#define GST_OMX_VIDEO_ENC_INITIAL_DELAY_DEFAULT (1500)
#define GST_OMX_VIDEO_ENC_CPB_SIZE_DEFAULT (3000)
#define GST_OMX_VIDEO_ENC_SCALING_LIST_DEFAULT (OMX_ALG_SCL_DEFAULT)
#define GST_OMX_VIDEO_ENC_LOW_BANDWIDTH_DEFAULT (FALSE)
#define GST_OMX_VIDEO_ENC_MAX_BITRATE_DEFAULT (0xffffffff)
#define GST_OMX_VIDEO_ENC_MAX_QUALITY_DEFAULT (0xffffffff)
#define GST_OMX_VIDEO_ENC_ASPECT_RATIO_DEFAULT (OMX_ALG_ASPECT_RATIO_AUTO)
#define GST_OMX_VIDEO_ENC_FILLER_DATA_DEFAULT (TRUE)
#define GST_OMX_VIDEO_ENC_NUM_SLICES_DEFAULT (0xffffffff)
#define GST_OMX_VIDEO_ENC_SLICE_SIZE_DEFAULT (0)
#define GST_OMX_VIDEO_ENC_DEPENDENT_SLICE_DEFAULT (FALSE)
#define GST_OMX_VIDEO_ENC_DEFAULT_ROI_QUALITY OMX_ALG_ROI_QUALITY_HIGH
#define GST_OMX_VIDEO_ENC_PREFETCH_BUFFER_DEFAULT (FALSE)
#define GST_OMX_VIDEO_ENC_LATENCY_MODE_DEFAULT (0xffffffff)
#define GST_OMX_VIDEO_ENC_LONGTERM_REF_DEFAULT (FALSE)
#define GST_OMX_VIDEO_ENC_LONGTERM_FREQUENCY_DEFAULT (0)
#define GST_OMX_VIDEO_ENC_LOOK_AHEAD_DEFAULT (0)
#define GST_OMX_VIDEO_ENC_SKIP_FRAME_DEFAULT (FALSE)
#define GST_OMX_VIDEO_ENC_MAX_PICTURE_SIZE_DEFAULT (0)
#define GST_OMX_VIDEO_ENC_MAX_PICTURE_SIZE_I_DEFAULT (0)
#define GST_OMX_VIDEO_ENC_MAX_PICTURE_SIZE_P_DEFAULT (0)
#define GST_OMX_VIDEO_ENC_MAX_PICTURE_SIZE_B_DEFAULT (0)

/* ZYNQ_USCALE_PLUS encoder custom events */
#define OMX_ALG_GST_EVENT_INSERT_LONGTERM "omx-alg/insert-longterm"
#define OMX_ALG_GST_EVENT_USE_LONGTERM "omx-alg/use-longterm"
#define OMX_ALG_GST_EVENT_SCENE_CHANGE "omx-alg/scene-change"
#define OMX_ALG_GST_EVENT_INSERT_PREFIX_SEI "omx-alg/insert-prefix-sei"
#define OMX_ALG_GST_EVENT_INSERT_SUFFIX_SEI "omx-alg/insert-suffix-sei"
#define OMX_ALG_GST_EVENT_LOAD_QP "omx-alg/load-qp"

/* class initialization */
#define do_init \
{ \
  GST_DEBUG_CATEGORY_INIT (gst_omx_video_enc_debug_category, "omxvideoenc", 0, \
      "debug category for gst-omx video encoder base class"); \
  G_IMPLEMENT_INTERFACE (GST_TYPE_PRESET, NULL); \
}

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstOMXVideoEnc, gst_omx_video_enc,
    GST_TYPE_VIDEO_ENCODER, do_init);

static void
gst_omx_video_enc_class_init (GstOMXVideoEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *video_encoder_class = GST_VIDEO_ENCODER_CLASS (klass);


  gobject_class->finalize = gst_omx_video_enc_finalize;
  gobject_class->set_property = gst_omx_video_enc_set_property;
  gobject_class->get_property = gst_omx_video_enc_get_property;

  g_object_class_install_property (gobject_class, PROP_CONTROL_RATE,
      g_param_spec_enum ("control-rate", "Control Rate",
          "Bitrate control method",
          GST_TYPE_OMX_VIDEO_ENC_CONTROL_RATE,
          GST_OMX_VIDEO_ENC_CONTROL_RATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_TARGET_BITRATE,
      g_param_spec_uint ("target-bitrate", "Target Bitrate",
          "Target bitrate in bits per second (0xffffffff=component default)",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_TARGET_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  g_object_class_install_property (gobject_class, PROP_QUANT_I_FRAMES,
      g_param_spec_uint ("quant-i-frames", "I-Frame Quantization",
          "Quantization parameter for I-frames (0xffffffff=component default)",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_QUANT_P_FRAMES,
      g_param_spec_uint ("quant-p-frames", "P-Frame Quantization",
          "Quantization parameter for P-frames (0xffffffff=component default)",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_QUANT_B_FRAMES,
      g_param_spec_uint ("quant-b-frames", "B-Frame Quantization",
          "Quantization parameter for B-frames (0xffffffff=component default)",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_QUANT_B_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  g_object_class_install_property (gobject_class, PROP_QP_MODE,
      g_param_spec_enum ("qp-mode", "QP mode",
          "QP control mode used by the VCU encoder",
          GST_TYPE_OMX_VIDEO_ENC_QP_MODE,
          GST_OMX_VIDEO_ENC_QP_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_MIN_QP,
      g_param_spec_uint ("min-qp", "min Quantization value",
          "Minimum QP value allowed for the rate control",
          0, 51, GST_OMX_VIDEO_ENC_MIN_QP_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_MAX_QP,
      g_param_spec_uint ("max-qp", "max Quantization value",
          "Maximum QP value allowed for the rate control",
          0, 51, GST_OMX_VIDEO_ENC_MAX_QP_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_GOP_MODE,
      g_param_spec_enum ("gop-mode", "GOP mode",
          "Group Of Pictures mode",
          GST_TYPE_OMX_VIDEO_ENC_GOP_MODE,
          GST_OMX_VIDEO_ENC_GOP_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_GDR_MODE,
      g_param_spec_enum ("gdr-mode", "GDR mode",
          "Gradual Decoder Refresh scheme mode. Only used if gop-mode=low-delay-p",
          GST_TYPE_OMX_VIDEO_ENC_GDR_MODE,
          GST_OMX_VIDEO_ENC_GDR_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_INITIAL_DELAY,
      g_param_spec_uint ("initial-delay", "Initial Delay",
          "The initial removal delay as specified in the HRD model in msec. "
          "Not used when control-rate=disable",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_INITIAL_DELAY_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_CPB_SIZE,
      g_param_spec_uint ("cpb-size", "CPB size",
          "Coded Picture Buffer as specified in the HRD model in msec. "
          "Not used when control-rate=disable",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_CPB_SIZE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_SCALING_LIST,
      g_param_spec_enum ("scaling-list", "Scaling List",
          "Scaling list mode",
          GST_TYPE_OMX_VIDEO_ENC_SCALING_LIST,
          GST_OMX_VIDEO_ENC_SCALING_LIST_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_LOW_BANDWIDTH,
      g_param_spec_boolean ("low-bandwidth", "Low bandwidth mode",
          "If enabled, decrease the vertical search range "
          "used for P-frame motion estimation to reduce the bandwidth",
          GST_OMX_VIDEO_ENC_LOW_BANDWIDTH_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_MAX_BITRATE,
      g_param_spec_uint ("max-bitrate", "Max Bitrate",
          "Max bitrate in bits per second, only used if control-rate=variable (0xffffffff=component default)",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_MAX_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_MAX_QUALITY,
      g_param_spec_uint ("max-quality-target", "Max Quality",
          "Cap quality at a certain high limit, keeping bitrate variable (Valid range: 0 to 20, 20 = lossless quality)"
          ", only used if control-rate=capped-variable",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_MAX_QUALITY_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_ASPECT_RATIO,
      g_param_spec_enum ("aspect-ratio", "Aspect ratio",
          "Display aspect ratio of the video sequence to be written in SPS/VUI",
          GST_TYPE_OMX_VIDEO_ENC_ASPECT_RATIO,
          GST_OMX_VIDEO_ENC_ASPECT_RATIO_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_FILLER_DATA,
      g_param_spec_boolean ("filler-data", "Filler Data",
          "Enable/Disable Filler Data NAL units for CBR rate control",
          GST_OMX_VIDEO_ENC_FILLER_DATA_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_NUM_SLICES,
      g_param_spec_uint ("num-slices", "Number of slices",
          "Number of slices produced for each frame. Each slice contains one or more complete macroblock/CTU row(s). "
          "Slices are distributed over the frame as regularly as possible. If slice-size is defined as well more slices "
          "may be produced to fit the slice-size requirement (0xffffffff=component default)",
          1, G_MAXUINT, GST_OMX_VIDEO_ENC_NUM_SLICES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_SLICE_SIZE,
      g_param_spec_uint ("slice-size", "Target slice size",
          "Target slice size (in bytes) that the encoder uses to "
          "automatically split the bitstream into approximately equally-sized slices",
          0, 65535, GST_OMX_VIDEO_ENC_SLICE_SIZE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_DEPENDENT_SLICE,
      g_param_spec_boolean ("dependent-slice", "Dependent slice",
          "If encoding with multiple slices, specify whether the additional slices are "
          "dependent slice segments or regular slices",
          GST_OMX_VIDEO_ENC_DEPENDENT_SLICE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEFAULT_ROI_QUALITY,
      g_param_spec_enum ("default-roi-quality", "Default ROI Qualtiy",
          "The default quality level to apply to each Region of Interest",
          GST_TYPE_OMX_VIDEO_ENC_ROI_QUALITY,
          GST_OMX_VIDEO_ENC_DEFAULT_ROI_QUALITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PREFETCH_BUFFER,
      g_param_spec_boolean ("prefetch-buffer", "L2Cache buffer",
          "Enable/Disable L2Cache buffer in encoding process",
          GST_OMX_VIDEO_ENC_PREFETCH_BUFFER_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_LATENCY_MODE,
      g_param_spec_enum ("latency-mode", "latency mode",
          "DEPRECATED: " LATENCY_MODE_DEPRECATION_MESSAGE,
          GST_TYPE_OMX_VIDEO_ENC_LATENCY_MODE,
          GST_OMX_VIDEO_ENC_LATENCY_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LONGTERM_REF,
      g_param_spec_boolean ("long-term-ref", "LongTerm Reference Pictures",
          "If enabled, encoder accepts dynamically inserting and using long-term reference "
          "picture events from upstream elements",
          GST_OMX_VIDEO_ENC_LONGTERM_REF_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_LONGTERM_FREQUENCY,
      g_param_spec_uint ("long-term-freq", "LongTerm reference frequency",
          "Periodicity of LongTerm reference picture marking in encoding process "
          "Units in frames, distance between two consequtive long-term reference pictures",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_LONGTERM_REF_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_LOOK_AHEAD,
      g_param_spec_uint ("look-ahead", "look ahead size",
          "The number of frames processed ahead of second pass encoding. If smaller than 2, dual pass encoding is disabled",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_LOOK_AHEAD_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_SKIP_FRAME,
      g_param_spec_boolean ("skip-frame", "Skip Frame",
          "If enabled and encoded picture exceeds the CPB buffer size, that specific picture is discarded and replaced by a picture with all MB/CTB encoded as Skip. "
          "Only used if control-rate=constant or variable and b-frames are less than 2",
          GST_OMX_VIDEO_ENC_SKIP_FRAME_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_MAX_PICTURE_SIZE,
      g_param_spec_uint ("max-picture-size", "maximum picture size",
          "Maximum picture size in Kbits, encoded picture size (I, P, and B type) will be limited to max-picture-size value. "
          "If set it to 0 then max-picture-size will not have any effect",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_MAX_PICTURE_SIZE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_MAX_PICTURE_SIZES,
      gst_param_spec_array ("max-picture-sizes",
          "Max picture size for I,P and B frames",
          "Max picture sizes baed on frame types ('<I, P, B>') "
          "Maximum picture size of I,P and B frames in Kbits, encoded picture size will be limited to max-picture-size-x value. "
          "If set it to 0 then max-picture-size-x will not have any effect",
          g_param_spec_int ("max-picture-size-x", "max picture size value",
              "One of I, P, or B frame's max picture size value", 0, G_MAXINT,
              0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
#endif

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_omx_video_enc_change_state);

  video_encoder_class->open = GST_DEBUG_FUNCPTR (gst_omx_video_enc_open);
  video_encoder_class->close = GST_DEBUG_FUNCPTR (gst_omx_video_enc_close);
  video_encoder_class->start = GST_DEBUG_FUNCPTR (gst_omx_video_enc_start);
  video_encoder_class->stop = GST_DEBUG_FUNCPTR (gst_omx_video_enc_stop);
  video_encoder_class->flush = GST_DEBUG_FUNCPTR (gst_omx_video_enc_flush);
  video_encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_video_enc_set_format);
  video_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_omx_video_enc_handle_frame);
  video_encoder_class->finish = GST_DEBUG_FUNCPTR (gst_omx_video_enc_finish);
  video_encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_omx_video_enc_propose_allocation);
  video_encoder_class->getcaps = GST_DEBUG_FUNCPTR (gst_omx_video_enc_getcaps);
  video_encoder_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_omx_video_enc_sink_event);
  video_encoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_omx_video_enc_decide_allocation);

  klass->cdata.type = GST_OMX_COMPONENT_TYPE_FILTER;
  klass->cdata.default_sink_template_caps =
      GST_VIDEO_CAPS_MAKE (GST_OMX_VIDEO_ENC_SUPPORTED_FORMATS);

  klass->handle_output_frame =
      GST_DEBUG_FUNCPTR (gst_omx_video_enc_handle_output_frame);
}

static void
gst_omx_video_enc_init (GstOMXVideoEnc * self)
{
  self->control_rate = GST_OMX_VIDEO_ENC_CONTROL_RATE_DEFAULT;
  self->target_bitrate = GST_OMX_VIDEO_ENC_TARGET_BITRATE_DEFAULT;
  self->quant_i_frames = GST_OMX_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT;
  self->quant_p_frames = GST_OMX_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT;
  self->quant_b_frames = GST_OMX_VIDEO_ENC_QUANT_B_FRAMES_DEFAULT;
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  self->qp_mode = GST_OMX_VIDEO_ENC_QP_MODE_DEFAULT;
  self->min_qp = GST_OMX_VIDEO_ENC_MIN_QP_DEFAULT;
  self->max_qp = GST_OMX_VIDEO_ENC_MAX_QP_DEFAULT;
  self->gop_mode = GST_OMX_VIDEO_ENC_GOP_MODE_DEFAULT;
  self->gdr_mode = GST_OMX_VIDEO_ENC_GDR_MODE_DEFAULT;
  self->initial_delay = GST_OMX_VIDEO_ENC_INITIAL_DELAY_DEFAULT;
  self->cpb_size = GST_OMX_VIDEO_ENC_CPB_SIZE_DEFAULT;
  self->scaling_list = GST_OMX_VIDEO_ENC_SCALING_LIST_DEFAULT;
  self->low_bandwidth = GST_OMX_VIDEO_ENC_LOW_BANDWIDTH_DEFAULT;
  self->max_bitrate = GST_OMX_VIDEO_ENC_MAX_BITRATE_DEFAULT;
  self->max_quality_target = GST_OMX_VIDEO_ENC_MAX_QUALITY_DEFAULT;
  self->aspect_ratio = GST_OMX_VIDEO_ENC_ASPECT_RATIO_DEFAULT;
  self->filler_data = GST_OMX_VIDEO_ENC_FILLER_DATA_DEFAULT;
  self->num_slices = GST_OMX_VIDEO_ENC_NUM_SLICES_DEFAULT;
  self->slice_size = GST_OMX_VIDEO_ENC_SLICE_SIZE_DEFAULT;
  self->dependent_slice = GST_OMX_VIDEO_ENC_DEPENDENT_SLICE_DEFAULT;
  self->default_roi_quality = GST_OMX_VIDEO_ENC_DEFAULT_ROI_QUALITY;
  self->prefetch_buffer = GST_OMX_VIDEO_ENC_PREFETCH_BUFFER_DEFAULT;
  self->latency_mode = GST_OMX_VIDEO_ENC_LATENCY_MODE_DEFAULT;
  self->long_term_ref = GST_OMX_VIDEO_ENC_LONGTERM_REF_DEFAULT;
  self->long_term_freq = GST_OMX_VIDEO_ENC_LONGTERM_FREQUENCY_DEFAULT;
  self->look_ahead = GST_OMX_VIDEO_ENC_LOOK_AHEAD_DEFAULT;
  self->skip_frame = GST_OMX_VIDEO_ENC_SKIP_FRAME_DEFAULT;
  self->max_picture_size = GST_OMX_VIDEO_ENC_MAX_PICTURE_SIZE_DEFAULT;
  self->max_picture_size_i = GST_OMX_VIDEO_ENC_MAX_PICTURE_SIZE_I_DEFAULT;
  self->max_picture_size_p = GST_OMX_VIDEO_ENC_MAX_PICTURE_SIZE_P_DEFAULT;
  self->max_picture_size_b = GST_OMX_VIDEO_ENC_MAX_PICTURE_SIZE_B_DEFAULT;
#endif

  self->default_target_bitrate = GST_OMX_PROP_OMX_DEFAULT;

  g_mutex_init (&self->drain_lock);
  g_cond_init (&self->drain_cond);

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  self->alg_roi_quality_enum_class =
      g_type_class_ref (GST_TYPE_OMX_VIDEO_ENC_ROI_QUALITY);
#endif
}

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS

#define CHECK_ERR(setting) \
  if (err == OMX_ErrorUnsupportedIndex || err == OMX_ErrorUnsupportedSetting) { \
    GST_WARNING_OBJECT (self, \
        "Setting " setting " parameters not supported by the component"); \
  } else if (err != OMX_ErrorNone) { \
    GST_ERROR_OBJECT (self, \
        "Failed to set " setting " parameters: %s (0x%08x)", \
        gst_omx_error_to_string (err), err); \
    return FALSE; \
  }

static gboolean
gst_omx_video_enc_set_color_primaries (GstOMXVideoEnc * self)
{
  OMX_ALG_VIDEO_PARAM_COLOR_PRIMARIES param;
  GstVideoInfo *info = &self->input_state->info;
  GstVideoColorimetry cinfo = GST_VIDEO_INFO_COLORIMETRY (info);
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = self->enc_in_port->index;

  switch (cinfo.primaries) {
    case GST_VIDEO_COLOR_PRIMARIES_BT709:
      param.eColorPrimaries = OMX_ALG_VIDEO_COLOR_PRIMARIES_BT_709;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_BT470M:
      param.eColorPrimaries = OMX_ALG_VIDEO_COLOR_PRIMARIES_BT_470_NTSC;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_BT470BG:
      param.eColorPrimaries = OMX_ALG_VIDEO_COLOR_PRIMARIES_BT_601_PAL;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTE170M:
      param.eColorPrimaries = OMX_ALG_VIDEO_COLOR_PRIMARIES_BT_601_NTSC;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTE240M:
      param.eColorPrimaries = OMX_ALG_VIDEO_COLOR_PRIMARIES_SMPTE_240M;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_FILM:
      param.eColorPrimaries = OMX_ALG_VIDEO_COLOR_PRIMARIES_GENERIC_FILM;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_BT2020:
      param.eColorPrimaries = OMX_ALG_VIDEO_COLOR_PRIMARIES_BT_2020;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTEST428:
      param.eColorPrimaries = OMX_ALG_VIDEO_COLOR_PRIMARIES_SMPTE_ST_428;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTERP431:
      param.eColorPrimaries = OMX_ALG_VIDEO_COLOR_PRIMARIES_SMPTE_RP_431;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTEEG432:
      param.eColorPrimaries = OMX_ALG_VIDEO_COLOR_PRIMARIES_SMPTE_EG_432;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_ADOBERGB:
    case GST_VIDEO_COLOR_PRIMARIES_UNKNOWN:
    default:
      GST_WARNING_OBJECT (self,
          "Provided color primaries %d (%s) are not supported", cinfo.primaries,
          gst_video_colorimetry_to_string (&cinfo));
      param.eColorPrimaries = OMX_ALG_VIDEO_COLOR_PRIMARIES_UNSPECIFIED;
  }

  GST_DEBUG_OBJECT (self, "Set color primaries to %d (%s)",
      param.eColorPrimaries, gst_video_colorimetry_to_string (&cinfo));

  err =
      gst_omx_component_set_parameter (self->enc,
      (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoColorPrimaries, &param);
  CHECK_ERR ("color-primaries");

  return TRUE;
}

static gboolean
set_zynqultrascaleplus_props (GstOMXVideoEnc * self)
{
  OMX_ERRORTYPE err;
  OMX_ALG_VIDEO_PARAM_QUANTIZATION_CONTROL quant;
  OMX_ALG_VIDEO_PARAM_QUANTIZATION_TABLE quant_table;

  if (self->qp_mode != GST_OMX_VIDEO_ENC_QP_MODE_DEFAULT) {
    guint32 qp_mode = OMX_ALG_QP_CTRL_NONE;
    guint32 qp_table = OMX_ALG_QP_TABLE_NONE;

    /* qp_mode should be mapped to combination QUANTIZATION_CONTROL & QUANTIZATION_TABLE Params */
    switch (self->qp_mode) {
      case UNIFORM_QP:
        qp_mode = OMX_ALG_QP_CTRL_NONE;
        qp_table = OMX_ALG_QP_TABLE_NONE;
        break;
      case AUTO_QP:
        qp_mode = OMX_ALG_QP_CTRL_AUTO;
        qp_table = OMX_ALG_QP_TABLE_NONE;
        break;
      case ROI_QP:
        qp_mode = OMX_ALG_QP_CTRL_NONE;
        qp_table = OMX_ALG_QP_TABLE_RELATIVE;
        break;
      case LOAD_QP_ABSOLUTE:
        qp_mode = OMX_ALG_QP_CTRL_NONE;
        qp_table = OMX_ALG_QP_TABLE_ABSOLUTE;
        break;
      case LOAD_QP_RELATIVE:
        qp_mode = OMX_ALG_QP_CTRL_NONE;
        qp_table = OMX_ALG_QP_TABLE_RELATIVE;
        break;
      default:
        GST_WARNING_OBJECT (self,
            "Invalid option. Falling back to Uniform mode");
        break;
    }

    GST_OMX_INIT_STRUCT (&quant);
    quant.nPortIndex = self->enc_out_port->index;
    quant.eQpControlMode = qp_mode;

    GST_DEBUG_OBJECT (self, "setting QP mode to %d", qp_mode);

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoQuantizationControl, &quant);
    CHECK_ERR ("quantization");

    GST_OMX_INIT_STRUCT (&quant_table);
    quant_table.nPortIndex = self->enc_out_port->index;
    quant_table.eQpTableMode = qp_table;

    GST_DEBUG_OBJECT (self, "setting QP Table Mode to %d", qp_table);

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoQuantizationTable, &quant_table);
    CHECK_ERR ("quantization table");
  }

  {
    OMX_ALG_VIDEO_PARAM_QUANTIZATION_EXTENSION qp_values;

    GST_OMX_INIT_STRUCT (&qp_values);
    qp_values.nPortIndex = self->enc_out_port->index;
    qp_values.nQpMin = self->min_qp;
    qp_values.nQpMax = self->max_qp;

    GST_DEBUG_OBJECT (self, "setting min QP as %d and max QP as %d",
        self->min_qp, self->max_qp);

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoQuantizationExtension,
        &qp_values);
    CHECK_ERR ("min-qp and max-qp");
  }

  {
    OMX_ALG_VIDEO_PARAM_GOP_CONTROL gop_mode;

    if (self->gdr_mode != OMX_ALG_GDR_OFF &&
        self->gop_mode != OMX_ALG_GOP_MODE_LOW_DELAY_P) {
      GST_ERROR_OBJECT (self,
          "gdr-mode mode only can be set if gop-mode=low-delay-p");
      return FALSE;
    }

    GST_OMX_INIT_STRUCT (&gop_mode);
    gop_mode.nPortIndex = self->enc_out_port->index;
    gop_mode.eGopControlMode = self->gop_mode;
    gop_mode.eGdrMode = self->gdr_mode;

    GST_DEBUG_OBJECT (self, "setting GOP mode to %d and GDR mode to %d",
        self->gop_mode, self->gdr_mode);

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoGopControl, &gop_mode);
    CHECK_ERR ("GOP & GDR");
  }

  if (self->control_rate != OMX_Video_ControlRateDisable) {
    if (self->cpb_size < self->initial_delay) {
      GST_ERROR_OBJECT (self,
          "cpb-size (%d) cannot be smaller than initial-delay (%d)",
          self->cpb_size, self->initial_delay);
      g_critical ("cpb-size (%d) cannot be smaller than initial-delay (%d)",
          self->cpb_size, self->initial_delay);
    } else {
      OMX_ALG_VIDEO_PARAM_CODED_PICTURE_BUFFER cpb;

      GST_OMX_INIT_STRUCT (&cpb);
      cpb.nPortIndex = self->enc_out_port->index;
      cpb.nCodedPictureBufferSize = self->cpb_size;
      cpb.nInitialRemovalDelay = self->initial_delay;

      GST_DEBUG_OBJECT (self, "setting cpb size to %d and initial delay to %d",
          self->cpb_size, self->initial_delay);

      err =
          gst_omx_component_set_parameter (self->enc,
          (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoCodedPictureBuffer, &cpb);
      CHECK_ERR ("cpb size & initial delay");
    }
  }

  {
    OMX_ALG_VIDEO_PARAM_SCALING_LIST scaling_list;

    GST_OMX_INIT_STRUCT (&scaling_list);
    scaling_list.nPortIndex = self->enc_out_port->index;
    scaling_list.eScalingListMode = self->scaling_list;

    GST_DEBUG_OBJECT (self, "setting scaling list mode as %d",
        self->scaling_list);

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoScalingList, &scaling_list);
    CHECK_ERR ("scaling-list");
  }

  {
    OMX_ALG_VIDEO_PARAM_LOW_BANDWIDTH low_bw;

    GST_OMX_INIT_STRUCT (&low_bw);
    low_bw.nPortIndex = self->enc_out_port->index;
    low_bw.bEnableLowBandwidth = self->low_bandwidth;

    GST_DEBUG_OBJECT (self, "%s low bandwith moded",
        self->low_bandwidth ? "Enable" : "Disable");

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoLowBandwidth, &low_bw);
    CHECK_ERR ("low-bandwidth");
  }

  {
    OMX_ALG_VIDEO_PARAM_MAX_BITRATE max_bitrate;

    GST_OMX_INIT_STRUCT (&max_bitrate);
    max_bitrate.nPortIndex = self->enc_out_port->index;
    /* nMaxBitrate is in kbps while max-bitrate is in bps */
    err = gst_omx_component_get_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoMaxBitrate, &max_bitrate);
    if (err != OMX_ErrorNone) {
      GST_WARNING_OBJECT (self,
          "Error getting max-bitrate parameters: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      return FALSE;
    }

    if (self->max_bitrate != GST_OMX_VIDEO_ENC_MAX_BITRATE_DEFAULT) {
      if (self->control_rate == OMX_ALG_Video_ControlRateVariableCapped ||
          self->control_rate == OMX_Video_ControlRateVariable) {
        max_bitrate.nMaxBitrate = self->max_bitrate;
        GST_DEBUG_OBJECT (self, "setting max bitrate to %d", self->max_bitrate);
      } else {
        GST_ERROR_OBJECT (self,
            "max-bitrate is only used with variable and capped-variable control-rate");
        return FALSE;
      }
    }

    if (self->max_quality_target != GST_OMX_VIDEO_ENC_MAX_QUALITY_DEFAULT) {
      if (self->control_rate == OMX_ALG_Video_ControlRateVariableCapped) {
        if ((self->max_quality_target < 0) || (self->max_quality_target > 20)) {
          GST_ERROR_OBJECT (self,
              "Invalid value for max-quality-target(Valid range: 0 to 20)");
          return FALSE;
        } else {
          max_bitrate.nMaxQuality = self->max_quality_target;
          GST_DEBUG_OBJECT (self, "setting max quality target to %d",
              self->max_quality_target);
        }
      } else {
        GST_ERROR_OBJECT (self,
            "max-quality-target is only used with capped-variable control-rate");
        return FALSE;
      }
    }

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoMaxBitrate, &max_bitrate);
    CHECK_ERR ("max-bitrate");
  }

  {
    OMX_ALG_VIDEO_PARAM_ASPECT_RATIO aspect_ratio;

    GST_OMX_INIT_STRUCT (&aspect_ratio);
    aspect_ratio.nPortIndex = self->enc_out_port->index;
    aspect_ratio.eAspectRatio = self->aspect_ratio;

    GST_DEBUG_OBJECT (self, "setting aspect ratio to %d", self->aspect_ratio);

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoAspectRatio, &aspect_ratio);
    CHECK_ERR ("aspect-ratio");
  }

  {
    OMX_ALG_VIDEO_PARAM_FILLER_DATA filler_data;

    GST_OMX_INIT_STRUCT (&filler_data);
    filler_data.nPortIndex = self->enc_out_port->index;
    filler_data.bDisableFillerData = !(self->filler_data);

    GST_DEBUG_OBJECT (self, "%s filler data",
        self->filler_data ? "Enable" : "Disable");

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoFillerData, &filler_data);
    CHECK_ERR ("filler-data");
  }

  if (self->num_slices != GST_OMX_VIDEO_ENC_NUM_SLICES_DEFAULT ||
      self->slice_size != GST_OMX_VIDEO_ENC_SLICE_SIZE_DEFAULT) {
    OMX_ALG_VIDEO_PARAM_SLICES slices;

    GST_OMX_INIT_STRUCT (&slices);
    slices.nPortIndex = self->enc_out_port->index;

    err = gst_omx_component_get_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoSlices, &slices);
    if (err != OMX_ErrorNone) {
      GST_WARNING_OBJECT (self, "Error getting slice parameters: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      return FALSE;
    }

    if (self->num_slices != GST_OMX_VIDEO_ENC_NUM_SLICES_DEFAULT) {
      slices.nNumSlices = self->num_slices;
      GST_DEBUG_OBJECT (self,
          "setting number of slices to %d (dependent slices: %d)",
          self->num_slices, self->dependent_slice);
    }

    if (self->slice_size != GST_OMX_VIDEO_ENC_SLICE_SIZE_DEFAULT) {
      slices.nSlicesSize = self->slice_size;
      GST_DEBUG_OBJECT (self, "setting slice size to %d (dependent slices: %d)",
          self->slice_size, self->dependent_slice);
    }

    slices.bDependentSlices = self->dependent_slice;

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoSlices, &slices);
    CHECK_ERR ("slices");
  }

  {
    OMX_ALG_VIDEO_PARAM_PREFETCH_BUFFER prefetch_buffer;

    GST_OMX_INIT_STRUCT (&prefetch_buffer);
    prefetch_buffer.nPortIndex = self->enc_out_port->index;
    prefetch_buffer.bEnablePrefetchBuffer = self->prefetch_buffer;

    GST_DEBUG_OBJECT (self, "setting prefetch buffer to %d",
        self->prefetch_buffer);

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoPrefetchBuffer,
        &prefetch_buffer);
    CHECK_ERR ("prefetch");
  }

  if (self->latency_mode != GST_OMX_VIDEO_ENC_LATENCY_MODE_DEFAULT) {
    OMX_ALG_VIDEO_PARAM_SUBFRAME subframe_mode;
    GST_OMX_INIT_STRUCT (&subframe_mode);
    subframe_mode.nPortIndex = self->enc_out_port->index;
    subframe_mode.bEnableSubframe = self->latency_mode;

    GST_DEBUG_OBJECT (self, "setting latency mode to %d", self->latency_mode);

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoSubframe, &subframe_mode);
    CHECK_ERR ("latency mode");
  }

  {
    OMX_ALG_VIDEO_PARAM_LONG_TERM longterm;
    GST_OMX_INIT_STRUCT (&longterm);
    longterm.nPortIndex = self->enc_out_port->index;
    longterm.bEnableLongTerm = self->long_term_ref;
    longterm.nLongTermFrequency = self->long_term_freq;

    GST_DEBUG_OBJECT (self, "setting long-term ref to %d, long-term-freq to %d",
        self->long_term_ref, self->long_term_freq);

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoLongTerm, &longterm);
    CHECK_ERR ("longterm");
  }

  {
    OMX_ALG_VIDEO_PARAM_LOOKAHEAD look_ahead;

    GST_OMX_INIT_STRUCT (&look_ahead);
    look_ahead.nPortIndex = self->enc_in_port->index;
    look_ahead.nLookAhead = self->look_ahead;

    GST_DEBUG_OBJECT (self, "setting look_ahead to %d", self->look_ahead);

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoLookAhead, &look_ahead);
    CHECK_ERR ("look-ahead");
  }

  if (self->skip_frame != GST_OMX_VIDEO_ENC_SKIP_FRAME_DEFAULT) {
    OMX_ALG_VIDEO_PARAM_SKIP_FRAME skip_frame;
    if (self->control_rate != OMX_Video_ControlRateConstant &&
        self->control_rate != OMX_Video_ControlRateVariable) {
      GST_ERROR_OBJECT (self,
          "skip-frame is only used with Variable and Constant control-rate");
      return FALSE;
    }

    GST_OMX_INIT_STRUCT (&skip_frame);
    skip_frame.nPortIndex = self->enc_out_port->index;
    skip_frame.bEnableSkipFrame = self->skip_frame;

    GST_DEBUG_OBJECT (self, "%s skip frame",
        self->skip_frame ? "Enable" : "Disable");

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoSkipFrame, &skip_frame);
    CHECK_ERR ("skip-frame");
  }

  if (self->max_picture_size != GST_OMX_VIDEO_ENC_MAX_PICTURE_SIZE_DEFAULT) {
    OMX_ALG_VIDEO_PARAM_MAX_PICTURE_SIZE max_picture_size;

    GST_OMX_INIT_STRUCT (&max_picture_size);
    max_picture_size.nPortIndex = self->enc_out_port->index;
    max_picture_size.nMaxPictureSize = self->max_picture_size;

    GST_DEBUG_OBJECT (self, "max_picture_size is set to %d",
        self->max_picture_size);

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoMaxPictureSize,
        &max_picture_size);
    CHECK_ERR ("max-picture-size");
  }

  if (self->max_picture_size_i != GST_OMX_VIDEO_ENC_MAX_PICTURE_SIZE_I_DEFAULT
      || self->max_picture_size_p !=
      GST_OMX_VIDEO_ENC_MAX_PICTURE_SIZE_P_DEFAULT
      || self->max_picture_size_b !=
      GST_OMX_VIDEO_ENC_MAX_PICTURE_SIZE_B_DEFAULT) {
    OMX_ALG_VIDEO_PARAM_MAX_PICTURE_SIZES max_picture_sizes;

    GST_OMX_INIT_STRUCT (&max_picture_sizes);
    max_picture_sizes.nPortIndex = self->enc_out_port->index;
    max_picture_sizes.nMaxPictureSizeI = self->max_picture_size_i;
    max_picture_sizes.nMaxPictureSizeP = self->max_picture_size_p;
    max_picture_sizes.nMaxPictureSizeB = self->max_picture_size_b;

    GST_DEBUG_OBJECT (self,
        "setting max_picture_size_i=%d, max_picture_size_p=%d, max_picture_size_b=%d",
        self->max_picture_size_i, self->max_picture_size_p,
        self->max_picture_size_b);

    err =
        gst_omx_component_set_parameter (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoMaxPictureSizes,
        &max_picture_sizes);
    CHECK_ERR ("max-picture-sizes");
  }

  return TRUE;
}
#endif

static gboolean
gst_omx_video_enc_set_bitrate (GstOMXVideoEnc * self)
{
  OMX_ERRORTYPE err;
  OMX_VIDEO_PARAM_BITRATETYPE bitrate_param;
  gboolean result = TRUE;

  GST_OBJECT_LOCK (self);

  GST_OMX_INIT_STRUCT (&bitrate_param);
  bitrate_param.nPortIndex = self->enc_out_port->index;

  err = gst_omx_component_get_parameter (self->enc,
      OMX_IndexParamVideoBitrate, &bitrate_param);

  if (err == OMX_ErrorNone) {
#ifdef USE_OMX_TARGET_RPI
    /* FIXME: Workaround for RPi returning garbage for this parameter */
    if (bitrate_param.nVersion.nVersion == 0) {
      GST_OMX_INIT_STRUCT (&bitrate_param);
      bitrate_param.nPortIndex = self->enc_out_port->index;
    }
#endif
    if (self->default_target_bitrate == GST_OMX_PROP_OMX_DEFAULT)
      /* Save the actual OMX default so we can restore it if needed */
      self->default_target_bitrate = bitrate_param.nTargetBitrate;

    if (self->control_rate != 0xffffffff)
      bitrate_param.eControlRate = self->control_rate;
    if (self->target_bitrate != 0xffffffff)
      bitrate_param.nTargetBitrate = self->target_bitrate;
    else
      bitrate_param.nTargetBitrate = self->default_target_bitrate;

    err =
        gst_omx_component_set_parameter (self->enc,
        OMX_IndexParamVideoBitrate, &bitrate_param);
    if (err == OMX_ErrorUnsupportedIndex) {
      GST_WARNING_OBJECT (self,
          "Setting a bitrate not supported by the component");
    } else if (err == OMX_ErrorUnsupportedSetting) {
      GST_WARNING_OBJECT (self,
          "Setting bitrate settings %u %u not supported by the component",
          self->control_rate, self->target_bitrate);
    } else if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self,
          "Failed to set bitrate parameters: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      result = FALSE;
    }
  } else {
    GST_ERROR_OBJECT (self, "Failed to get bitrate parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }

  GST_OBJECT_UNLOCK (self);
  return result;
}

static gboolean
gst_omx_video_enc_open (GstVideoEncoder * encoder)
{
  GstOMXVideoEnc *self = GST_OMX_VIDEO_ENC (encoder);
  GstOMXVideoEncClass *klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);
  gint in_port_index, out_port_index;

  self->enc =
      gst_omx_component_new (GST_OBJECT_CAST (self), klass->cdata.core_name,
      klass->cdata.component_name, klass->cdata.component_role,
      klass->cdata.hacks);
  self->started = FALSE;

  if (!self->enc)
    return FALSE;

  if (gst_omx_component_get_state (self->enc,
          GST_CLOCK_TIME_NONE) != OMX_StateLoaded)
    return FALSE;

  in_port_index = klass->cdata.in_port_index;
  out_port_index = klass->cdata.out_port_index;

  if (in_port_index == -1 || out_port_index == -1) {
    OMX_PORT_PARAM_TYPE param;
    OMX_ERRORTYPE err;

    GST_OMX_INIT_STRUCT (&param);

    err =
        gst_omx_component_get_parameter (self->enc, OMX_IndexParamVideoInit,
        &param);
    if (err != OMX_ErrorNone) {
      GST_WARNING_OBJECT (self, "Couldn't get port information: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      /* Fallback */
      in_port_index = 0;
      out_port_index = 1;
    } else {
      GST_DEBUG_OBJECT (self, "Detected %u ports, starting at %u",
          (guint) param.nPorts, (guint) param.nStartPortNumber);
      in_port_index = param.nStartPortNumber + 0;
      out_port_index = param.nStartPortNumber + 1;
    }
  }

  self->enc_in_port = gst_omx_component_add_port (self->enc, in_port_index);
  self->enc_out_port = gst_omx_component_add_port (self->enc, out_port_index);

  if (!self->enc_in_port || !self->enc_out_port)
    return FALSE;

  /* Set properties */
  {
    OMX_ERRORTYPE err;

    if (!gst_omx_video_enc_set_bitrate (self))
      return FALSE;

    if (self->quant_i_frames != 0xffffffff ||
        self->quant_p_frames != 0xffffffff ||
        self->quant_b_frames != 0xffffffff) {
      OMX_VIDEO_PARAM_QUANTIZATIONTYPE quant_param;

      GST_OMX_INIT_STRUCT (&quant_param);
      quant_param.nPortIndex = self->enc_out_port->index;

      err = gst_omx_component_get_parameter (self->enc,
          OMX_IndexParamVideoQuantization, &quant_param);

      if (err == OMX_ErrorNone) {

        if (self->quant_i_frames != 0xffffffff)
          quant_param.nQpI = self->quant_i_frames;
        if (self->quant_p_frames != 0xffffffff)
          quant_param.nQpP = self->quant_p_frames;
        if (self->quant_b_frames != 0xffffffff)
          quant_param.nQpB = self->quant_b_frames;

        err =
            gst_omx_component_set_parameter (self->enc,
            OMX_IndexParamVideoQuantization, &quant_param);
        if (err == OMX_ErrorUnsupportedIndex) {
          GST_WARNING_OBJECT (self,
              "Setting quantization parameters not supported by the component");
        } else if (err == OMX_ErrorUnsupportedSetting) {
          GST_WARNING_OBJECT (self,
              "Setting quantization parameters %u %u %u not supported by the component",
              self->quant_i_frames, self->quant_p_frames, self->quant_b_frames);
        } else if (err != OMX_ErrorNone) {
          GST_ERROR_OBJECT (self,
              "Failed to set quantization parameters: %s (0x%08x)",
              gst_omx_error_to_string (err), err);
          return FALSE;
        }
      } else {
        GST_ERROR_OBJECT (self,
            "Failed to get quantization parameters: %s (0x%08x)",
            gst_omx_error_to_string (err), err);

      }
    }
  }
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  if (!set_zynqultrascaleplus_props (self))
    return FALSE;
#endif

  return TRUE;
}

static gboolean
gst_omx_video_enc_deallocate_in_buffers (GstOMXVideoEnc * self)
{
  /* Pool will take care of deallocating buffers when deactivated upstream */
  if (!self->in_pool_used
      && gst_omx_port_deallocate_buffers (self->enc_in_port) != OMX_ErrorNone)
    return FALSE;

  return TRUE;
}

static gboolean
gst_omx_video_enc_shutdown (GstOMXVideoEnc * self)
{
  OMX_STATETYPE state;

  GST_DEBUG_OBJECT (self, "Shutting down encoder");

  state = gst_omx_component_get_state (self->enc, 0);
  if (state > OMX_StateLoaded || state == OMX_StateInvalid) {
    if (state > OMX_StateIdle) {
      gst_omx_component_set_state (self->enc, OMX_StateIdle);
      gst_omx_component_get_state (self->enc, 5 * GST_SECOND);
    }
    gst_omx_component_set_state (self->enc, OMX_StateLoaded);
    gst_omx_video_enc_deallocate_in_buffers (self);
    gst_omx_port_deallocate_buffers (self->enc_out_port);
    if (state > OMX_StateLoaded)
      gst_omx_component_get_state (self->enc, 5 * GST_SECOND);
  }

  return TRUE;
}

static gboolean
gst_omx_video_enc_close (GstVideoEncoder * encoder)
{
  GstOMXVideoEnc *self = GST_OMX_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Closing encoder");

  if (!gst_omx_video_enc_shutdown (self))
    return FALSE;

  self->enc_in_port = NULL;
  self->enc_out_port = NULL;
  if (self->enc)
    gst_omx_component_unref (self->enc);
  self->enc = NULL;

  self->started = FALSE;

  return TRUE;
}

static void
gst_omx_video_enc_finalize (GObject * object)
{
  GstOMXVideoEnc *self = GST_OMX_VIDEO_ENC (object);

  g_mutex_clear (&self->drain_lock);
  g_cond_clear (&self->drain_cond);

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  g_clear_pointer (&self->alg_roi_quality_enum_class, g_type_class_unref);
#endif

  G_OBJECT_CLASS (gst_omx_video_enc_parent_class)->finalize (object);
}

static void
gst_omx_video_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOMXVideoEnc *self = GST_OMX_VIDEO_ENC (object);

  switch (prop_id) {
    case PROP_CONTROL_RATE:
      self->control_rate = g_value_get_enum (value);
      break;
    case PROP_TARGET_BITRATE:
      GST_OBJECT_LOCK (self);
      self->target_bitrate = g_value_get_uint (value);
      if (self->enc) {
        OMX_VIDEO_CONFIG_BITRATETYPE config;
        OMX_ERRORTYPE err;

        GST_OMX_INIT_STRUCT (&config);
        config.nPortIndex = self->enc_out_port->index;
        config.nEncodeBitrate = self->target_bitrate;
        err =
            gst_omx_component_set_config (self->enc,
            OMX_IndexConfigVideoBitrate, &config);
        if (err != OMX_ErrorNone)
          GST_ERROR_OBJECT (self,
              "Failed to set bitrate parameter: %s (0x%08x)",
              gst_omx_error_to_string (err), err);
      }
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_QUANT_I_FRAMES:
      self->quant_i_frames = g_value_get_uint (value);
      break;
    case PROP_QUANT_P_FRAMES:
      self->quant_p_frames = g_value_get_uint (value);
      break;
    case PROP_QUANT_B_FRAMES:
      self->quant_b_frames = g_value_get_uint (value);
      break;
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
    case PROP_QP_MODE:
      self->qp_mode = g_value_get_enum (value);
      break;
    case PROP_MIN_QP:
      self->min_qp = g_value_get_uint (value);
      break;
    case PROP_MAX_QP:
      self->max_qp = g_value_get_uint (value);
      break;
    case PROP_GOP_MODE:
      self->gop_mode = g_value_get_enum (value);
      break;
    case PROP_GDR_MODE:
      self->gdr_mode = g_value_get_enum (value);
      break;
    case PROP_INITIAL_DELAY:
      self->initial_delay = g_value_get_uint (value);
      break;
    case PROP_CPB_SIZE:
      self->cpb_size = g_value_get_uint (value);
      break;
    case PROP_SCALING_LIST:
      self->scaling_list = g_value_get_enum (value);
      break;
    case PROP_LOW_BANDWIDTH:
      self->low_bandwidth = g_value_get_boolean (value);
      break;
    case PROP_MAX_BITRATE:
      self->max_bitrate = g_value_get_uint (value);
      break;
    case PROP_ASPECT_RATIO:
      self->aspect_ratio = g_value_get_enum (value);
      break;
    case PROP_FILLER_DATA:
      self->filler_data = g_value_get_boolean (value);
      break;
    case PROP_NUM_SLICES:
      self->num_slices = g_value_get_uint (value);
      break;
    case PROP_SLICE_SIZE:
      self->slice_size = g_value_get_uint (value);
      break;
    case PROP_DEPENDENT_SLICE:
      self->dependent_slice = g_value_get_boolean (value);
      break;
    case PROP_DEFAULT_ROI_QUALITY:
      self->default_roi_quality = g_value_get_enum (value);
      break;
    case PROP_PREFETCH_BUFFER:
      self->prefetch_buffer = g_value_get_boolean (value);
      break;
    case PROP_LATENCY_MODE:
      self->latency_mode = g_value_get_enum (value);
      g_warning ("Property 'latency-mode' is deprecated and have no effect. "
          LATENCY_MODE_DEPRECATION_MESSAGE);
      break;
    case PROP_LONGTERM_REF:
      self->long_term_ref = g_value_get_boolean (value);
      break;
    case PROP_LONGTERM_FREQUENCY:
      self->long_term_freq = g_value_get_uint (value);
      break;
    case PROP_LOOK_AHEAD:
      self->look_ahead = g_value_get_uint (value);
      break;
    case PROP_SKIP_FRAME:
      self->skip_frame = g_value_get_boolean (value);
      break;
    case PROP_MAX_PICTURE_SIZE:
      self->max_picture_size = g_value_get_uint (value);
      break;
    case PROP_MAX_PICTURE_SIZES:
    {
      const GValue *v;

      if (gst_value_array_get_size (value) != 3) {
        GST_ERROR_OBJECT (self,
            "Badly formated max-picture-sizes, must contains 3 gint");
        break;
      }

      v = gst_value_array_get_value (value, 0);
      if (!G_VALUE_HOLDS_INT (v)) {
        GST_ERROR_OBJECT (self, "max-picture-sizes for I frame is not int");
        break;
      }
      self->max_picture_size_i = g_value_get_int (v);

      v = gst_value_array_get_value (value, 1);
      if (!G_VALUE_HOLDS_INT (v)) {
        GST_ERROR_OBJECT (self, "max-picture-sizes for P frame is not int");
        break;
      }
      self->max_picture_size_p = g_value_get_int (v);

      v = gst_value_array_get_value (value, 2);
      if (!G_VALUE_HOLDS_INT (v)) {
        GST_ERROR_OBJECT (self, "max-picture-sizes for B frame is not int");
        break;
      }
      self->max_picture_size_b = g_value_get_int (v);

      break;
    }
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_video_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOMXVideoEnc *self = GST_OMX_VIDEO_ENC (object);

  switch (prop_id) {
    case PROP_CONTROL_RATE:
      g_value_set_enum (value, self->control_rate);
      break;
    case PROP_TARGET_BITRATE:
      GST_OBJECT_LOCK (self);
      g_value_set_uint (value, self->target_bitrate);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_QUANT_I_FRAMES:
      g_value_set_uint (value, self->quant_i_frames);
      break;
    case PROP_QUANT_P_FRAMES:
      g_value_set_uint (value, self->quant_p_frames);
      break;
    case PROP_QUANT_B_FRAMES:
      g_value_set_uint (value, self->quant_b_frames);
      break;
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
    case PROP_QP_MODE:
      g_value_set_enum (value, self->qp_mode);
      break;
    case PROP_MIN_QP:
      g_value_set_uint (value, self->min_qp);
      break;
    case PROP_MAX_QP:
      g_value_set_uint (value, self->max_qp);
      break;
    case PROP_GOP_MODE:
      g_value_set_enum (value, self->gop_mode);
      break;
    case PROP_GDR_MODE:
      g_value_set_enum (value, self->gdr_mode);
      break;
    case PROP_INITIAL_DELAY:
      g_value_set_uint (value, self->initial_delay);
      break;
    case PROP_CPB_SIZE:
      g_value_set_uint (value, self->cpb_size);
      break;
    case PROP_SCALING_LIST:
      g_value_set_enum (value, self->scaling_list);
      break;
    case PROP_LOW_BANDWIDTH:
      g_value_set_boolean (value, self->low_bandwidth);
      break;
    case PROP_MAX_BITRATE:
       g_value_set_uint (value, self->max_bitrate);
       break;
    case PROP_MAX_QUALITY:
      g_value_set_uint (value, self->max_quality_target);
      break;
    case PROP_ASPECT_RATIO:
      g_value_set_enum (value, self->aspect_ratio);
      break;
    case PROP_FILLER_DATA:
      g_value_set_boolean (value, self->filler_data);
      break;
    case PROP_NUM_SLICES:
      g_value_set_uint (value, self->num_slices);
      break;
    case PROP_SLICE_SIZE:
      g_value_set_uint (value, self->slice_size);
      break;
    case PROP_DEPENDENT_SLICE:
      g_value_set_boolean (value, self->dependent_slice);
      break;
    case PROP_DEFAULT_ROI_QUALITY:
      g_value_set_enum (value, self->default_roi_quality);
      break;
    case PROP_PREFETCH_BUFFER:
      g_value_set_boolean (value, self->prefetch_buffer);
      break;
    case PROP_LATENCY_MODE:
      g_value_set_enum (value, ENC_LATENCY_MODE_NORMAL);
      break;
    case PROP_LONGTERM_REF:
      g_value_set_boolean (value, self->long_term_ref);
      break;
    case PROP_LONGTERM_FREQUENCY:
      g_value_set_uint (value, self->long_term_freq);
      break;
    case PROP_LOOK_AHEAD:
      g_value_set_uint (value, self->look_ahead);
      break;
    case PROP_SKIP_FRAME:
      g_value_set_boolean (value, self->skip_frame);
      break;
    case PROP_MAX_PICTURE_SIZE:
      g_value_set_uint (value, self->max_picture_size);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_omx_video_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstOMXVideoEnc *self;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  g_return_val_if_fail (GST_IS_OMX_VIDEO_ENC (element),
      GST_STATE_CHANGE_FAILURE);
  self = GST_OMX_VIDEO_ENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->downstream_flow_ret = GST_FLOW_OK;

      self->draining = FALSE;
      self->started = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (self->enc_in_port)
        gst_omx_port_set_flushing (self->enc_in_port, 5 * GST_SECOND, TRUE);
      if (self->enc_out_port)
        gst_omx_port_set_flushing (self->enc_out_port, 5 * GST_SECOND, TRUE);

      g_mutex_lock (&self->drain_lock);
      self->draining = FALSE;
      g_cond_broadcast (&self->drain_cond);
      g_mutex_unlock (&self->drain_lock);
      break;
    default:
      break;
  }

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  ret =
      GST_ELEMENT_CLASS (gst_omx_video_enc_parent_class)->change_state (element,
      transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      self->downstream_flow_ret = GST_FLOW_FLUSHING;
      self->started = FALSE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
get_chroma_info_from_input (GstOMXVideoEnc * self, const gchar ** chroma_format,
    guint * bit_depth_luma, guint * bit_depth_chroma)
{
  switch (self->input_state->info.finfo->format) {
    case GST_VIDEO_FORMAT_GRAY8:
      *chroma_format = "4:0:0";
      *bit_depth_luma = 8;
      *bit_depth_chroma = 0;
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_NV12:
      *chroma_format = "4:2:0";
      *bit_depth_luma = *bit_depth_chroma = 8;
      break;
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_UYVY:
      *chroma_format = "4:2:2";
      *bit_depth_luma = *bit_depth_chroma = 8;
      break;
    case GST_VIDEO_FORMAT_GRAY10_LE32:
      *chroma_format = "4:0:0";
      *bit_depth_luma = 10;
      *bit_depth_chroma = 0;
      break;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      *chroma_format = "4:2:0";
      *bit_depth_luma = *bit_depth_chroma = 10;
      break;
    case GST_VIDEO_FORMAT_NV16_10LE32:
      *chroma_format = "4:2:2";
      *bit_depth_luma = *bit_depth_chroma = 10;
      break;
    default:
      return FALSE;
  }

  return TRUE;
}

static GstCaps *
get_output_caps (GstOMXVideoEnc * self)
{
  GstOMXVideoEncClass *klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);
  GstCaps *caps;
  const gchar *chroma_format;
  guint bit_depth_luma, bit_depth_chroma;

  caps = klass->get_caps (self, self->enc_out_port, self->input_state);

  /* Add chroma info about the encoded stream inferred from the format of the input */
  if (get_chroma_info_from_input (self, &chroma_format, &bit_depth_luma,
          &bit_depth_chroma)) {
    GST_DEBUG_OBJECT (self,
        "adding chroma info to output caps: %s (luma %d bits) (chroma %d bits)",
        chroma_format, bit_depth_luma, bit_depth_chroma);

    gst_caps_set_simple (caps, "chroma-format", G_TYPE_STRING, chroma_format,
        "bit-depth-luma", G_TYPE_UINT, bit_depth_luma,
        "bit-depth-chroma", G_TYPE_UINT, bit_depth_chroma, NULL);
  }

  return caps;
}

static gboolean
set_output_state (GstOMXVideoEnc * self)
{
  GstCaps *caps;
  GstVideoCodecState *state;
  gboolean result = FALSE;

  caps = get_output_caps (self);
  if (!caps)
    goto out;

  GST_DEBUG_OBJECT (self, "Setting output state: %" GST_PTR_FORMAT, caps);

  state =
      gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (self), caps,
      self->input_state);
  gst_video_codec_state_unref (state);

  if (!gst_video_encoder_negotiate (GST_VIDEO_ENCODER (self)))
    goto out;

  result = TRUE;
out:
  GST_VIDEO_ENCODER_STREAM_UNLOCK (self);
  return result;
}

static GstFlowReturn
gst_omx_video_enc_handle_output_frame (GstOMXVideoEnc * self, GstOMXPort * port,
    GstOMXBuffer * buf, GstVideoCodecFrame * frame)
{
  GstOMXVideoEncClass *klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);
  GstFlowReturn flow_ret = GST_FLOW_OK;

  if ((buf->omx_buf->nFlags & OMX_BUFFERFLAG_CODECCONFIG)
      && buf->omx_buf->nFilledLen > 0) {
    GstVideoCodecState *state;
    GstBuffer *codec_data;
    GstMapInfo map = GST_MAP_INFO_INIT;
    GstCaps *caps;

    GST_DEBUG_OBJECT (self, "Handling codec data");

    caps = get_output_caps (self);
    codec_data = gst_buffer_new_and_alloc (buf->omx_buf->nFilledLen);

    gst_buffer_map (codec_data, &map, GST_MAP_WRITE);
    memcpy (map.data,
        buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        buf->omx_buf->nFilledLen);
    gst_buffer_unmap (codec_data, &map);
    state =
        gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (self), caps,
        self->input_state);
    state->codec_data = codec_data;
    gst_video_codec_state_unref (state);
    if (!gst_video_encoder_negotiate (GST_VIDEO_ENCODER (self))) {
      gst_video_codec_frame_unref (frame);
      GST_ERROR_OBJECT (self,
          "Downstream element refused to negotiate codec_data in the caps");
      return GST_FLOW_NOT_NEGOTIATED;
    }
    gst_video_codec_frame_unref (frame);
    flow_ret = GST_FLOW_OK;
  } else if (buf->omx_buf->nFilledLen > 0) {
    GstBuffer *outbuf;
    GstMapInfo map = GST_MAP_INFO_INIT;

    GST_DEBUG_OBJECT (self, "Handling output data");

    outbuf = gst_buffer_new_and_alloc (buf->omx_buf->nFilledLen);

    gst_buffer_map (outbuf, &map, GST_MAP_WRITE);
    memcpy (map.data,
        buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        buf->omx_buf->nFilledLen);
    gst_buffer_unmap (outbuf, &map);

    GST_BUFFER_TIMESTAMP (outbuf) =
        gst_util_uint64_scale (GST_OMX_GET_TICKS (buf->omx_buf->nTimeStamp),
        GST_SECOND, OMX_TICKS_PER_SECOND);
    if (buf->omx_buf->nTickCount != 0)
      GST_BUFFER_DURATION (outbuf) =
          gst_util_uint64_scale (buf->omx_buf->nTickCount, GST_SECOND,
          OMX_TICKS_PER_SECOND);

    if ((klass->cdata.hacks & GST_OMX_HACK_SYNCFRAME_FLAG_NOT_USED)
        || (buf->omx_buf->nFlags & OMX_BUFFERFLAG_SYNCFRAME)) {
      if (frame)
        GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
      else
        GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
    } else {
      if (frame)
        GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT (frame);
      else
        GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
    }

    if (frame) {
      frame->output_buffer = outbuf;
      if ((buf->omx_buf->nFlags & OMX_BUFFERFLAG_ENDOFFRAME)
          || !gst_omx_port_get_subframe (self->enc_out_port)) {
        flow_ret =
            gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (self), frame);
        if (!(buf->omx_buf->nFlags & OMX_BUFFERFLAG_ENDOFFRAME))
          GST_WARNING_OBJECT (self,
              "OMX_BUFFERFLAG_ENDOFFRAME is missing in flags 0x%x",
              (guint) buf->omx_buf->nFlags);
      } else {
        flow_ret =
            gst_video_encoder_finish_subframe (GST_VIDEO_ENCODER (self), frame);
        gst_video_codec_frame_unref (frame);
      }
    } else {
      GST_ERROR_OBJECT (self, "No corresponding frame found");
      flow_ret = gst_pad_push (GST_VIDEO_ENCODER_SRC_PAD (self), outbuf);
    }
  } else if (frame != NULL) {
    /* Just ignore empty buffers, don't drop a frame for that */
    flow_ret = GST_FLOW_OK;
    gst_video_codec_frame_unref (frame);
  }

  return flow_ret;
}

static gboolean
gst_omx_video_enc_ensure_nb_out_buffers (GstOMXVideoEnc * self)
{
  GstOMXVideoEncClass *klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);
  guint extra = 0;

  if (!(klass->cdata.hacks & GST_OMX_HACK_ENSURE_BUFFER_COUNT_ACTUAL))
    return TRUE;

  /* If dowstream tell us how many buffers it needs allocate as many extra buffers so we won't starve
   * if it keeps them downstream (like when using dynamic mode). */
  if (self->nb_downstream_buffers)
    extra = self->nb_downstream_buffers;
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  if (g_getenv ("ENC_EXTRA_OP_BUFFERS") != NULL)
    extra += atoi (g_getenv ("ENC_EXTRA_OP_BUFFERS"));
#endif
  if (!gst_omx_port_ensure_buffer_count_actual (self->enc_out_port, extra))
    return FALSE;

  return TRUE;
}

static gboolean
gst_omx_video_enc_allocate_out_buffers (GstOMXVideoEnc * self)
{
  GstEvent *event;
  guint nb_buffers;

  if (gst_omx_port_allocate_buffers (self->enc_out_port) != OMX_ErrorNone)
    return FALSE;

  nb_buffers = self->enc_out_port->port_def.nBufferCountActual;

  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
      gst_structure_new ("buffers-allocated",
          "nb-buffers", G_TYPE_UINT, nb_buffers, NULL));

  gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), event);

  return TRUE;
}

static void
gst_omx_video_enc_pause_loop (GstOMXVideoEnc * self, GstFlowReturn flow_ret)
{
  g_mutex_lock (&self->drain_lock);
  if (self->draining) {
    self->draining = FALSE;
    g_cond_broadcast (&self->drain_cond);
  }
  gst_pad_pause_task (GST_VIDEO_ENCODER_SRC_PAD (self));
  self->downstream_flow_ret = flow_ret;
  self->started = FALSE;
  g_mutex_unlock (&self->drain_lock);
}

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
static gboolean
zynq_seamless_output_transition (GstOMXVideoEnc * self, GstOMXPort * port)
{
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_VIDEO_PORTDEFINITIONTYPE new;

  gst_omx_port_get_port_definition (port, &port_def);
  new = port_def.format.video;

  if (!gst_omx_video_port_support_resolution (port,
          new.nFrameWidth, new.nFrameHeight)) {
    GST_DEBUG_OBJECT (self,
        "Output port does not support new resolution (%ux%u), can't do seamless transition",
        new.nFrameWidth, new.nFrameHeight);
    return FALSE;
  }

  /* No need to check xFramerate, it doesn't affect the buffer size */

  GST_DEBUG_OBJECT (self,
      "only the output resolution changed; use seamless transition");
  return TRUE;
}
#endif
static void
gst_omx_video_enc_loop (GstOMXVideoEnc * self)
{
  GstOMXVideoEncClass *klass;
  GstOMXPort *port = self->enc_out_port;
  GstOMXBuffer *buf = NULL;
  GstVideoCodecFrame *frame;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstOMXAcquireBufferReturn acq_return;
  OMX_ERRORTYPE err;

  klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);

  acq_return = gst_omx_port_acquire_buffer (port, &buf, GST_OMX_WAIT);
  if (acq_return == GST_OMX_ACQUIRE_BUFFER_ERROR) {
    goto component_error;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
    goto flushing;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_EOS) {
    goto eos;
  }

  if (!gst_pad_has_current_caps (GST_VIDEO_ENCODER_SRC_PAD (self))
      || acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
    gboolean disable_port = FALSE, reconfigure_port = FALSE;

    GST_DEBUG_OBJECT (self, "Port settings have changed, updating caps");

    if (acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      reconfigure_port = TRUE;
      if (gst_omx_port_is_enabled (port))
        disable_port = TRUE;
    }

    if (disable_port) {
      /* Reallocate all buffers */
      err = gst_omx_port_set_enabled (port, FALSE);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_wait_buffers_released (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_deallocate_buffers (port);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_wait_enabled (port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;
    }

    if (!set_output_state (self)) {
      if (buf)
        gst_omx_port_release_buffer (self->enc_out_port, buf);
      goto caps_failed;
    }

    if (reconfigure_port) {
      if (!gst_omx_video_enc_ensure_nb_out_buffers (self))
        goto reconfigure_error;

      err = gst_omx_port_set_enabled (port, TRUE);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      if (!gst_omx_video_enc_allocate_out_buffers (self))
        goto reconfigure_error;

      err = gst_omx_port_wait_enabled (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_populate (port);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_mark_reconfigured (port);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
      if (self->xlnx_ll && self->started && self->in_pool_used) {
        GstEvent *event;

        GST_DEBUG_OBJECT (self, "Tell XLNX-LL producer it can start streaming");

        event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
            gst_structure_new ("xlnx-ll-consumer-ready", NULL, NULL));

        gst_pad_push_event (GST_VIDEO_DECODER_SINK_PAD (self), event);
      }
#endif
    }

    /* Now get a buffer */
    if (acq_return != GST_OMX_ACQUIRE_BUFFER_OK) {
      return;
    }
  }
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  if (acq_return == GST_OMX_ACQUIRE_BUFFER_RESOLUTION_CHANGE) {
    GST_DEBUG_OBJECT (self,
        "Output resolution changed; use seamless transition");

    if (!zynq_seamless_output_transition (self, port)
        || !set_output_state (self)) {
      gst_omx_port_release_buffer (self->enc_out_port, buf);
      GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
          ("Seamless output transition failed"));
    }
    /* Now get a buffer */
    return;
  }
#endif

  g_assert (acq_return == GST_OMX_ACQUIRE_BUFFER_OK);

  /* This prevents a deadlock between the srcpad stream
   * lock and the videocodec stream lock, if ::flush()
   * is called at the wrong time
   */
  if (gst_omx_port_is_flushing (self->enc_out_port)) {
    GST_DEBUG_OBJECT (self, "Flushing");
    gst_omx_port_release_buffer (self->enc_out_port, buf);
    goto flushing;
  }

  GST_DEBUG_OBJECT (self, "Handling buffer: 0x%08x (%s) %" G_GUINT64_FORMAT,
      (guint) buf->omx_buf->nFlags,
      gst_omx_buffer_flags_to_string (buf->omx_buf->nFlags),
      (guint64) GST_OMX_GET_TICKS (buf->omx_buf->nTimeStamp));

  frame = gst_omx_video_find_nearest_frame (GST_ELEMENT_CAST (self), buf,
      gst_video_encoder_get_frames (GST_VIDEO_ENCODER (self)));

  g_assert (klass->handle_output_frame);

  if (frame)
    flow_ret =
        klass->handle_output_frame (self, self->enc_out_port, buf, frame);
  else {
    gst_omx_port_release_buffer (self->enc_out_port, buf);
    goto flow_error;
  }


  GST_DEBUG_OBJECT (self, "Finished frame: %s", gst_flow_get_name (flow_ret));

  err = gst_omx_port_release_buffer (port, buf);
  if (err != OMX_ErrorNone)
    goto release_error;

  GST_VIDEO_ENCODER_STREAM_LOCK (self);
  self->downstream_flow_ret = flow_ret;
  GST_VIDEO_ENCODER_STREAM_UNLOCK (self);

  GST_DEBUG_OBJECT (self, "Read frame from component");

  if (flow_ret != GST_FLOW_OK)
    goto flow_error;

  return;

component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->enc),
            gst_omx_component_get_last_error (self->enc)));
    gst_pad_push_event (GST_VIDEO_ENCODER_SRC_PAD (self), gst_event_new_eos ());
    gst_omx_video_enc_pause_loop (self, GST_FLOW_ERROR);
    return;
  }
flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    gst_omx_video_enc_pause_loop (self, GST_FLOW_FLUSHING);
    return;
  }

eos:
  {
    g_mutex_lock (&self->drain_lock);
    if (self->draining) {
      GST_DEBUG_OBJECT (self, "Drained");
      self->draining = FALSE;
      g_cond_broadcast (&self->drain_cond);
      flow_ret = GST_FLOW_OK;
      gst_pad_pause_task (GST_VIDEO_ENCODER_SRC_PAD (self));
    } else {
      GST_DEBUG_OBJECT (self, "Component signalled EOS");
      flow_ret = GST_FLOW_EOS;
    }
    g_mutex_unlock (&self->drain_lock);

    GST_VIDEO_ENCODER_STREAM_LOCK (self);
    self->downstream_flow_ret = flow_ret;
    GST_VIDEO_ENCODER_STREAM_UNLOCK (self);

    /* Here we fallback and pause the task for the EOS case */
    if (flow_ret != GST_FLOW_OK)
      goto flow_error;

    return;
  }
flow_error:
  {
    if (flow_ret == GST_FLOW_EOS) {
      GST_DEBUG_OBJECT (self, "EOS");

      gst_pad_push_event (GST_VIDEO_ENCODER_SRC_PAD (self),
          gst_event_new_eos ());
    } else if (flow_ret < GST_FLOW_EOS) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("Internal data stream error."),
          ("stream stopped, reason %s", gst_flow_get_name (flow_ret)));

      gst_pad_push_event (GST_VIDEO_ENCODER_SRC_PAD (self),
          gst_event_new_eos ());
    } else if (flow_ret == GST_FLOW_FLUSHING) {
      GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    }
    gst_omx_video_enc_pause_loop (self, flow_ret);
    return;
  }
reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure output port"));
    gst_pad_push_event (GST_VIDEO_ENCODER_SRC_PAD (self), gst_event_new_eos ());
    gst_omx_video_enc_pause_loop (self, GST_FLOW_NOT_NEGOTIATED);
    return;
  }
caps_failed:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL), ("Failed to set caps"));
    gst_pad_push_event (GST_VIDEO_ENCODER_SRC_PAD (self), gst_event_new_eos ());
    gst_omx_video_enc_pause_loop (self, GST_FLOW_NOT_NEGOTIATED);
    return;
  }
release_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to relase output buffer to component: %s (0x%08x)",
            gst_omx_error_to_string (err), err));
    gst_pad_push_event (GST_VIDEO_ENCODER_SRC_PAD (self), gst_event_new_eos ());
    gst_omx_video_enc_pause_loop (self, GST_FLOW_ERROR);
    return;
  }
}

static gboolean
gst_omx_video_enc_start (GstVideoEncoder * encoder)
{
  GstOMXVideoEnc *self;

  self = GST_OMX_VIDEO_ENC (encoder);

  self->last_upstream_ts = 0;
  self->downstream_flow_ret = GST_FLOW_OK;
  self->nb_upstream_buffers = 0;
  self->nb_downstream_buffers = 0;
  self->in_pool_used = FALSE;
  self->xlnx_ll = FALSE;

  return TRUE;
}

static gboolean
gst_omx_video_enc_stop (GstVideoEncoder * encoder)
{
  GstOMXVideoEnc *self;

  self = GST_OMX_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Stopping encoder");

  gst_omx_port_set_flushing (self->enc_in_port, 5 * GST_SECOND, TRUE);
  gst_omx_port_set_flushing (self->enc_out_port, 5 * GST_SECOND, TRUE);

  gst_pad_stop_task (GST_VIDEO_ENCODER_SRC_PAD (encoder));

  if (gst_omx_component_get_state (self->enc, 0) > OMX_StateIdle)
    gst_omx_component_set_state (self->enc, OMX_StateIdle);

  self->downstream_flow_ret = GST_FLOW_FLUSHING;
  self->started = FALSE;

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = NULL;

  g_mutex_lock (&self->drain_lock);
  self->draining = FALSE;
  g_cond_broadcast (&self->drain_cond);
  g_mutex_unlock (&self->drain_lock);

  self->default_target_bitrate = GST_OMX_PROP_OMX_DEFAULT;

  gst_omx_component_get_state (self->enc, 5 * GST_SECOND);

  return TRUE;
}

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
static void
gst_omx_video_enc_set_latency (GstOMXVideoEnc * self)
{
  GstClockTime latency;
  OMX_ALG_PARAM_REPORTED_LATENCY param;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&param);
  err =
      gst_omx_component_get_parameter (self->enc,
      (OMX_INDEXTYPE) OMX_ALG_IndexParamReportedLatency, &param);

  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self, "Couldn't retrieve latency: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return;
  }

  GST_DEBUG_OBJECT (self, "retrieved latency of %d ms",
      (guint32) param.nLatency);

  /* Convert to ns */
  latency = param.nLatency * GST_MSECOND;

  gst_video_encoder_set_latency (GST_VIDEO_ENCODER (self), latency, latency);
}
#endif

static gboolean
gst_omx_video_enc_disable (GstOMXVideoEnc * self)
{
  GstOMXVideoEncClass *klass;

  klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Need to disable and drain encoder");
  gst_omx_video_enc_drain (self);
  gst_omx_port_set_flushing (self->enc_out_port, 5 * GST_SECOND, TRUE);

  /* Wait until the srcpad loop is finished,
   * unlock GST_VIDEO_ENCODER_STREAM_LOCK to prevent deadlocks
   * caused by using this lock from inside the loop function */
  GST_VIDEO_ENCODER_STREAM_UNLOCK (self);
  gst_pad_stop_task (GST_VIDEO_ENCODER_SRC_PAD (self));
  GST_VIDEO_ENCODER_STREAM_LOCK (self);

  if (klass->cdata.hacks & GST_OMX_HACK_NO_COMPONENT_RECONFIGURE) {
    GST_VIDEO_ENCODER_STREAM_UNLOCK (self);
    gst_omx_video_enc_stop (GST_VIDEO_ENCODER (self));
    gst_omx_video_enc_close (GST_VIDEO_ENCODER (self));
    GST_VIDEO_ENCODER_STREAM_LOCK (self);

    if (!gst_omx_video_enc_open (GST_VIDEO_ENCODER (self)))
      return FALSE;

    /* The decoder is returned to initial state */
    self->disabled = FALSE;
  } else {
    /* Disabling at the same time input port and output port is only
     * required when a buffer is shared between the ports. This cannot
     * be the case for a encoder because its input and output buffers
     * are of different nature. So let's disable ports sequencially.
     * Starting from IL 1.2.0, this point has been clarified.
     * OMX_SendCommand will return an error if the IL client attempts to
     * call it when there is already an on-going command being processed.
     * The exception is for buffer sharing above and the event
     * OMX_EventPortNeedsDisable will be sent to request disabling the
     * other port at the same time. */
    if (gst_omx_port_set_enabled (self->enc_in_port, FALSE) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_wait_buffers_released (self->enc_in_port,
            5 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;
    if (!gst_omx_video_enc_deallocate_in_buffers (self))
      return FALSE;
    if (gst_omx_port_wait_enabled (self->enc_in_port,
            1 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;

    if (gst_omx_port_set_enabled (self->enc_out_port, FALSE) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_wait_buffers_released (self->enc_out_port,
            1 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_deallocate_buffers (self->enc_out_port) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_wait_enabled (self->enc_out_port,
            1 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;

    self->disabled = TRUE;
  }

  GST_DEBUG_OBJECT (self, "Encoder drained and disabled");
  return TRUE;
}

static gboolean
gst_omx_video_enc_configure_input_buffer (GstOMXVideoEnc * self,
    GstBuffer * input)
{
  GstOMXVideoEncClass *klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);
  GstVideoInfo *info = &self->input_state->info;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  GstVideoMeta *meta;
  guint stride, slice_height;

  gst_omx_port_get_port_definition (self->enc_in_port, &port_def);

  meta = gst_buffer_get_video_meta (input);
  if (meta) {
    guint plane_height[GST_VIDEO_MAX_PLANES];

    /* Use the stride and slice height of the first plane */
    if (!gst_video_meta_get_plane_height (meta, plane_height)) {
      GST_WARNING_OBJECT (self, "Failed to retrieve plane height from meta");
      slice_height = GST_VIDEO_INFO_FIELD_HEIGHT (info);
    } else {
      slice_height = plane_height[0];
    }

    stride = meta->stride[0];
    g_assert (stride != 0);

    GST_DEBUG_OBJECT (self,
        "adjusting stride (%d) and slice-height (%d) using input buffer meta",
        stride, slice_height);
  } else {
    GST_WARNING_OBJECT (self,
        "input buffer doesn't provide video meta, can't adjust stride and slice height");

    stride = info->stride[0];
    slice_height = GST_VIDEO_INFO_FIELD_HEIGHT (info);
  }

  if (port_def.nBufferAlignment)
    port_def.format.video.nStride =
        GST_ROUND_UP_N (stride, port_def.nBufferAlignment);
  else
    port_def.format.video.nStride = GST_ROUND_UP_4 (stride);    /* safe (?) default */

  if (klass->cdata.hacks & GST_OMX_HACK_HEIGHT_MULTIPLE_16)
    port_def.format.video.nSliceHeight = GST_ROUND_UP_16 (slice_height);
  else
    port_def.format.video.nSliceHeight = slice_height;

  switch (port_def.format.video.eColorFormat) {
    case OMX_COLOR_FormatYUV420Planar:
    case OMX_COLOR_FormatYUV420PackedPlanar:
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
      /* Formats defined in extensions have their own enum so disable to -Wswitch warning */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
    case OMX_ALG_COLOR_FormatYUV420SemiPlanar10bitPacked:
#pragma GCC diagnostic pop
#endif
      port_def.nBufferSize =
          (port_def.format.video.nStride * port_def.format.video.nFrameHeight) +
          2 * ((port_def.format.video.nStride / 2) *
          ((port_def.format.video.nFrameHeight + 1) / 2));
      break;

    case OMX_COLOR_FormatYUV420PackedSemiPlanar:
    case OMX_COLOR_FormatYUV420SemiPlanar:
      port_def.nBufferSize =
          (port_def.format.video.nStride * port_def.format.video.nFrameHeight) +
          (port_def.format.video.nStride *
          ((port_def.format.video.nFrameHeight + 1) / 2));
      break;

    case OMX_COLOR_FormatL8:
      port_def.nBufferSize =
          port_def.format.video.nStride * port_def.format.video.nFrameHeight;
      break;

    case OMX_COLOR_FormatYUV422SemiPlanar:
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
      /* Formats defined in extensions have their own enum so disable to -Wswitch warning */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
    case OMX_ALG_COLOR_FormatYUV422SemiPlanar10bitPacked:
#pragma GCC diagnostic pop
#endif
      port_def.nBufferSize =
          (port_def.format.video.nStride * port_def.format.video.nFrameHeight) +
          2 * (port_def.format.video.nStride *
          ((port_def.format.video.nFrameHeight + 1) / 2));
      break;

    default:
      GST_ERROR_OBJECT (self, "Unsupported port format %x",
          port_def.format.video.eColorFormat);
      g_assert_not_reached ();
  }

  GST_DEBUG_OBJECT (self,
      "setting input nStride=%d nSliceHeight=%d nBufferSize=%d (nBufferAlignment=%d)",
      (guint) port_def.format.video.nStride,
      (guint) port_def.format.video.nSliceHeight,
      (guint) port_def.nBufferSize, (guint) port_def.nBufferAlignment);

  if (gst_omx_port_update_port_definition (self->enc_in_port,
          &port_def) != OMX_ErrorNone)
    return FALSE;

  return TRUE;
}

static gboolean
gst_omx_video_enc_ensure_nb_in_buffers (GstOMXVideoEnc * self)
{
  guint extra = 0;
  GstOMXVideoEncClass *klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);

  if (self->input_allocation == GST_OMX_BUFFER_ALLOCATION_USE_BUFFER_DYNAMIC &&
      self->nb_upstream_buffers > self->enc_in_port->port_def.nBufferCountMin) {
    GST_DEBUG_OBJECT (self,
        "Upstream allocated %d buffers, allocate as many input buffers as we are using dynamic buffer mode",
        self->nb_upstream_buffers);

    if (!gst_omx_port_update_buffer_count_actual (self->enc_in_port,
            self->nb_upstream_buffers))
      return FALSE;
  } else if ((klass->cdata.hacks & GST_OMX_HACK_ENSURE_BUFFER_COUNT_ACTUAL)) {
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
    if (g_getenv ("ENC_EXTRA_IP_BUFFERS") != NULL)
      extra = atoi (g_getenv ("ENC_EXTRA_IP_BUFFERS"));
#endif
    if (!gst_omx_port_ensure_buffer_count_actual (self->enc_in_port, extra))
      return FALSE;
  }

  return TRUE;
}

static gboolean
gst_omx_video_enc_allocate_in_buffers (GstOMXVideoEnc * self)
{
  switch (self->input_allocation) {
    case GST_OMX_BUFFER_ALLOCATION_ALLOCATE_BUFFER:
      if (gst_omx_port_allocate_buffers (self->enc_in_port) != OMX_ErrorNone)
        return FALSE;
      break;
    case GST_OMX_BUFFER_ALLOCATION_USE_BUFFER_DYNAMIC:
      if (gst_omx_port_use_dynamic_buffers (self->enc_in_port) != OMX_ErrorNone)
        return FALSE;
      break;
    case GST_OMX_BUFFER_ALLOCATION_USE_BUFFER:
    default:
      /* Not supported */
      g_return_val_if_reached (FALSE);
  }

  return TRUE;
}

static gboolean
check_input_alignment (GstOMXVideoEnc * self, GstMapInfo * map)
{
  OMX_PARAM_PORTDEFINITIONTYPE *port_def = &self->enc_in_port->port_def;

  if (map->size != port_def->nBufferSize) {
    GST_DEBUG_OBJECT (self,
        "input buffer has wrong size/stride (%" G_GSIZE_FORMAT
        " expected: %u), can't use dynamic allocation",
        map->size, (guint32) port_def->nBufferSize);
    return FALSE;
  }

  if (port_def->nBufferAlignment &&
      (GPOINTER_TO_UINT (map->data) & (port_def->nBufferAlignment - 1)) != 0) {
    GST_DEBUG_OBJECT (self,
        "input buffer is not properly aligned (address: %p alignment: %u bytes), can't use dynamic allocation",
        map->data, (guint32) port_def->nBufferAlignment);
    return FALSE;
  }

  return TRUE;
}

/* Check if @inbuf's alignment and stride matches the requirements to use the
 * dynamic buffer mode. */
static gboolean
can_use_dynamic_buffer_mode (GstOMXVideoEnc * self, GstBuffer * inbuf)
{
  GstMapInfo map;
  gboolean result = FALSE;

  if (gst_buffer_n_memory (inbuf) > 1) {
    GST_DEBUG_OBJECT (self,
        "input buffer contains more than one memory, can't use dynamic allocation");
    return FALSE;
  }

  if (!gst_buffer_map (inbuf, &map, GST_MAP_READ)) {
    GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
        ("failed to map input buffer"));
    return FALSE;
  }

  result = check_input_alignment (self, &map);

  gst_buffer_unmap (inbuf, &map);
  return result;
}

/* Choose the allocation mode for input buffers depending of what's supported by
 * the component and the size/alignment of the input buffer. */
static GstOMXBufferAllocation
gst_omx_video_enc_pick_input_allocation_mode (GstOMXVideoEnc * self,
    GstBuffer * inbuf)
{
  if (!gst_omx_is_dynamic_allocation_supported ())
    return GST_OMX_BUFFER_ALLOCATION_ALLOCATE_BUFFER;

  if (can_use_dynamic_buffer_mode (self, inbuf)) {
    GST_DEBUG_OBJECT (self,
        "input buffer is properly aligned, use dynamic allocation");
    return GST_OMX_BUFFER_ALLOCATION_USE_BUFFER_DYNAMIC;
  }

  GST_DEBUG_OBJECT (self, "let input buffer allocate its buffers");
  return GST_OMX_BUFFER_ALLOCATION_ALLOCATE_BUFFER;
}

static gboolean
gst_omx_video_enc_set_to_idle (GstOMXVideoEnc * self)
{
  GstOMXVideoEncClass *klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);
  gboolean no_disable_outport;

  no_disable_outport = klass->cdata.hacks & GST_OMX_HACK_NO_DISABLE_OUTPORT;

  if (!no_disable_outport) {
    /* Disable output port */
    if (gst_omx_port_set_enabled (self->enc_out_port, FALSE) != OMX_ErrorNone)
      return FALSE;

    if (gst_omx_port_wait_enabled (self->enc_out_port,
            1 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;
  }

  if (gst_omx_component_set_state (self->enc, OMX_StateIdle) != OMX_ErrorNone)
    return FALSE;

  /* Need to allocate buffers to reach Idle state */
  if (!gst_omx_video_enc_allocate_in_buffers (self))
    return FALSE;

  if (no_disable_outport) {
    if (!gst_omx_video_enc_allocate_out_buffers (self))
      return FALSE;
  }

  if (gst_omx_component_get_state (self->enc,
          GST_CLOCK_TIME_NONE) != OMX_StateIdle)
    return FALSE;

  return TRUE;
}

static GstOMXBuffer *
get_omx_buf (GstBuffer * buffer)
{
  GstMemory *mem;

  mem = gst_buffer_peek_memory (buffer, 0);
  return gst_omx_memory_get_omx_buf (mem);
}

static gboolean
buffer_is_from_input_pool (GstOMXVideoEnc * self, GstBuffer * buffer)
{
  /* Buffer from our input pool will already have a GstOMXBuffer associated
   * with our input port. */
  GstOMXBuffer *buf;

  buf = get_omx_buf (buffer);
  if (!buf)
    return FALSE;

  return buf->port == self->enc_in_port;
}

static gboolean
gst_omx_video_enc_enable (GstOMXVideoEnc * self, GstBuffer * input)
{
  GstOMXVideoEncClass *klass;

  klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);

  /* Is downstream using our buffer pool? */
  if (buffer_is_from_input_pool (self, input)) {
    self->in_pool_used = TRUE;
  }

  if (!self->in_pool_used) {
    if (!gst_omx_video_enc_configure_input_buffer (self, input))
      return FALSE;

    self->input_allocation = gst_omx_video_enc_pick_input_allocation_mode (self,
        input);
    self->input_dmabuf = FALSE;

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
    if (gst_is_dmabuf_memory (gst_buffer_peek_memory (input, 0))) {
      if (self->input_allocation ==
          GST_OMX_BUFFER_ALLOCATION_USE_BUFFER_DYNAMIC) {
        GST_DEBUG_OBJECT (self, "Configure encoder input to import dmabuf");
        gst_omx_port_set_dmabuf (self->enc_in_port, TRUE);
      } else {
        GST_DEBUG_OBJECT (self,
            "Wrong input allocation mode (%d); dynamic buffers are required to use dmabuf import",
            self->input_allocation);
      }

      self->input_dmabuf = TRUE;
    }
#endif
  }

  GST_DEBUG_OBJECT (self, "Enabling component");

  if (!self->in_pool_used) {
    if (!gst_omx_video_enc_ensure_nb_in_buffers (self))
      return FALSE;
    if (!gst_omx_video_enc_ensure_nb_out_buffers (self))
      return FALSE;
  }

  if (self->disabled) {
    if (gst_omx_port_set_enabled (self->enc_in_port, TRUE) != OMX_ErrorNone)
      return FALSE;
    if (!gst_omx_video_enc_allocate_in_buffers (self))
      return FALSE;

    if ((klass->cdata.hacks & GST_OMX_HACK_NO_DISABLE_OUTPORT)) {
      if (gst_omx_port_set_enabled (self->enc_out_port, TRUE) != OMX_ErrorNone)
        return FALSE;
      if (!gst_omx_video_enc_allocate_out_buffers (self))
        return FALSE;

      if (gst_omx_port_wait_enabled (self->enc_out_port,
              5 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
    }

    if (gst_omx_port_wait_enabled (self->enc_in_port,
            5 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_mark_reconfigured (self->enc_in_port) != OMX_ErrorNone)
      return FALSE;
  } else {
    /* If the input pool is active we already allocated buffers and set the component to Idle. */
    if (!self->in_pool_used) {
      if (!gst_omx_video_enc_set_to_idle (self))
        return FALSE;
    }

    if (gst_omx_component_set_state (self->enc,
            OMX_StateExecuting) != OMX_ErrorNone)
      return FALSE;

    if (gst_omx_component_get_state (self->enc,
            GST_CLOCK_TIME_NONE) != OMX_StateExecuting)
      return FALSE;
  }

  /* Unset flushing to allow ports to accept data again */
  gst_omx_port_set_flushing (self->enc_in_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_set_flushing (self->enc_out_port, 5 * GST_SECOND, FALSE);

  if (gst_omx_component_get_last_error (self->enc) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Component in error state: %s (0x%08x)",
        gst_omx_component_get_last_error_string (self->enc),
        gst_omx_component_get_last_error (self->enc));
    return FALSE;
  }

  self->disabled = FALSE;

  return TRUE;
}

/* returns TRUE if only the framerate changed and that framerate could be
 * updated using OMX_IndexConfigVideoFramerate */
static gboolean
gst_omx_video_enc_framerate_changed (GstOMXVideoEnc * self,
    GstVideoCodecState * state)
{
  GstVideoInfo prev_info = self->input_state->info;
  GstVideoInfo *info = &state->info;
  GstOMXVideoEncClass *klass;

  klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);

  prev_info.fps_n = info->fps_n;
  prev_info.fps_d = info->fps_d;

  /* if only the framerate changed, try and set the framerate parameter */
  if (gst_video_info_is_equal (info, &prev_info)) {
    OMX_CONFIG_FRAMERATETYPE config;
    OMX_ERRORTYPE err;

    GST_DEBUG_OBJECT (self, "Framerate change detected: %d/%d -> %d/%d",
        self->input_state->info.fps_n, self->input_state->info.fps_d,
        info->fps_n, info->fps_d);

    GST_OMX_INIT_STRUCT (&config);
    config.nPortIndex = self->enc_in_port->index;
    if (klass->cdata.hacks & GST_OMX_HACK_VIDEO_FRAMERATE_INTEGER) {
      config.xEncodeFramerate =
          info->fps_d ? GST_VIDEO_INFO_FIELD_RATE_N (info) / (info->fps_d) : 0;
    } else {
      config.xEncodeFramerate = gst_omx_video_calculate_framerate_q16 (info);
    }

    err = gst_omx_component_set_config (self->enc,
        OMX_IndexConfigVideoFramerate, &config);
    if (err == OMX_ErrorNone) {
      gst_video_codec_state_unref (self->input_state);
      self->input_state = gst_video_codec_state_ref (state);
      return TRUE;
    } else {
      GST_WARNING_OBJECT (self,
          "Failed to set framerate configuration: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      /* if changing the rate dynamically didn't work, keep going with a full
       * encoder reset */
    }
  }

  return FALSE;
}

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
/* returns TRUE if only the resolution changed and that resolution could be
 * updated using OMX_ALG_VIDEO_CONFIG_NOTIFY_RESOLUTION_CHANGE */
static gboolean
gst_omx_video_enc_resolution_changed (GstOMXVideoEnc * self,
    GstVideoCodecState * state)
{
  GstVideoInfo *info = &state->info;
  OMX_ALG_VIDEO_CONFIG_NOTIFY_RESOLUTION_CHANGE config;
  OMX_ERRORTYPE err;

  if (self->input_state->info.finfo->format != info->finfo->format) {
    GST_LOG_OBJECT (self, "Video format changed, can't do seamless transition");
    return FALSE;
  }

  if (!gst_omx_video_port_support_resolution (self->enc_in_port,
          info->width, info->height)) {
    GST_DEBUG_OBJECT (self,
        "Input port does not support new resolution (%dx%d), can't do seamless transition",
        info->width, info->height);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self,
      "Resolution change detected (%dx%d -> %dx%d) do seameless transition",
      self->input_state->info.width, self->input_state->info.height,
      info->width, info->height);

  GST_OMX_INIT_STRUCT (&config);
  config.nPortIndex = self->enc_in_port->index;

  config.nWidth = info->width;
  config.nHeight = GST_VIDEO_INFO_FIELD_HEIGHT (info);

  err = gst_omx_component_set_config (self->enc,
      OMX_ALG_IndexConfigVideoNotifyResolutionChange, &config);
  if (err == OMX_ErrorNone) {
    OMX_PARAM_PORTDEFINITIONTYPE port_def;

    gst_video_codec_state_unref (self->input_state);
    self->input_state = gst_video_codec_state_ref (state);

    /* Port has been updated with the new resolution so update it */
    gst_omx_port_update_port_definition (self->enc_in_port, NULL);
    gst_omx_port_get_port_definition (self->enc_in_port, &port_def);
  } else {
    GST_WARNING_OBJECT (self,
        "Failed to set resolution configuration: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    /* if changing the resolution dynamically didn't work, keep going with a full
     * encoder reset */
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_omx_video_enc_set_interlacing_parameters (GstOMXVideoEnc * self,
    GstVideoInfo * info)
{
  OMX_ERRORTYPE err;
  OMX_INTERLACEFORMATTYPE interlace_format_param;

  GST_OMX_INIT_STRUCT (&interlace_format_param);
  interlace_format_param.nPortIndex = self->enc_in_port->index;

  err = gst_omx_component_get_parameter (self->enc,
      (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoInterlaceFormatCurrent,
      &interlace_format_param);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get interlace format: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  if (info->interlace_mode == GST_VIDEO_INTERLACE_MODE_PROGRESSIVE)
    interlace_format_param.nFormat = OMX_InterlaceFrameProgressive;
  else if (info->interlace_mode == GST_VIDEO_INTERLACE_MODE_ALTERNATE) {
    if (GST_VIDEO_INFO_FIELD_ORDER (info) ==
        GST_VIDEO_FIELD_ORDER_BOTTOM_FIELD_FIRST)
      interlace_format_param.nFormat =
          OMX_ALG_InterlaceAlternateBottomFieldFirst;
    else if (GST_VIDEO_INFO_FIELD_ORDER (info) ==
        GST_VIDEO_FIELD_ORDER_BOTTOM_FIELD_FIRST)
      interlace_format_param.nFormat = OMX_ALG_InterlaceAlternateTopFieldFirst;
    else {
      GST_INFO_OBJECT (self,
          "input field-order unspecified, assume top-field-first");
      interlace_format_param.nFormat = OMX_ALG_InterlaceAlternateTopFieldFirst;
    }
  } else {
    /* Caps templates should ensure this doesn't happen but just to be safe.. */
    GST_ERROR_OBJECT (self, "Video interlacing mode %s not supported",
        gst_video_interlace_mode_to_string (info->interlace_mode));
    return FALSE;
  }

  err = gst_omx_component_set_parameter (self->enc,
      (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoInterlaceFormatCurrent,
      &interlace_format_param);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to set interlacing mode %s (%s) format: %s (0x%08x)",
        gst_video_interlace_mode_to_string (info->interlace_mode),
        interlace_format_param.nFormat ==
        OMX_ALG_InterlaceAlternateTopFieldFirst ? "top-field-first" :
        "bottom-field-first", gst_omx_error_to_string (err), err);
    return FALSE;
  } else {
    GST_DEBUG_OBJECT (self,
        "Video interlacing mode %s (%s) set on component",
        gst_video_interlace_mode_to_string (info->interlace_mode),
        interlace_format_param.nFormat ==
        OMX_ALG_InterlaceAlternateTopFieldFirst ? "top-field-first" :
        "bottom-field-first");
  }

  return TRUE;
}
#endif // USE_OMX_TARGET_ZYNQ_USCALE_PLUS

static gboolean
gst_omx_video_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstOMXVideoEnc *self;
  GstOMXVideoEncClass *klass;
  gboolean needs_disable = FALSE;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  GstVideoInfo *info = &state->info;
  GList *negotiation_map = NULL, *l;
  GstCaps *caps;

  self = GST_OMX_VIDEO_ENC (encoder);
  klass = GST_OMX_VIDEO_ENC_GET_CLASS (encoder);

  caps = gst_video_info_to_caps (info);
  GST_DEBUG_OBJECT (self, "Setting new input format: %" GST_PTR_FORMAT, caps);
  gst_caps_unref (caps);

  gst_omx_port_get_port_definition (self->enc_in_port, &port_def);

  needs_disable =
      gst_omx_component_get_state (self->enc,
      GST_CLOCK_TIME_NONE) != OMX_StateLoaded;
  /* If the component is not in Loaded state and a real format change happens
   * we have to disable the port and re-allocate all buffers. If no real
   * format change happened we can just exit here.
   */
  if (needs_disable) {
    if (gst_omx_video_enc_framerate_changed (self, state))
      return TRUE;

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
    if (gst_omx_video_enc_resolution_changed (self, state))
      return TRUE;
#endif

    GST_DEBUG_OBJECT (self, "Need to disable input port to reconfigure it");

    if (!gst_omx_video_enc_disable (self))
      return FALSE;

    if (!self->disabled) {
      /* The local port_def is now obsolete so get it again. */
      gst_omx_port_get_port_definition (self->enc_in_port, &port_def);
    }
  }

  negotiation_map =
      gst_omx_video_get_supported_colorformats (self->enc_in_port,
      self->input_state);
  if (!negotiation_map) {
    /* Fallback */
    switch (info->finfo->format) {
      case GST_VIDEO_FORMAT_I420:
        port_def.format.video.eColorFormat = OMX_COLOR_FormatYUV420Planar;
        break;
      case GST_VIDEO_FORMAT_NV12:
        port_def.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
        break;
      case GST_VIDEO_FORMAT_NV16:
        port_def.format.video.eColorFormat = OMX_COLOR_FormatYUV422SemiPlanar;
        break;
      case GST_VIDEO_FORMAT_ABGR:
        port_def.format.video.eColorFormat = OMX_COLOR_Format32bitARGB8888;
        break;
      case GST_VIDEO_FORMAT_ARGB:
        port_def.format.video.eColorFormat = OMX_COLOR_Format32bitBGRA8888;
        break;
      default:
        GST_ERROR_OBJECT (self, "Unsupported format %s",
            gst_video_format_to_string (info->finfo->format));
        return FALSE;
        break;
    }
  } else {
    for (l = negotiation_map; l; l = l->next) {
      GstOMXVideoNegotiationMap *m = l->data;

      if (m->format == info->finfo->format) {
        port_def.format.video.eColorFormat = m->type;
        break;
      }
    }
    g_list_free_full (negotiation_map,
        (GDestroyNotify) gst_omx_video_negotiation_map_free);
  }

  port_def.format.video.nFrameWidth = info->width;
  port_def.format.video.nFrameHeight = GST_VIDEO_INFO_FIELD_HEIGHT (info);

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  if (!gst_omx_video_enc_set_interlacing_parameters (self, info))
    return FALSE;
#endif

  if (G_UNLIKELY (klass->cdata.hacks & GST_OMX_HACK_VIDEO_FRAMERATE_INTEGER)) {
    port_def.format.video.xFramerate =
        info->fps_d ? GST_VIDEO_INFO_FIELD_RATE_N (info) / (info->fps_d) : 0;
  } else {
    port_def.format.video.xFramerate =
        gst_omx_video_calculate_framerate_q16 (info);
  }

  GST_DEBUG_OBJECT (self, "Setting inport port definition");
  if (gst_omx_port_update_port_definition (self->enc_in_port,
          &port_def) != OMX_ErrorNone)
    return FALSE;

#ifdef USE_OMX_TARGET_RPI
  /* aspect ratio */
  {
    OMX_ERRORTYPE err;
    OMX_CONFIG_POINTTYPE aspect_ratio_param;

    GST_OMX_INIT_STRUCT (&aspect_ratio_param);
    aspect_ratio_param.nPortIndex = self->enc_out_port->index;

    err = gst_omx_component_get_parameter (self->enc,
        OMX_IndexParamBrcmPixelAspectRatio, &aspect_ratio_param);

    if (err == OMX_ErrorNone) {

      aspect_ratio_param.nX = info->par_n;
      aspect_ratio_param.nY = info->par_d;

      err =
          gst_omx_component_set_parameter (self->enc,
          OMX_IndexParamBrcmPixelAspectRatio, &aspect_ratio_param);

      if (err == OMX_ErrorUnsupportedIndex) {
        GST_WARNING_OBJECT (self,
            "Setting aspect ratio parameters not supported by the component");
      } else if (err == OMX_ErrorUnsupportedSetting) {
        GST_WARNING_OBJECT (self,
            "Setting aspect ratio %u %u not supported by the component",
            aspect_ratio_param.nX, aspect_ratio_param.nY);
      } else if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self,
            "Failed to set aspect ratio: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        return FALSE;
      }
    }
  }
#endif // USE_OMX_TARGET_RPI

  if (klass->set_format) {
    if (!klass->set_format (self, self->enc_in_port, state)) {
      GST_ERROR_OBJECT (self, "Subclass failed to set the new format");
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (self, "Updating ports definition");
  if (gst_omx_port_update_port_definition (self->enc_out_port,
          NULL) != OMX_ErrorNone)
    return FALSE;
  if (gst_omx_port_update_port_definition (self->enc_in_port,
          NULL) != OMX_ErrorNone)
    return FALSE;

  /* Some OMX implementations reset the bitrate after setting the compression
   * format, see bgo#698049, so re-set it */
  gst_omx_video_enc_set_bitrate (self);

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = gst_video_codec_state_ref (state);

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  gst_omx_video_enc_set_latency (self);
  if (!gst_omx_video_enc_set_color_primaries (self))
    return FALSE;

  {
    GstCapsFeatures *features;

    features = gst_caps_get_features (state->caps, 0);
    if (features
        && gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_XLNX_LL)) {
      GST_DEBUG_OBJECT (self, "Input is using XLNX-LowLatency");
      self->xlnx_ll = TRUE;
    }
  }
#endif

  self->downstream_flow_ret = GST_FLOW_OK;
  return TRUE;
}

static gboolean
gst_omx_video_enc_flush (GstVideoEncoder * encoder)
{
  GstOMXVideoEnc *self;

  self = GST_OMX_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Flushing encoder");

  if (gst_omx_component_get_state (self->enc, 0) == OMX_StateLoaded)
    return TRUE;

  /* 0) Pause the components */
  if (gst_omx_component_get_state (self->enc, 0) == OMX_StateExecuting) {
    gst_omx_component_set_state (self->enc, OMX_StatePause);
    gst_omx_component_get_state (self->enc, GST_CLOCK_TIME_NONE);
  }

  /* 1) Flush the ports */
  GST_DEBUG_OBJECT (self, "flushing ports");
  gst_omx_port_set_flushing (self->enc_in_port, 5 * GST_SECOND, TRUE);
  gst_omx_port_set_flushing (self->enc_out_port, 5 * GST_SECOND, TRUE);

  /* Wait until the srcpad loop is finished,
   * unlock GST_VIDEO_ENCODER_STREAM_LOCK to prevent deadlocks
   * caused by using this lock from inside the loop function */
  GST_VIDEO_ENCODER_STREAM_UNLOCK (self);
  GST_PAD_STREAM_LOCK (GST_VIDEO_ENCODER_SRC_PAD (self));
  GST_PAD_STREAM_UNLOCK (GST_VIDEO_ENCODER_SRC_PAD (self));
  GST_VIDEO_ENCODER_STREAM_LOCK (self);

  /* 3) Resume components */
  gst_omx_component_set_state (self->enc, OMX_StateExecuting);
  gst_omx_component_get_state (self->enc, GST_CLOCK_TIME_NONE);

  /* 4) Unset flushing to allow ports to accept data again */
  gst_omx_port_set_flushing (self->enc_in_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_set_flushing (self->enc_out_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_populate (self->enc_out_port);

  /* Start the srcpad loop again */
  self->last_upstream_ts = 0;
  self->downstream_flow_ret = GST_FLOW_OK;
  self->started = FALSE;
  GST_DEBUG_OBJECT (self, "Flush finished");

  return TRUE;
}

static gboolean
gst_omx_video_enc_copy_plane (GstOMXVideoEnc * self, guint i,
    GstVideoFrame * frame, GstOMXBuffer * outbuf,
    const GstVideoFormatInfo * finfo)
{
  OMX_PARAM_PORTDEFINITIONTYPE *port_def = &self->enc_in_port->port_def;
  guint8 *src, *dest;
  gint src_stride, dest_stride;
  gint j, height, width;

  src_stride = GST_VIDEO_FRAME_COMP_STRIDE (frame, i);
  dest_stride = port_def->format.video.nStride;
  /* XXX: Try this if no stride was set */
  if (dest_stride == 0)
    dest_stride = src_stride;

  dest = outbuf->omx_buf->pBuffer + outbuf->omx_buf->nOffset;
  if (i == 1)
    dest +=
        port_def->format.video.nSliceHeight * port_def->format.video.nStride;

  src = GST_VIDEO_FRAME_COMP_DATA (frame, i);
  height = GST_VIDEO_FRAME_COMP_HEIGHT (frame, i);
  width = GST_VIDEO_FRAME_COMP_WIDTH (frame, i) * (i == 0 ? 1 : 2);

  if (GST_VIDEO_FORMAT_INFO_BITS (finfo) == 10)
    /* Need ((width + 2) / 3) 32-bits words */
    width = (width + 2) / 3 * 4;

  if (dest + dest_stride * height >
      outbuf->omx_buf->pBuffer + outbuf->omx_buf->nAllocLen) {
    GST_ERROR_OBJECT (self, "Invalid output buffer size");
    return FALSE;
  }

  for (j = 0; j < height; j++) {
    memcpy (dest, src, width);
    src += src_stride;
    dest += dest_stride;
  }

  /* nFilledLen should include the vertical padding in each slice (spec 3.1.3.7.1) */
  outbuf->omx_buf->nFilledLen +=
      GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (finfo, i,
      port_def->format.video.nSliceHeight) * port_def->format.video.nStride;
  return TRUE;
}

static gboolean
gst_omx_video_enc_semi_planar_manual_copy (GstOMXVideoEnc * self,
    GstBuffer * inbuf, GstOMXBuffer * outbuf, const GstVideoFormatInfo * finfo)
{
  GstVideoInfo *info = &self->input_state->info;
  GstVideoFrame frame;
  gint i;

  outbuf->omx_buf->nFilledLen = 0;

  if (!gst_video_frame_map (&frame, info, inbuf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Invalid input buffer size");
    return FALSE;
  }

  for (i = 0; i < 2; i++) {
    if (!gst_omx_video_enc_copy_plane (self, i, &frame, outbuf, finfo)) {
      gst_video_frame_unmap (&frame);
      return FALSE;
    }
  }

  gst_video_frame_unmap (&frame);
  return TRUE;
}

static gboolean
gst_omx_video_enc_fill_buffer (GstOMXVideoEnc * self, GstBuffer * inbuf,
    GstOMXBuffer * outbuf)
{
  GstVideoCodecState *state = gst_video_codec_state_ref (self->input_state);
  GstVideoInfo *info = &state->info;
  OMX_PARAM_PORTDEFINITIONTYPE *port_def = &self->enc_in_port->port_def;
  gboolean ret = FALSE;
  GstVideoFrame frame;
  GstVideoMeta *meta = gst_buffer_get_video_meta (inbuf);
  gint stride = meta ? meta->stride[0] : info->stride[0];

#ifndef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  //HACK: Seen that the change of resolution in OMX stack is async
  //we might receive buffers which are still having the previous resolution
  if (info->width != port_def->format.video.nFrameWidth ||
      GST_VIDEO_INFO_FIELD_HEIGHT (info) !=
      port_def->format.video.nFrameHeight) {
    GST_ERROR_OBJECT (self, "Width or height do not match");
    goto done;
  }
#endif

  if (self->enc_in_port->allocation ==
      GST_OMX_BUFFER_ALLOCATION_USE_BUFFER_DYNAMIC) {
    if (gst_buffer_n_memory (inbuf) > 1) {
      GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
          ("input buffer now has more than one memory, can't use dynamic allocation any more"));
      return FALSE;
    }

    if (!self->input_dmabuf) {
      /* Map and keep a ref on the buffer while it's being processed
       * by the OMX component. */
      if (!gst_omx_buffer_map_frame (outbuf, inbuf, info)) {
        GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
            ("failed to map input buffer"));
        return FALSE;
      }

      if (!check_input_alignment (self, &outbuf->input_frame.map[0])) {
        GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
            ("input buffer now has wrong alignment/stride, can't use dynamic allocation any more"));
        return FALSE;
      }

      GST_LOG_OBJECT (self, "Transfer buffer of %" G_GSIZE_FORMAT " bytes",
          gst_buffer_get_size (inbuf));
    } else {
      /* dmabuf input */
      if (!gst_omx_buffer_import_fd (outbuf, inbuf)) {
        GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
            ("failed to import dmabuf"));
        return FALSE;
      }

      GST_LOG_OBJECT (self, "Import dmabuf of %" G_GSIZE_FORMAT " bytes",
          gst_buffer_get_size (inbuf));
    }

    ret = TRUE;
    goto done;
  }

  /* Same strides and everything */
  if ((gst_buffer_get_size (inbuf) ==
          outbuf->omx_buf->nAllocLen - outbuf->omx_buf->nOffset) &&
      (stride == port_def->format.video.nStride)) {
    outbuf->omx_buf->nFilledLen = gst_buffer_get_size (inbuf);

    GST_LOG_OBJECT (self, "Matched strides - direct copy %u bytes",
        (guint) outbuf->omx_buf->nFilledLen);

    gst_buffer_extract (inbuf, 0,
        outbuf->omx_buf->pBuffer + outbuf->omx_buf->nOffset,
        outbuf->omx_buf->nFilledLen);
    ret = TRUE;
    goto done;
  }

  /* Different strides */
  GST_LOG_OBJECT (self, "Mismatched strides - copying line-by-line");

  switch (info->finfo->format) {
    case GST_VIDEO_FORMAT_I420:{
      gint i, j, height, width;
      guint8 *src, *dest;
      gint src_stride, dest_stride;

      outbuf->omx_buf->nFilledLen = 0;

      if (!gst_video_frame_map (&frame, info, inbuf, GST_MAP_READ)) {
        GST_ERROR_OBJECT (self, "Invalid input buffer size");
        ret = FALSE;
        goto done;
      }

      for (i = 0; i < 3; i++) {
        if (i == 0) {
          dest_stride = port_def->format.video.nStride;
        } else {
          dest_stride = port_def->format.video.nStride / 2;
        }

        src_stride = GST_VIDEO_FRAME_COMP_STRIDE (&frame, i);
        /* XXX: Try this if no stride was set */
        if (dest_stride == 0)
          dest_stride = src_stride;

        dest = outbuf->omx_buf->pBuffer + outbuf->omx_buf->nOffset;
        if (i > 0)
          dest +=
              port_def->format.video.nSliceHeight *
              port_def->format.video.nStride;
        if (i == 2)
          dest +=
              (port_def->format.video.nSliceHeight / 2) *
              (port_def->format.video.nStride / 2);

        src = GST_VIDEO_FRAME_COMP_DATA (&frame, i);
        height = GST_VIDEO_FRAME_COMP_HEIGHT (&frame, i);
        width = GST_VIDEO_FRAME_COMP_WIDTH (&frame, i);

        if (dest + dest_stride * height >
            outbuf->omx_buf->pBuffer + outbuf->omx_buf->nAllocLen) {
          gst_video_frame_unmap (&frame);
          GST_ERROR_OBJECT (self, "Invalid output buffer size");
          ret = FALSE;
          goto done;
        }

        for (j = 0; j < height; j++) {
          memcpy (dest, src, width);
          src += src_stride;
          dest += dest_stride;
        }

        /* nFilledLen should include the vertical padding in each slice (spec 3.1.3.7.1) */
        if (i == 0)
          outbuf->omx_buf->nFilledLen +=
              port_def->format.video.nSliceHeight *
              port_def->format.video.nStride;
        else
          outbuf->omx_buf->nFilledLen +=
              (port_def->format.video.nSliceHeight / 2) *
              (port_def->format.video.nStride / 2);
      }
      gst_video_frame_unmap (&frame);
      ret = TRUE;
      break;
    }
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_NV12_10LE32:
    case GST_VIDEO_FORMAT_NV16_10LE32:
      ret =
          gst_omx_video_enc_semi_planar_manual_copy (self, inbuf, outbuf,
          info->finfo);
      break;
    case GST_VIDEO_FORMAT_GRAY8:
    {
      if (!gst_video_frame_map (&frame, info, inbuf, GST_MAP_READ)) {
        GST_ERROR_OBJECT (self, "Failed to map input buffer");
        ret = FALSE;
        goto done;
      }

      ret = gst_omx_video_enc_copy_plane (self, 0, &frame, outbuf, info->finfo);
      gst_video_frame_unmap (&frame);
    }
      break;
    default:
      GST_ERROR_OBJECT (self, "Unsupported format");
      goto done;
      break;
  }

done:

  gst_video_codec_state_unref (state);

  return ret;
}

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
static void
handle_roi_metadata (GstOMXVideoEnc * self, GstBuffer * input)
{
  GstMeta *meta;
  gpointer state = NULL;

  while ((meta =
          gst_buffer_iterate_meta_filtered (input, &state,
              GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE))) {
    GstVideoRegionOfInterestMeta *roi = (GstVideoRegionOfInterestMeta *) meta;
    OMX_ALG_VIDEO_CONFIG_REGION_OF_INTEREST roi_param;
    GstStructure *s;

    GST_LOG_OBJECT (self, "Input buffer ROI: type=%s id=%d (%d, %d) %dx%d",
        g_quark_to_string (roi->roi_type), roi->id, roi->x, roi->y, roi->w,
        roi->h);

    if (self->qp_mode != ROI_QP) {
      GST_WARNING_OBJECT (self,
          "Need qp-mode=roi to handle ROI metadata (current: %d); ignoring",
          self->qp_mode);
      continue;
    }

    GST_OMX_INIT_STRUCT (&roi_param);
    roi_param.nPortIndex = self->enc_in_port->index;
    roi_param.nLeft = roi->x;
    roi_param.nTop = roi->y;
    roi_param.nWidth = roi->w;
    roi_param.nHeight = roi->h;

    s = gst_video_region_of_interest_meta_get_param (roi, "roi/omx-alg");
    if (s) {
      const gchar *quality;
      GEnumValue *evalue;

      quality = gst_structure_get_string (s, "quality");

      evalue =
          g_enum_get_value_by_nick (self->alg_roi_quality_enum_class, quality);
      if (!evalue) {
        roi_param.eQuality = self->default_roi_quality;

        GST_WARNING_OBJECT (self,
            "Unknown ROI encoding quality '%s', use default (%d)",
            quality, self->default_roi_quality);
      } else {
        roi_param.eQuality = evalue->value;

        GST_LOG_OBJECT (self, "Use encoding quality '%s' from upstream",
            quality);
      }
    } else {
      roi_param.eQuality = self->default_roi_quality;

      GST_LOG_OBJECT (self, "No quality specified upstream, use default (%d)",
          self->default_roi_quality);
    }

    gst_omx_component_set_config (self->enc,
        (OMX_INDEXTYPE) OMX_ALG_IndexConfigVideoRegionOfInterest, &roi_param);
  }
}
#endif

static GstFlowReturn
gst_omx_video_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstOMXAcquireBufferReturn acq_ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXVideoEnc *self;
  GstOMXPort *port;
  GstOMXBuffer *buf;
  OMX_ERRORTYPE err;
  GstClockTimeDiff deadline;
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  gboolean starting = FALSE;
#endif

  self = GST_OMX_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Handling frame");

  if (self->downstream_flow_ret != GST_FLOW_OK) {
    gst_video_codec_frame_unref (frame);
    return self->downstream_flow_ret;
  }

  deadline = gst_video_encoder_get_max_encode_time (encoder, frame);
  if (deadline < 0) {
    GST_WARNING_OBJECT (self,
        "Input frame is too late, dropping (deadline %" GST_TIME_FORMAT ")",
        GST_TIME_ARGS (-deadline));

    /* Calling finish_frame with frame->output_buffer == NULL will drop it */
    return gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (self), frame);
  }

  if (!self->started) {
    if (gst_omx_port_is_flushing (self->enc_out_port)) {
      if (!gst_omx_video_enc_enable (self, frame->input_buffer))
        goto enable_error;
    }

    GST_DEBUG_OBJECT (self, "Starting task");
    gst_pad_start_task (GST_VIDEO_ENCODER_SRC_PAD (self),
        (GstTaskFunction) gst_omx_video_enc_loop, self, NULL);
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
    starting = TRUE;
#endif
  }

  port = self->enc_in_port;

  while (acq_ret != GST_OMX_ACQUIRE_BUFFER_OK) {
    GstClockTime timestamp, duration;
    gboolean fill_buffer = TRUE;

    /* Make sure to release the base class stream lock, otherwise
     * _loop() can't call _finish_frame() and we might block forever
     * because no input buffers are released */
    GST_VIDEO_ENCODER_STREAM_UNLOCK (self);

    if (buffer_is_from_input_pool (self, frame->input_buffer)) {
      /* Receiving a buffer from our input pool */
      buf = get_omx_buf (frame->input_buffer);

      GST_LOG_OBJECT (self,
          "Input buffer %p already has a OMX buffer associated: %p",
          frame->input_buffer, buf);

      g_assert (!buf->input_buffer);
      /* Prevent the buffer to be released to the pool while it's being
       * processed by OMX. The reference will be dropped in EmptyBufferDone() */
      buf->input_buffer = gst_buffer_ref (frame->input_buffer);

      acq_ret = GST_OMX_ACQUIRE_BUFFER_OK;
      fill_buffer = FALSE;
      buf->omx_buf->nFilledLen = gst_buffer_get_size (frame->input_buffer);
    } else {
      acq_ret = gst_omx_port_acquire_buffer (port, &buf, GST_OMX_WAIT);
    }

    if (acq_ret == GST_OMX_ACQUIRE_BUFFER_ERROR) {
      GST_VIDEO_ENCODER_STREAM_LOCK (self);
      goto component_error;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
      GST_VIDEO_ENCODER_STREAM_LOCK (self);
      goto flushing;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      /* Reallocate all buffers */
      err = gst_omx_port_set_enabled (port, FALSE);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_buffers_released (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_deallocate_buffers (port);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      if (!gst_omx_video_enc_ensure_nb_in_buffers (self)) {
        GST_VIDEO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_set_enabled (port, TRUE);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      if (!gst_omx_video_enc_allocate_in_buffers (self)) {
        GST_VIDEO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_mark_reconfigured (port);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      /* Now get a new buffer and fill it */
      GST_VIDEO_ENCODER_STREAM_LOCK (self);
      continue;
    }
    GST_VIDEO_ENCODER_STREAM_LOCK (self);

    g_assert (acq_ret == GST_OMX_ACQUIRE_BUFFER_OK && buf != NULL);

    if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <= 0) {
      gst_omx_port_release_buffer (port, buf);
      goto full_buffer;
    }

    if (self->downstream_flow_ret != GST_FLOW_OK) {
      gst_omx_port_release_buffer (port, buf);
      goto flow_error;
    }

    /* Now handle the frame */

    if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
#ifdef USE_OMX_TARGET_RPI
      OMX_CONFIG_BOOLEANTYPE config;

      GST_OMX_INIT_STRUCT (&config);
      config.bEnabled = OMX_TRUE;

      GST_DEBUG_OBJECT (self, "Forcing a keyframe (iframe on the RPi)");

      err =
          gst_omx_component_set_config (self->enc,
          OMX_IndexConfigBrcmVideoRequestIFrame, &config);
#elif defined(USE_OMX_TARGET_ZYNQ_USCALE_PLUS)
      OMX_ALG_VIDEO_CONFIG_INSERT config;

      GST_OMX_INIT_STRUCT (&config);
      config.nPortIndex = self->enc_out_port->index;

      GST_DEBUG_OBJECT (self, "Forcing a keyframe");
      err = gst_omx_component_set_config (self->enc, (OMX_INDEXTYPE)
          OMX_ALG_IndexConfigVideoInsertInstantaneousDecodingRefresh, &config);
#else
      OMX_CONFIG_INTRAREFRESHVOPTYPE config;

      GST_OMX_INIT_STRUCT (&config);
      config.nPortIndex = port->index;
      config.IntraRefreshVOP = OMX_TRUE;

      GST_DEBUG_OBJECT (self, "Forcing a keyframe");
      err =
          gst_omx_component_set_config (self->enc,
          OMX_IndexConfigVideoIntraVOPRefresh, &config);
#endif
      if (err != OMX_ErrorNone)
        GST_ERROR_OBJECT (self, "Failed to force a keyframe: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
    }
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
    handle_roi_metadata (self, frame->input_buffer);
#endif

    /* Copy the buffer content in chunks of size as requested
     * by the port */
    if (fill_buffer
        && !gst_omx_video_enc_fill_buffer (self, frame->input_buffer, buf)) {
      gst_omx_port_release_buffer (port, buf);
      goto buffer_fill_error;
    }

    timestamp = frame->pts;
    if (timestamp != GST_CLOCK_TIME_NONE) {
      GST_OMX_SET_TICKS (buf->omx_buf->nTimeStamp,
          gst_util_uint64_scale (timestamp, OMX_TICKS_PER_SECOND, GST_SECOND));
      self->last_upstream_ts = timestamp;
    }

    duration = frame->duration;
    if (duration != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTickCount =
          gst_util_uint64_scale (duration, OMX_TICKS_PER_SECOND, GST_SECOND);
      self->last_upstream_ts += duration;
    } else {
      buf->omx_buf->nTickCount = 0;
    }

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
    if (GST_VIDEO_BUFFER_IS_TOP_FIELD (frame->input_buffer))
      buf->omx_buf->nFlags |= OMX_ALG_BUFFERFLAG_TOP_FIELD;
    else if (GST_VIDEO_BUFFER_IS_BOTTOM_FIELD (frame->input_buffer))
      buf->omx_buf->nFlags |= OMX_ALG_BUFFERFLAG_BOT_FIELD;
#endif

    self->started = TRUE;
    err = gst_omx_port_release_buffer (port, buf);
    if (err != OMX_ErrorNone)
      goto release_error;

    GST_DEBUG_OBJECT (self, "Passed frame to component");
  }

  gst_video_codec_frame_unref (frame);

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  if (self->xlnx_ll && starting && !self->in_pool_used) {
    GstEvent *event;

    GST_DEBUG_OBJECT (self, "Tell XLNX-LL producer it can start streaming");

    event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
        gst_structure_new ("xlnx-ll-consumer-ready", NULL, NULL));

    gst_pad_push_event (GST_VIDEO_DECODER_SINK_PAD (self), event);
  }
#endif

  return self->downstream_flow_ret;

full_buffer:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Got OpenMAX buffer with no free space (%p, %u/%u)", buf,
            (guint) buf->omx_buf->nOffset, (guint) buf->omx_buf->nAllocLen));
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }

flow_error:
  {
    gst_video_codec_frame_unref (frame);
    return self->downstream_flow_ret;
  }

enable_error:
  {
    /* Report the OMX error, if any */
    if (gst_omx_component_get_last_error (self->enc) != OMX_ErrorNone)
      GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
          ("Failed to enable OMX encoder: %s (0x%08x)",
              gst_omx_component_get_last_error_string (self->enc),
              gst_omx_component_get_last_error (self->enc)));
    else
      GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
          ("Failed to enable OMX encoder"));
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }

component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->enc),
            gst_omx_component_get_last_error (self->enc)));
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }

flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- returning FLUSHING");
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_FLUSHING;
  }
reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure input port"));
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }
buffer_fill_error:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE, (NULL),
        ("Failed to write input into the OpenMAX buffer"));
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }
release_error:
  {
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to relase input buffer to component: %s (0x%08x)",
            gst_omx_error_to_string (err), err));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_omx_video_enc_finish (GstVideoEncoder * encoder)
{
  GstOMXVideoEnc *self;

  self = GST_OMX_VIDEO_ENC (encoder);

  return gst_omx_video_enc_drain (self);
}

static GstFlowReturn
gst_omx_video_enc_drain (GstOMXVideoEnc * self)
{
  GstOMXVideoEncClass *klass;
  GstOMXBuffer *buf;
  GstOMXAcquireBufferReturn acq_ret;
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (self, "Draining component");

  klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);

  if (!self->started) {
    GST_DEBUG_OBJECT (self, "Component not started yet");
    return GST_FLOW_OK;
  }
  self->started = FALSE;

  if ((klass->cdata.hacks & GST_OMX_HACK_NO_EMPTY_EOS_BUFFER)) {
    GST_WARNING_OBJECT (self, "Component does not support empty EOS buffers");
    return GST_FLOW_OK;
  }

  /* Make sure to release the base class stream lock, otherwise
   * _loop() can't call _finish_frame() and we might block forever
   * because no input buffers are released */
  GST_VIDEO_ENCODER_STREAM_UNLOCK (self);

  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port. */
  acq_ret = gst_omx_port_acquire_buffer (self->enc_in_port, &buf, GST_OMX_WAIT);
  if (acq_ret != GST_OMX_ACQUIRE_BUFFER_OK) {
    GST_VIDEO_ENCODER_STREAM_LOCK (self);
    GST_ERROR_OBJECT (self, "Failed to acquire buffer for draining: %d",
        acq_ret);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&self->drain_lock);
  self->draining = TRUE;
  buf->omx_buf->nFilledLen = 0;
  GST_OMX_SET_TICKS (buf->omx_buf->nTimeStamp,
      gst_util_uint64_scale (self->last_upstream_ts, OMX_TICKS_PER_SECOND,
          GST_SECOND));
  buf->omx_buf->nTickCount = 0;
  buf->omx_buf->nFlags |= OMX_BUFFERFLAG_EOS;
  err = gst_omx_port_release_buffer (self->enc_in_port, buf);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to drain component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    g_mutex_unlock (&self->drain_lock);
    GST_VIDEO_ENCODER_STREAM_LOCK (self);
    return GST_FLOW_ERROR;
  }
  GST_DEBUG_OBJECT (self, "Waiting until component is drained");
  g_cond_wait (&self->drain_cond, &self->drain_lock);
  GST_DEBUG_OBJECT (self, "Drained component");
  g_mutex_unlock (&self->drain_lock);
  GST_VIDEO_ENCODER_STREAM_LOCK (self);

  self->started = FALSE;

  return GST_FLOW_OK;
}

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
static gboolean
pool_request_allocate_cb (GstBufferPool * pool, GstOMXVideoEnc * self)
{
  GstStructure *config;
  guint min;

  gst_omx_port_set_dmabuf (self->enc_in_port, TRUE);

  config = gst_buffer_pool_get_config (pool);

  if (!gst_buffer_pool_config_get_params (config, NULL, NULL, &min, NULL)) {
    gst_structure_free (config);
    return FALSE;
  }
  gst_structure_free (config);

  GST_DEBUG_OBJECT (self,
      "input pool configured for %d buffers, adjust nBufferCountActual", min);

  if (!gst_omx_port_update_buffer_count_actual (self->enc_in_port, min))
    return FALSE;

  if (!gst_omx_video_enc_set_to_idle (self))
    return FALSE;

  self->input_allocation = GST_OMX_BUFFER_ALLOCATION_ALLOCATE_BUFFER;
  self->input_dmabuf = TRUE;

  /* gst_omx_port_acquire_buffer() will fail if the input port is stil flushing
   * which will prevent upstream from acquiring buffers. */
  gst_omx_port_set_flushing (self->enc_in_port, 5 * GST_SECOND, FALSE);

  return TRUE;
}

static GstBufferPool *
create_input_pool (GstOMXVideoEnc * self, GstCaps * caps, guint num_buffers)
{
  GstBufferPool *pool;
  GstStructure *config;

  pool =
      gst_omx_buffer_pool_new (GST_ELEMENT_CAST (self), self->enc,
      self->enc_in_port, GST_OMX_BUFFER_MODE_DMABUF);

  g_signal_connect_object (pool, "allocate",
      G_CALLBACK (pool_request_allocate_cb), self, 0);

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps,
      self->enc_in_port->port_def.nBufferSize, num_buffers, 0);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_INFO_OBJECT (self, "Failed to set config on input pool");
    gst_object_unref (pool);
    return NULL;
  }

  return pool;
}
#endif

static GstStructure *
get_allocation_video_meta (GstOMXVideoEnc * self, GstVideoInfo * info)
{
  GstStructure *result;
  GstVideoAlignment align;

  gst_omx_video_get_port_padding (self->enc_in_port, info, &align);

  result = gst_structure_new_empty ("video-meta");

  gst_structure_set (result, "padding-top", G_TYPE_UINT, align.padding_top,
      "padding-bottom", G_TYPE_UINT, align.padding_bottom,
      "padding-left", G_TYPE_UINT, align.padding_left,
      "padding-right", G_TYPE_UINT, align.padding_right, NULL);

  GST_LOG_OBJECT (self, "Request buffer layout to producer: %" GST_PTR_FORMAT,
      result);

  return result;
}

static gboolean
gst_omx_video_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query)
{
  GstOMXVideoEnc *self = GST_OMX_VIDEO_ENC (encoder);
  guint num_buffers;
  GstCaps *caps;
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *params;

  gst_query_parse_allocation (query, &caps, NULL);

  if (!caps) {
    GST_WARNING_OBJECT (self, "allocation query does not contain caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  params = get_allocation_video_meta (self, &info);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, params);
  gst_structure_free (params);

  num_buffers = self->enc_in_port->port_def.nBufferCountMin + 1;

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  if (g_getenv ("ENC_EXTRA_IP_BUFFERS") != NULL)
    num_buffers += atoi (g_getenv ("ENC_EXTRA_IP_BUFFERS"));

  /* dmabuf export is currently only supported on Zynqultrascaleplus */
  pool = create_input_pool (self, caps, num_buffers);
  if (!pool) {
    GST_WARNING_OBJECT (self, "Failed to create and configure pool");
    return FALSE;
  }
#endif

  GST_DEBUG_OBJECT (self,
      "request at least %d buffers of size %d", num_buffers,
      (guint) self->enc_in_port->port_def.nBufferSize);
  gst_query_add_allocation_pool (query, pool,
      self->enc_in_port->port_def.nBufferSize, num_buffers, 0);

  self->in_pool_used = FALSE;

  g_clear_object (&pool);

  return
      GST_VIDEO_ENCODER_CLASS
      (gst_omx_video_enc_parent_class)->propose_allocation (encoder, query);
}

static GList *
filter_supported_formats (GList * negotiation_map)
{
  GList *cur;

  for (cur = negotiation_map; cur != NULL;) {
    GstOMXVideoNegotiationMap *nmap = (GstOMXVideoNegotiationMap *) (cur->data);
    GList *next;

    switch (nmap->format) {
      case GST_VIDEO_FORMAT_I420:
      case GST_VIDEO_FORMAT_NV12:
      case GST_VIDEO_FORMAT_NV12_10LE32:
      case GST_VIDEO_FORMAT_NV16:
      case GST_VIDEO_FORMAT_NV16_10LE32:
      case GST_VIDEO_FORMAT_GRAY8:
        cur = g_list_next (cur);
        continue;
      default:
        gst_omx_video_negotiation_map_free (nmap);
        next = g_list_next (cur);
        negotiation_map = g_list_delete_link (negotiation_map, cur);
        cur = next;
    }
  }

  return negotiation_map;
}

static GstCaps *
add_interlace_to_caps (GstOMXVideoEnc * self, GstCaps * caps)
{
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  OMX_ERRORTYPE err;
  OMX_INTERLACEFORMATTYPE interlace_format_param;
  GstCaps *caps_alternate;

  if (gst_caps_is_empty (caps))
    /* No caps to add to */
    return caps;

  GST_OMX_INIT_STRUCT (&interlace_format_param);
  interlace_format_param.nPortIndex = self->enc_in_port->index;

  err = gst_omx_component_get_parameter (self->enc,
      OMX_ALG_IndexParamVideoInterlaceFormatSupported, &interlace_format_param);

  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self,
        "Failed to get OMX_ALG_IndexParamVideoInterlaceFormatSupported %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return caps;
  }

  if (!(interlace_format_param.nFormat &
          OMX_ALG_InterlaceAlternateTopFieldFirst)
      && !(interlace_format_param.nFormat &
          OMX_ALG_InterlaceAlternateBottomFieldFirst))
    return caps;

  /* Alternate mode is supported, create an 'alternate' variant of the caps
   * with the caps feature. */
  caps_alternate = gst_caps_copy (caps);

  gst_caps_set_features_simple (caps_alternate,
      gst_caps_features_new (GST_CAPS_FEATURE_FORMAT_INTERLACED, NULL));

  caps = gst_caps_merge (caps, caps_alternate);
#endif // USE_OMX_TARGET_ZYNQ_USCALE_PLUS

  return caps;
}

static GstCaps *
gst_omx_video_enc_getcaps (GstVideoEncoder * encoder, GstCaps * filter)
{
  GstOMXVideoEnc *self = GST_OMX_VIDEO_ENC (encoder);
  GList *negotiation_map = NULL;
  GstCaps *comp_supported_caps;
  GstCaps *ret;

  if (!self->enc)
    return gst_video_encoder_proxy_getcaps (encoder, NULL, filter);

  negotiation_map =
      gst_omx_video_get_supported_colorformats (self->enc_in_port,
      self->input_state);
  negotiation_map = filter_supported_formats (negotiation_map);

  comp_supported_caps = gst_omx_video_get_caps_for_map (negotiation_map);
  g_list_free_full (negotiation_map,
      (GDestroyNotify) gst_omx_video_negotiation_map_free);

  comp_supported_caps = add_interlace_to_caps (self, comp_supported_caps);

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  comp_supported_caps =
      gst_omx_video_add_xlnx_ll_to_caps (comp_supported_caps, TRUE);
#endif

  if (!gst_caps_is_empty (comp_supported_caps)) {
    ret =
        gst_video_encoder_proxy_getcaps (encoder, comp_supported_caps, filter);
    gst_caps_unref (comp_supported_caps);
  } else {
    gst_caps_unref (comp_supported_caps);
    ret = gst_video_encoder_proxy_getcaps (encoder, NULL, filter);
  }

  GST_LOG_OBJECT (encoder, "Supported caps %" GST_PTR_FORMAT, ret);

  return ret;
}

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
static gboolean
handle_scene_change_event (GstOMXVideoEnc * self, GstEvent * event)
{
  guint look_ahead;
  const GstStructure *s;
  OMX_ALG_VIDEO_CONFIG_NOTIFY_SCENE_CHANGE sc;
  OMX_ERRORTYPE err;

  s = gst_event_get_structure (event);

  if (!gst_structure_get_uint (s, "look-ahead", &look_ahead)) {
    GST_WARNING_OBJECT (self,
        "missing look-ahead field in scene-change event; ignoring");
    return FALSE;
  }

  GST_LOG_OBJECT (self, "received scene-change event (look-ahead: %d)",
      look_ahead);

  GST_OMX_INIT_STRUCT (&sc);
  sc.nPortIndex = self->enc_in_port->index;
  sc.nLookAhead = look_ahead;
  err =
      gst_omx_component_set_config (self->enc,
      (OMX_INDEXTYPE) OMX_ALG_IndexConfigVideoNotifySceneChange, &sc);

  if (err != OMX_ErrorNone)
    GST_ERROR_OBJECT (self,
        "Failed to notify scene change: %s (0x%08x)",
        gst_omx_error_to_string (err), err);

  return TRUE;
}

static gboolean
handle_longterm_event (GstOMXVideoEnc * self, GstEvent * event)
{
  OMX_ALG_VIDEO_CONFIG_INSERT longterm;
  OMX_ERRORTYPE err;
  OMX_INDEXTYPE omx_index_long_term;

  GST_OMX_INIT_STRUCT (&longterm);
  longterm.nPortIndex = self->enc_in_port->index;

  /* If long-term-ref is enabled then "omx-alg/insert-longterm" event
   * marks the encoding picture as long term reference picture and
   * "omx-alg/use-longterm" event informs the encoder that encoding picture
   * should use existing long term picture in the dpb as reference for encoding process */

  if (self->long_term_ref) {
    if (gst_event_has_name (event, OMX_ALG_GST_EVENT_INSERT_LONGTERM)) {
      GST_LOG_OBJECT (self, "received omx-alg/insert-longterm event");
      omx_index_long_term =
          (OMX_INDEXTYPE) OMX_ALG_IndexConfigVideoInsertLongTerm;
    } else {
      GST_LOG_OBJECT (self, "received omx-alg/use-longterm event");
      omx_index_long_term = (OMX_INDEXTYPE) OMX_ALG_IndexConfigVideoUseLongTerm;
    }

    err =
        gst_omx_component_set_config (self->enc, omx_index_long_term,
        &longterm);

    if (err != OMX_ErrorNone)
      GST_ERROR_OBJECT (self,
          "Failed to longterm events: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
  } else {
    GST_WARNING_OBJECT (self,
        "LongTerm events are not handled because long_term_ref is disabled");
  }

  return TRUE;
}

static gboolean
handle_load_qp (GstOMXVideoEnc * self, GstEvent * event)
{
  const GstStructure *s;
  GstBuffer *buf;
  GstMapInfo map;
  OMX_ALG_VIDEO_CONFIG_DATA config;
  OMX_ERRORTYPE err;

  s = gst_event_get_structure (event);

  if (!gst_structure_get (s, "qp-table", GST_TYPE_BUFFER, &buf, NULL)) {
    GST_WARNING_OBJECT (self, "Failed to parse load-qp event");
    goto load_qp_done;
  }

  if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
    GST_WARNING_OBJECT (self, "Failed to map payload buffer");
    gst_buffer_unref (buf);
    goto load_qp_done;
  }

  GST_OMX_INIT_STRUCT (&config);
  config.pBuffer = map.data;
  config.nAllocLen = map.size;
  config.nFilledLen = map.size;
  config.nOffset =
      GST_BUFFER_OFFSET_IS_VALID (buf) ? GST_BUFFER_OFFSET (buf) : 0;
  err = gst_omx_component_set_config (self->enc,
      (OMX_INDEXTYPE) OMX_ALG_IndexConfigVideoQuantizationParameterTable,
      &config);

  if (err != OMX_ErrorNone)
    GST_ERROR_OBJECT (self,
        "Failed to load qp: %s (0x%08x)", gst_omx_error_to_string (err), err);

  gst_buffer_unmap (buf, &map);
  gst_buffer_unref (buf);

load_qp_done:
  return TRUE;
}

static gboolean
handle_sei_insertion (GstOMXVideoEnc * self, GstEvent * event)
{
  const GstStructure *s;
  gboolean prefix;
  OMX_ALG_VIDEO_CONFIG_SEI config;
  OMX_ERRORTYPE err;
  guint32 payload_type;
  GstBuffer *buf;
  GstMapInfo map;
  OMX_INDEXTYPE index;

  prefix = gst_event_has_name (event, OMX_ALG_GST_EVENT_INSERT_PREFIX_SEI);
  s = gst_event_get_structure (event);

  if (!gst_structure_get (s, "payload-type", G_TYPE_UINT, &payload_type,
          "payload", GST_TYPE_BUFFER, &buf, NULL)) {
    GST_WARNING_OBJECT (self, "Failed to parse event");
    return TRUE;
  }

  if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
    GST_WARNING_OBJECT (self, "Failed to map payload buffer");
    gst_buffer_unref (buf);
    return TRUE;
  }

  GST_DEBUG_OBJECT (self,
      "Requesting encoder to insert custom SEI %s (payload-type=%d)",
      prefix ? "prefix" : "suffix", payload_type);

  GST_OMX_INIT_STRUCT (&config);
  config.nType = payload_type;
  config.pBuffer = map.data;
  config.nAllocLen = map.size;
  config.nFilledLen = map.size;
  config.nOffset =
      GST_BUFFER_OFFSET_IS_VALID (buf) ? GST_BUFFER_OFFSET (buf) : 0;

  if (prefix)
    index = (OMX_INDEXTYPE) OMX_ALG_IndexConfigVideoInsertPrefixSEI;
  else
    index = (OMX_INDEXTYPE) OMX_ALG_IndexConfigVideoInsertSuffixSEI;

  err = gst_omx_component_set_config (self->enc, index, &config);

  if (err != OMX_ErrorNone)
    GST_ERROR_OBJECT (self,
        "Failed to request SEI insertion: %s (0x%08x)",
        gst_omx_error_to_string (err), err);

  gst_buffer_unmap (buf, &map);
  gst_buffer_unref (buf);
  return TRUE;
}
#endif

static gboolean
gst_omx_video_enc_sink_event (GstVideoEncoder * encoder, GstEvent * event)
{
  GstOMXVideoEnc *self = GST_OMX_VIDEO_ENC (encoder);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      if (gst_event_has_name (event, "buffers-allocated")) {
        const GstStructure *s;

        s = gst_event_get_structure (event);
        if (!gst_structure_get_uint (s, "nb-buffers",
                &self->nb_upstream_buffers)) {
          GST_WARNING_OBJECT (self,
              "buffers-allocated event missing 'nb-buffers' field");
          return TRUE;
        } else {
          GST_DEBUG_OBJECT (self, "Upstream allocated %d buffers",
              self->nb_upstream_buffers);
        }

        gst_event_unref (event);
        return TRUE;
      }
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
      else if (gst_event_has_name (event, OMX_ALG_GST_EVENT_SCENE_CHANGE))
        handle_scene_change_event (self, event);
      else if (gst_event_has_name (event, OMX_ALG_GST_EVENT_INSERT_LONGTERM)
          || gst_event_has_name (event, OMX_ALG_GST_EVENT_USE_LONGTERM))
        return handle_longterm_event (self, event);
      else if (gst_event_has_name (event, OMX_ALG_GST_EVENT_INSERT_PREFIX_SEI)
          || gst_event_has_name (event, OMX_ALG_GST_EVENT_INSERT_SUFFIX_SEI))
        handle_sei_insertion (self, event);
      else if (gst_event_has_name (event, OMX_ALG_GST_EVENT_LOAD_QP))
        handle_load_qp (self, event);
#endif
    }
    default:
      break;
  }

  return
      GST_VIDEO_ENCODER_CLASS (gst_omx_video_enc_parent_class)->sink_event
      (encoder, event);
}

static gboolean
gst_omx_video_enc_decide_allocation (GstVideoEncoder * encoder,
    GstQuery * query)
{
  GstOMXVideoEnc *self = GST_OMX_VIDEO_ENC (encoder);
  guint min = 1;

  if (!GST_VIDEO_ENCODER_CLASS
      (gst_omx_video_enc_parent_class)->decide_allocation (encoder, query))
    return FALSE;

  if (gst_query_get_n_allocation_pools (query)) {
    gst_query_parse_nth_allocation_pool (query, 0, NULL, NULL, &min, NULL);
    GST_DEBUG_OBJECT (self,
        "Downstream requested %d buffers, adjust number of output buffers accordingly",
        min);
  } else {
    GST_DEBUG_OBJECT (self, "Downstream didn't set any allocation pool info");
  }

  self->nb_downstream_buffers = min;

  return TRUE;
}
