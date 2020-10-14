/*
 * Copyright (C) 2020 Xilinx Inc.
 *     Author: Dylan Yip <dylan.yip@xilinx.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ext/hdr-ctrls.h"
#include "edid-utils.h"

#define EDID_CTA_EXTENSION_TAG    0x02
#define EDID_DB_OFFSET            4
#define EDID_EXTENDED_TAG_CODE    0x06
#define EDID_MAX_LENGTH           128

static gboolean
contains_hdr_eotf (guchar * edid, gint num_db, guint eotf)
{
  gint i = 0;

  /* Parse EDID as per CTA 861.G 7.5.13 */
  for (i = 0; i < num_db; i++) {
    guchar *db = &edid[i * EDID_MAX_LENGTH];
    gint j, end;

    /* Look for extension blocks */
    if (db[0] != EDID_CTA_EXTENSION_TAG)
      continue;
    end = db[2];
    /* Look for HDR static metadata datablock and check supported EOTFs */
    for (j = EDID_DB_OFFSET; j < end && j + (db[j] & 0x1f) < end;
        j += (db[j] & 0x1f) + 1) {
      if (db[j + 1] != EDID_EXTENDED_TAG_CODE)
        continue;
      if (db[j + 2] & (1 << eotf))
        return TRUE;
    }
  }
  return FALSE;
}

gboolean
gst_edid_is_hdr10_supported (GstV4l2Object * v4l2object)
{
  struct v4l2_edid edid = { 0 };
  gboolean ret = FALSE;

  /* Get number of EDID blocks */
  if (v4l2object->ioctl (v4l2object->video_fd, VIDIOC_G_EDID, &edid)) {
    GST_WARNING_OBJECT (v4l2object, "VIDIOC_G_EDID failed: %s",
        g_strerror (errno));
    return ret;
  }

  /* Get actual EDID */
  edid.edid = g_malloc (EDID_MAX_LENGTH * edid.blocks);
  if (!edid.edid) {
    GST_WARNING_OBJECT (v4l2object, "g_malloc failed: %s", g_strerror (errno));
    return ret;
  }

  if (v4l2object->ioctl (v4l2object->video_fd, VIDIOC_G_EDID, &edid)) {
    GST_WARNING_OBJECT (v4l2object, "VIDIOC_G_EDID failed: %s",
        g_strerror (errno));
    g_free (edid.edid);
    return ret;
  }

  /* Check EDID for HDR10 EOTF support */
  ret = contains_hdr_eotf (edid.edid, edid.blocks, V4L2_EOTF_SMPTE_ST2084);
  g_free (edid.edid);
  return ret;
}
