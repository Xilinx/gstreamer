/* GStreamer
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * gstv4l2media.h - media controller wrapper
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

#ifndef __GST_V4L2_MEDIA_H__
#define __GST_V4L2_MEDIA_H__

#include <gst/gst.h>

#ifdef HAVE_GUDEV
#include <gudev/gudev.h>
#endif

typedef struct {
  gchar *path;

  gint fd;

  GHashTable *entities;
  GHashTable *interfaces;
  GHashTable *pads;
  GHashTable *links;

#ifdef HAVE_GUDEV
  GUdevClient *udev;
#endif
} GstV4l2Media;

typedef struct {
  gchar *name;
  guint32 function;
  guint32 flags;

  GList *pads;
} GstV4l2MediaEntity;

typedef struct {
  guint32 type;
  guint32 flags;
  guint32 major;
  guint32 minor;
} GstV4l2MediaInterface;

typedef struct {
  GstV4l2MediaEntity *entity;
  guint32 flags;
  guint32 index;
} GstV4l2MediaPad;

typedef enum
{
  GST_V4L2_MEDIA_LINK_TYPE_UNKNOWN = 0,
  /* source and sink are both GstV4l2MediaPad */
  GST_V4L2_MEDIA_LINK_TYPE_DATA,
  /* source is a GstV4l2MediaInterface
   * sink is a GstV4l2MediaEntity */
  GST_V4L2_MEDIA_LINK_TYPE_INTERFACE,
} GstV4l2MediaLinkType;

typedef struct {
  GstV4l2MediaLinkType link_type;
  gpointer source;
  gpointer sink;
  guint32 flags;
} GstV4l2MediaLink;


GstV4l2Media * gst_v4l2_media_new (const gchar * path);
void gst_v4l2_media_free (GstV4l2Media * self);

gboolean gst_v4l2_media_open (GstV4l2Media * self);
void gst_v4l2_media_close (GstV4l2Media * self);

gboolean gst_v4l2_media_refresh_topology (GstV4l2Media * self);

GList * gst_v4l2_media_get_entities (GstV4l2Media * self);

GList * gst_v4l2_media_find_interfaces_linked_with_entity (GstV4l2Media * self, GstV4l2MediaEntity * entity, guint32 interface_type);
GList * gst_v4l2_media_find_pads_linked_with_sink (GstV4l2Media * self, GstV4l2MediaPad * sink);

gchar * gst_v4l2_media_get_interface_device_file (GstV4l2Media *self, GstV4l2MediaInterface * interface);

gchar * gst_v4l2_media_get_device_file (gchar *video_file);
#endif /* __GST_V4L2_MEDIA_H__ */
