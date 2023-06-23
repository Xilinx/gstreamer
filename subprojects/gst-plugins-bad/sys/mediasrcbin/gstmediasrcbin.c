/*
 * Copyright (C) 2020 – 2021 Xilinx, Inc.  All rights reserved.
 *
 * Authors:
 *   Naveen Cherukuri <naveen.cherukuri@xilinx.com>
 *   Vishal Sagar <vishal.sagar@xilinx.com>
 *   Ronak Shah <ronak.shah@xilinx.com>
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
 *
 */

#include "gstmediasrcbin.h"
// MEDIA_BUS_FMT_VYYUYY8_1X24 is xilinx internal and is not defined in kernel header
#ifndef MEDIA_BUS_FMT_VYYUYY8_1X24
#define MEDIA_BUS_FMT_VYYUYY8_1X24             0x2100
#endif
// MEDIA_BUS_FMT_VYYUYY10_4X20 is xilinx internal and is not defined in kernel header
#ifndef MEDIA_BUS_FMT_VYYUYY10_4X20
#define MEDIA_BUS_FMT_VYYUYY10_4X20            0x2101
#endif

#define PREFERED_CAPS_WIDTH                    3840
#define PREFERED_CAPS_HEIGHT                   2160
#define PREFERED_CAPS_FPS_NUMERATOR            120
#define PREFERED_CAPS_FPS_DENOMINATOR          1

G_DEFINE_TYPE (GstMediaSrcBinPad, gst_media_src_bin_pad, GST_TYPE_GHOST_PAD);

static void
media_src_bin_pad_install_properties (GstMediaSrcBinPadClass * klass,
    const gchar * element_name)
{
  GstElementFactory *src_factory;
  guint num_props = 0;
  GParamSpec **props = NULL;
  GObjectClass *obj_class = NULL;
  guint new_num_props = 0;
  guint i;

  src_factory = gst_element_factory_find (element_name);
  if (!src_factory) {
    GST_ERROR ("failed to find element factory : %s...\n", element_name);
    return;
  }

  obj_class =
      g_type_class_ref (gst_element_factory_get_element_type (src_factory));
  props = g_object_class_list_properties (obj_class, &num_props);

  for (i = 0; i < num_props; i++) {
    GParamSpec *pspec = props[i];
    GParamSpec *our_pspec;

    if (g_object_class_find_property (G_OBJECT_CLASS (klass),
            g_param_spec_get_name (pspec))) {
      continue;
    }
    new_num_props++;

    GST_LOG ("Child Property : name = %s, nickname = %s and blurb = %s",
        g_param_spec_get_name (pspec), g_param_spec_get_nick (pspec),
        g_param_spec_get_blurb (pspec));

    if (G_IS_PARAM_SPEC_BOOLEAN (pspec)) {      /* Boolean */
      GParamSpecBoolean *src_pspec = G_PARAM_SPEC_BOOLEAN (pspec);
      our_pspec = g_param_spec_boolean (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->default_value, pspec->flags);
    } else if (G_IS_PARAM_SPEC_CHAR (pspec)) {  /* Char */
      GParamSpecChar *src_pspec = G_PARAM_SPEC_CHAR (pspec);
      our_pspec = g_param_spec_char (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->minimum, src_pspec->maximum, src_pspec->default_value,
          pspec->flags);
    } else if (G_IS_PARAM_SPEC_UCHAR (pspec)) { /* Unsigned Char */
      GParamSpecUChar *src_pspec = G_PARAM_SPEC_UCHAR (pspec);
      our_pspec = g_param_spec_char (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->minimum, src_pspec->maximum, src_pspec->default_value,
          pspec->flags);
    } else if (G_IS_PARAM_SPEC_INT (pspec)) {   /* Integer */
      GParamSpecInt *src_pspec = G_PARAM_SPEC_INT (pspec);
      our_pspec = g_param_spec_int (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->minimum, src_pspec->maximum, src_pspec->default_value,
          pspec->flags);
    } else if (G_IS_PARAM_SPEC_UINT (pspec)) {  /* Unsigned Integer */
      GParamSpecUInt *src_pspec = G_PARAM_SPEC_UINT (pspec);
      our_pspec = g_param_spec_uint (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->minimum, src_pspec->maximum, src_pspec->default_value,
          pspec->flags);
    } else if (G_IS_PARAM_SPEC_LONG (pspec)) {  /* Long */
      GParamSpecLong *src_pspec = G_PARAM_SPEC_LONG (pspec);
      our_pspec = g_param_spec_long (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->minimum, src_pspec->maximum, src_pspec->default_value,
          pspec->flags);
    } else if (G_IS_PARAM_SPEC_ULONG (pspec)) { /* Unsigned Long */
      GParamSpecULong *src_pspec = G_PARAM_SPEC_ULONG (pspec);
      our_pspec = g_param_spec_ulong (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->minimum, src_pspec->maximum, src_pspec->default_value,
          pspec->flags);
    } else if (G_IS_PARAM_SPEC_INT64 (pspec)) { /* Integer64 */
      GParamSpecInt64 *src_pspec = G_PARAM_SPEC_INT64 (pspec);
      our_pspec = g_param_spec_int64 (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->minimum, src_pspec->maximum, src_pspec->default_value,
          pspec->flags);
    } else if (G_IS_PARAM_SPEC_UINT64 (pspec)) {        /* Unsigned Integer64 */
      GParamSpecUInt64 *src_pspec = G_PARAM_SPEC_UINT64 (pspec);
      our_pspec = g_param_spec_uint64 (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->minimum, src_pspec->maximum, src_pspec->default_value,
          pspec->flags);
    } else if (G_IS_PARAM_SPEC_FLOAT (pspec)) { /* Float */
      GParamSpecFloat *src_pspec = G_PARAM_SPEC_FLOAT (pspec);
      our_pspec = g_param_spec_float (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->minimum, src_pspec->maximum, src_pspec->default_value,
          pspec->flags);
    } else if (G_IS_PARAM_SPEC_DOUBLE (pspec)) {        /* Double */
      GParamSpecDouble *src_pspec = G_PARAM_SPEC_DOUBLE (pspec);
      our_pspec = g_param_spec_double (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->minimum, src_pspec->maximum, src_pspec->default_value,
          pspec->flags);
    } else if (G_IS_PARAM_SPEC_ENUM (pspec)) {  /* Enum */
      GParamSpecEnum *src_pspec = G_PARAM_SPEC_ENUM (pspec);
      our_pspec = g_param_spec_enum (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          pspec->value_type, src_pspec->default_value, pspec->flags);
    } else if (G_IS_PARAM_SPEC_FLAGS (pspec)) { /* Flags */
      GParamSpecFlags *src_pspec = G_PARAM_SPEC_FLAGS (pspec);
      our_pspec = g_param_spec_flags (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          pspec->value_type, src_pspec->default_value, pspec->flags);
    } else if (G_IS_PARAM_SPEC_STRING (pspec)) {        /* String */
      GParamSpecString *src_pspec = G_PARAM_SPEC_STRING (pspec);
      our_pspec = g_param_spec_string (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->default_value, pspec->flags);
    } else if (G_IS_PARAM_SPEC_PARAM (pspec)) { /* Param */
      our_pspec = g_param_spec_param (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          pspec->value_type, pspec->flags);
    } else if (G_IS_PARAM_SPEC_BOXED (pspec)) { /*Boxed */
      our_pspec = g_param_spec_boxed (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          pspec->value_type, pspec->flags);
    } else if (G_IS_PARAM_SPEC_POINTER (pspec)) {       /*Pointer */
      our_pspec = g_param_spec_pointer (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          pspec->flags);
    } else if (G_IS_PARAM_SPEC_OBJECT (pspec)) {        /* Object */
      our_pspec = g_param_spec_object (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          pspec->value_type, pspec->flags);
    } else if (G_IS_PARAM_SPEC_UNICHAR (pspec)) {       /* UniChar */
      GParamSpecUnichar *src_pspec = G_PARAM_SPEC_UNICHAR (pspec);
      our_pspec = g_param_spec_unichar (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->default_value, pspec->flags);
    } else if (G_IS_PARAM_SPEC_VALUE_ARRAY (pspec)) {   /* ValurArray */
      GParamSpecValueArray *src_pspec = G_PARAM_SPEC_VALUE_ARRAY (pspec);
      our_pspec = g_param_spec_value_array (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->element_spec, pspec->flags);
    } else if (G_IS_PARAM_SPEC_GTYPE (pspec)) { /* GType */
      GParamSpecGType *src_pspec = G_PARAM_SPEC_GTYPE (pspec);
      our_pspec = g_param_spec_gtype (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->is_a_type, pspec->flags);
    } else if (G_IS_PARAM_SPEC_VARIANT (pspec)) {       /* Variant */
      GParamSpecVariant *src_pspec = G_PARAM_SPEC_VARIANT (pspec);
      our_pspec = g_param_spec_variant (g_param_spec_get_name (pspec),
          g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec),
          src_pspec->type, src_pspec->default_value, pspec->flags);
    } else {
      g_critical ("Unsupported property type %s for property %s",
          G_PARAM_SPEC_TYPE_NAME (pspec), g_param_spec_get_name (pspec));
      continue;
    }
    g_object_class_install_property (G_OBJECT_CLASS (klass), new_num_props,
        our_pspec);
  }
  g_free (props);
  return;
}

static void
gst_media_src_bin_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMediaSrcBinPad *self = GST_MEDIA_SRC_BIN_PAD (object);

  if (self->src) {
    GST_LOG_OBJECT (self, "going to get property %s", pspec->name);
    g_object_get_property (G_OBJECT (self->src), pspec->name, value);
  }
}

static void
gst_media_src_bin_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMediaSrcBinPad *self = GST_MEDIA_SRC_BIN_PAD (object);

  if (self->src) {
    GST_LOG_OBJECT (self, "going to set property %s", pspec->name);
    g_object_set_property (G_OBJECT (self->src), pspec->name, value);
  }
}

static void
gst_media_src_bin_pad_class_init (GstMediaSrcBinPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_media_src_bin_pad_set_property;
  gobject_class->get_property = gst_media_src_bin_pad_get_property;
  media_src_bin_pad_install_properties (klass, "v4l2src");
}

static void
gst_media_src_bin_pad_init (GstMediaSrcBinPad * binpad)
{
  binpad->src = NULL;
}

GST_DEBUG_CATEGORY (gst_media_src_bin_debug);
#define GST_CAT_DEFAULT gst_media_src_bin_debug

static GstStaticPadTemplate mediasrcbin_srcpad_template =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

#define gst_media_src_bin_parent_class parent_class
G_DEFINE_TYPE (GstMediaSrcBin, gst_media_src_bin, GST_TYPE_BIN);

#define DEFAULT_MEDIA_DEVICE "/dev/media0"

enum
{
  PROP_0,
  PROP_MEDIA_DEVICE,
};

static guint
get_media_bus_format (GstVideoFormat gst_fmt)
{
  switch (gst_fmt) {
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_NV16:
      return MEDIA_BUS_FMT_UYVY8_1X16;
    case GST_VIDEO_FORMAT_NV12:
      return MEDIA_BUS_FMT_VYYUYY8_1X24;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      return MEDIA_BUS_FMT_VYYUYY10_4X20;
    case GST_VIDEO_FORMAT_NV16_10LE32:
      return MEDIA_BUS_FMT_UYVY10_1X20;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      return MEDIA_BUS_FMT_RBG888_1X24;
    case GST_VIDEO_FORMAT_GRAY8:
      return MEDIA_BUS_FMT_Y8_1X8;
    default:
      GST_ERROR ("Gst Fourcc %d not handled", gst_fmt);
      return 0;
  }
}

static void
gst_media_src_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMediaSrcBin *bin = GST_MEDIA_SRC_BIN (object);

  switch (prop_id) {
    case PROP_MEDIA_DEVICE:
      g_free (bin->mediadev);
      bin->mediadev = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_media_src_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMediaSrcBin *bin = GST_MEDIA_SRC_BIN (object);

  switch (prop_id) {
    case PROP_MEDIA_DEVICE:
      g_value_set_string (value, bin->mediadev);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_media_src_bin_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstMediaSrcBin *bin = GST_MEDIA_SRC_BIN (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:{
      GstCaps *caps, *new_caps;
      gst_query_parse_caps (query, &caps);
      GST_INFO_OBJECT (bin, "old caps %p are %" GST_PTR_FORMAT, caps, caps);
      /* temp fix, setting any caps for negotiation purpose */
      /* TODO: medialib layer provides formats supported by
       * FBwrite driver and those will be sent in this query
       */
      new_caps = gst_caps_new_any ();
      GST_INFO_OBJECT (bin, "new caps %p are %" GST_PTR_FORMAT, new_caps,
          new_caps);
      gst_query_set_caps_result (query, new_caps);
      gst_caps_unref (new_caps);
      return TRUE;
    }
    default:
      return gst_pad_query_default (pad, parent, query);
  }
}

static gboolean
media_src_bin_create_child (GstMediaSrcBin * bin, gchar * videodev)
{
  GstElement *child = NULL;
  GstPadTemplate *templ;
  GstPad *target_pad = NULL;
  gchar *pad_name =
      g_strdup_printf ("src_%u", g_list_length (bin->vnodes_info));
  gchar *v4l2src_obj_name =
      g_strdup_printf ("v4l2src%u", g_list_length (bin->vnodes_info));
  gboolean bret = TRUE;
  GstVideoNodeInfo *vnode_info = NULL;
  GstMediaSrcBinPad *ghost_pad = NULL;

  child = gst_element_factory_make ("v4l2src", NULL);
  g_object_set (G_OBJECT (child), "device", videodev, NULL);
  g_object_set (G_OBJECT (child), "name", v4l2src_obj_name, NULL);

  GST_INFO_OBJECT (bin, "created v4l2src with device node %s", videodev);

  bret = gst_bin_add (GST_BIN (bin), child);
  if (!bret) {
    GST_ERROR_OBJECT (bin, "failed to add element %s to bin",
        GST_ELEMENT_NAME (GST_ELEMENT (child)));
    gst_object_unref (child);
    goto error;
  }

  templ =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS
      (GST_MEDIA_SRC_BIN_GET_CLASS (bin)), "src_%u");
  ghost_pad =
      g_object_new (GST_TYPE_MEDIA_SRC_BIN_PAD, "name", pad_name, "direction",
      GST_PAD_SRC, "template", templ, NULL);
  ghost_pad->src = child;

  target_pad = gst_element_get_static_pad (child, "src");

  bret = gst_ghost_pad_construct (GST_GHOST_PAD (ghost_pad));
  if (!bret) {
    GST_ERROR_OBJECT (bin, "ghost pad construct failed");
    goto error;
  }

  bret = gst_ghost_pad_set_target (GST_GHOST_PAD (ghost_pad), target_pad);
  if (!bret) {
    GST_ERROR_OBJECT (bin, "failed to set target");
    goto error;
  }
  gst_pad_set_query_function (GST_PAD (ghost_pad), gst_media_src_bin_query);

  vnode_info = g_slice_alloc0 (sizeof (GstVideoNodeInfo));
  vnode_info->vdevice = g_strdup (videodev);
  vnode_info->ghostpad = ghost_pad;
  bin->vnodes_info = g_list_append (bin->vnodes_info, vnode_info);

  bret = gst_element_add_pad (GST_ELEMENT (bin), GST_PAD (ghost_pad));
  if (!bret) {
    GST_ERROR_OBJECT (bin, "failed to add pad %s to bin",
        GST_PAD_NAME (GST_PAD (ghost_pad)));
    goto error;
  }

exit:
  g_free (pad_name);
  g_free (v4l2src_obj_name);
  return bret;

error:
  if (target_pad)
    gst_object_unref (target_pad);

  if (ghost_pad)
    gst_object_unref (ghost_pad);
  goto exit;
}

static void
media_src_bin_free_video_node_info (GstVideoNodeInfo * vnode_info,
    gpointer user_data)
{
  g_free (vnode_info->vdevice);
  g_slice_free1 (sizeof (GstVideoNodeInfo), vnode_info);
}

static gboolean
media_src_bin_configure_media_pipe (GstMediaSrcBin * bin, gchar * device,
    GstCaps * caps)
{
  struct xml_vdev_fmt_info fmt = { 0, };
  gint iret = 0;
  GstVideoInfo vinfo;

  /* This check really should return an error as vinfo will be invalid
     if the caps are not fixed. The current implementation assumes a caps
     filter is used that fixes the caps fr any pipeline that uses the xvipp
     driver. For vivid and uvc, xml_set_connected_src_pad() does not attempt
     to set the format on the subdev connected to the video node and thus
     vinfo is ignored.
   */
  if (!gst_video_info_from_caps (&vinfo, caps)) {
    GST_INFO_OBJECT (bin, "failed to parse caps for %s", device);
  }

  fmt.width = GST_VIDEO_INFO_WIDTH (&vinfo);
  fmt.height = GST_VIDEO_INFO_HEIGHT (&vinfo);
  fmt.frm_num = GST_VIDEO_INFO_FPS_N (&vinfo);;
  fmt.frm_den = GST_VIDEO_INFO_FPS_D (&vinfo);
  fmt.code = get_media_bus_format (GST_VIDEO_INFO_FORMAT (&vinfo));

  GST_INFO_OBJECT (bin, "configuring media-srcpad of %s with"
      "width = %d, height = %d, framerate = %d/%d, media-fourcc = %d",
      device, fmt.width, fmt.height, fmt.frm_num, fmt.frm_den, fmt.code);

  iret = xml_set_connected_src_pad (device, &fmt);
  if (iret < 0) {
    GST_ERROR_OBJECT (bin, "failed to set format on %s", device);
    return FALSE;
  }
  return TRUE;
}

static GstCaps *
gst_media_src_bin_caps_remove_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure (caps, i);
    f = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (res, st, f))
      continue;

    st = gst_structure_copy (st);

    /* Remove colorimetry, chroma-site, interlace-mode and pixel-aspect-ratio
     * info from caps as only width, height, framerate and format are useful
     */
    gst_structure_remove_fields (st, "colorimetry", "chroma-site",
        "interlace-mode", "pixel-aspect-ratio", NULL);

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }

  return res;
}

static void
gst_media_src_bin_fixate_caps (GstMediaSrcBin * bin, GstCaps * caps)
{
  GstStructure *s = NULL;
  gint i, n;

  n = gst_caps_get_size (caps);
  GST_DEBUG_OBJECT (bin, "caps size is : %d", n);

  for (i = 0; i < n; i++) {
    s = gst_caps_get_structure (caps, i);

    if (gst_structure_has_field (s, "width"))
      gst_structure_fixate_field_nearest_int (s, "width", PREFERED_CAPS_WIDTH);

    if (gst_structure_has_field (s, "height"))
      gst_structure_fixate_field_nearest_int (s, "height",
          PREFERED_CAPS_HEIGHT);

    if (gst_structure_has_field (s, "framerate"))
      gst_structure_fixate_field_nearest_fraction (s, "framerate",
          PREFERED_CAPS_FPS_NUMERATOR, PREFERED_CAPS_FPS_DENOMINATOR);
  }
}

static GstStateChangeReturn
gst_media_src_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstMediaSrcBin *bin = GST_MEDIA_SRC_BIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      gint nvideodev = 0;
      guint idx = 0;

      /* initialize media device */
      ret = xml_init_media_dev (bin->mediadev);
      if (ret < 0) {
        GST_ERROR_OBJECT (bin, "%s media dev init failed = %d", bin->mediadev,
            ret);
        return GST_STATE_CHANGE_FAILURE;
      }
      GST_DEBUG_OBJECT (bin, "media device initialization is successful");

      nvideodev = xml_get_video_dev_count (bin->mediadev);
      if (nvideodev <= 0) {
        GST_ERROR_OBJECT (bin, "failed to get video devices count");
        return GST_STATE_CHANGE_FAILURE;
      }

      GST_DEBUG_OBJECT (bin, "number of video nodes in media node %s : %d",
          bin->mediadev, nvideodev);

      for (idx = 0; idx < nvideodev; idx++) {
        gchar video_dev_name[32] = { 0 };
        gint iret = 0;

        iret = xml_get_video_dev_name (bin->mediadev, idx, video_dev_name);
        if (iret < 0) {
          GST_ERROR_OBJECT (bin, "failed to get video dev name in %s",
              bin->mediadev);
          return GST_STATE_CHANGE_FAILURE;
        }
        GST_DEBUG_OBJECT (bin, "video device name at index %d : %s", idx,
            video_dev_name);

        if (!media_src_bin_create_child (bin, video_dev_name)) {
          GST_ERROR_OBJECT (bin, "failed to create child element");
          return GST_STATE_CHANGE_FAILURE;
        }
      }
      gst_element_no_more_pads (GST_ELEMENT (bin));
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
      GList *node;
      for (node = bin->vnodes_info; node != NULL; node = node->next) {
        GstVideoNodeInfo *vnode_info = node->data;
        GstCaps *caps;

        if (!gst_pad_is_linked (GST_PAD (vnode_info->ghostpad))) {
          GST_ERROR_OBJECT (bin, "'%s' is not linked",
              GST_PAD_NAME (vnode_info->ghostpad));
          return GST_STATE_CHANGE_FAILURE;
        }
        caps = gst_pad_peer_query_caps (GST_PAD (vnode_info->ghostpad), NULL);
        if (!caps) {
          GST_ERROR_OBJECT (bin, "peer caps is NULL");
          return GST_STATE_CHANGE_FAILURE;
        }

        GST_INFO_OBJECT (bin, "%s peer caps are %" GST_PTR_FORMAT,
            GST_PAD_NAME (vnode_info->ghostpad), caps);

        if (caps && !gst_caps_is_any (caps))
          gst_media_src_bin_fixate_caps (bin, caps);

        GST_INFO_OBJECT (bin, "%s fixated peer caps are %" GST_PTR_FORMAT,
            GST_PAD_NAME (vnode_info->ghostpad), caps);

        /* Remove colorimetry, chroma-site, interlace-mode and
         * pixel-aspect-ratio info from caps as only width, height,
         * framerate and format are useful
         */
        if (!gst_caps_is_fixed (caps))
          caps = gst_media_src_bin_caps_remove_info (caps);

        GST_INFO_OBJECT (bin, "%s updated peer caps are %" GST_PTR_FORMAT,
            GST_PAD_NAME (vnode_info->ghostpad), caps);

        if (!media_src_bin_configure_media_pipe (bin, vnode_info->vdevice,
                caps)) {
          GST_ERROR_OBJECT (bin, "%s failed to configure = %d", bin->mediadev,
              ret);
          gst_caps_unref (caps);
          return GST_STATE_CHANGE_FAILURE;
        }
        gst_caps_unref (caps);
      }
    }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      g_list_foreach (bin->vnodes_info,
          (GFunc) media_src_bin_free_video_node_info, NULL);
      g_list_free (bin->vnodes_info);
      bin->vnodes_info = NULL;
      break;
    default:
      break;
  }
  return ret;
}

static void
gst_media_src_bin_finalize (GObject * object)
{
  GstMediaSrcBin *bin = GST_MEDIA_SRC_BIN (object);

  g_free (bin->mediadev);
  bin->mediadev = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
gst_media_src_bin_class_init (GstMediaSrcBinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_media_src_bin_set_property;
  gobject_class->get_property = gst_media_src_bin_get_property;
  gobject_class->finalize = gst_media_src_bin_finalize;

  gstelement_class->change_state = gst_media_src_bin_change_state;

  g_object_class_install_property (gobject_class, PROP_MEDIA_DEVICE,
      g_param_spec_string ("media-device", "Media device node",
          "Media Device location", DEFAULT_MEDIA_DEVICE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class,
      &mediasrcbin_srcpad_template);

  gst_element_class_set_static_metadata (gstelement_class,
      "mediasrcbin",
      "Source/Video",
      "Reads frames from media device e.g. /dev/media0",
      "Naveen Cherukuri <naveen.cherukuri@xilinx.com>, "
      "Vishal Sagar <vishal.sagar@xilinx.com>, "
      "Ronak Shah <ronak.shah@xilinx.com>");

  GST_DEBUG_CATEGORY_INIT (gst_media_src_bin_debug, "mediasrcbin",
      0, "Media Source Bin");
}

static void
gst_media_src_bin_init (GstMediaSrcBin * bin)
{
  bin->mediadev = g_strdup (DEFAULT_MEDIA_DEVICE);
  bin->vnodes_info = NULL;
}

static gboolean
media_src_bin_init (GstPlugin * mediasrcbin)
{
  return gst_element_register (mediasrcbin, "mediasrcbin", GST_RANK_NONE,
      GST_TYPE_MEDIA_SRC_BIN);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "mediasrcbin"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mediasrcbin,
    "Media Source Bin",
    media_src_bin_init, "0.1", "LGPL", "GStreamer SDX", "http://xilinx.com/")
