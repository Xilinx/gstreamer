/* GStreamer
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * gstv4l2media.c - media controller wrapper
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstv4l2media.h"

#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include "gst/gst-i18n-plugin.h"

#include "ext/media.h"

#ifdef HAVE_GUDEV
#include <gudev/gudev.h>
#endif

GST_DEBUG_CATEGORY_STATIC (v4l2_media);
#define GST_CAT_DEFAULT v4l2_media

static GstV4l2MediaEntity *
media_entity_new (struct media_v2_entity *entity)
{
  GstV4l2MediaEntity *e = g_new0 (GstV4l2MediaEntity, 1);

  e->name = g_strdup (entity->name);
  e->function = entity->function;
  e->flags = entity->flags;

  return e;
}

static void
media_entity_free (GstV4l2MediaEntity * entity)
{
  g_free (entity->name);
  g_list_free (entity->pads);
  g_free (entity);
}

static GstV4l2MediaInterface *
media_interface_new (struct media_v2_interface *iface)
{
  GstV4l2MediaInterface *i = g_new0 (GstV4l2MediaInterface, 1);

  i->type = iface->intf_type;
  i->flags = iface->flags;
  i->major = iface->devnode.major;
  i->minor = iface->devnode.minor;

  return i;
}

static void
media_interface_free (GstV4l2MediaInterface * iface)
{
  g_free (iface);
}

static GstV4l2MediaPad *
media_pad_new (GstV4l2Media * self, struct media_v2_pad *pad)
{
  GstV4l2MediaPad *p = g_new0 (GstV4l2MediaPad, 1);

  p->entity =
      g_hash_table_lookup (self->entities, GUINT_TO_POINTER (pad->entity_id));
  if (p->entity)
    p->entity->pads = g_list_append (p->entity->pads, p);
  else
    GST_WARNING ("Unknown entity %d", pad->entity_id);
  p->flags = pad->flags;
  p->index = pad->index;

  return p;
}

static void
media_pad_free (GstV4l2MediaPad * pad)
{
  g_free (pad);
}

static GstV4l2MediaLink *
media_link_new (GstV4l2Media * self, struct media_v2_link *link)
{
  GstV4l2MediaLink *l = g_new0 (GstV4l2MediaLink, 1);
  guint32 link_type;

  l->flags = link->flags;

  link_type = l->flags & MEDIA_LNK_FL_LINK_TYPE;
  if (link_type == MEDIA_LNK_FL_DATA_LINK) {
    l->link_type = GST_V4L2_MEDIA_LINK_TYPE_DATA;
    l->source =
        g_hash_table_lookup (self->pads, GUINT_TO_POINTER (link->source_id));
    l->sink =
        g_hash_table_lookup (self->pads, GUINT_TO_POINTER (link->sink_id));
  } else if (link_type == MEDIA_LNK_FL_INTERFACE_LINK) {
    l->link_type = GST_V4L2_MEDIA_LINK_TYPE_INTERFACE;
    l->source =
        g_hash_table_lookup (self->interfaces,
        GUINT_TO_POINTER (link->source_id));
    l->sink =
        g_hash_table_lookup (self->entities, GUINT_TO_POINTER (link->sink_id));
  }

  if (!l->source)
    GST_WARNING ("Unknown source %d", link->source_id);
  if (!l->sink)
    GST_WARNING ("Unknown sink %d", link->sink_id);

  return l;
}

static void
media_link_free (GstV4l2MediaLink * link)
{
  g_free (link);
}

GstV4l2Media *
gst_v4l2_media_new (const gchar * path)
{
  GstV4l2Media *m = g_new0 (GstV4l2Media, 1);

  GST_DEBUG_CATEGORY_INIT (v4l2_media, "v4l2_media", 0,
      "Media controller wrapper");

  if (!path)
    path = "/dev/media0";

  m->path = g_strdup (path);

  m->entities =
      g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) media_entity_free);
  m->interfaces =
      g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) media_interface_free);
  m->pads =
      g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify) media_pad_free);
  m->links =
      g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) media_link_free);

#ifdef HAVE_GUDEV
  m->udev = g_udev_client_new (NULL);
#endif

  return m;
}

void
gst_v4l2_media_free (GstV4l2Media * self)
{
  g_return_if_fail (self);

  if (self->fd > 0)
    gst_v4l2_media_close (self);

  g_free (self->path);
  g_hash_table_unref (self->entities);
  g_hash_table_unref (self->interfaces);
  g_hash_table_unref (self->pads);
  g_hash_table_unref (self->links);

#ifdef HAVE_GUDEV
  g_clear_object (&self->udev);
#endif

  g_free (self);
}

gboolean
gst_v4l2_media_open (GstV4l2Media * self)
{
  gint fd;

  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (self->fd == 0, FALSE);

  fd = open (self->path, O_RDONLY);
  if (fd < 0) {
    GST_WARNING ("Failed to open %s: %s", self->path, g_strerror (errno));
    return FALSE;
  }

  self->fd = fd;

  return TRUE;
}

void
gst_v4l2_media_close (GstV4l2Media * self)
{
  g_return_if_fail (self);
  g_return_if_fail (self->fd > 0);

  close (self->fd);
}

gboolean
gst_v4l2_media_refresh_topology (GstV4l2Media * self)
{
  struct media_v2_topology arg;
  struct media_v2_entity *entities = NULL;
  struct media_v2_interface *interfaces = NULL;
  struct media_v2_pad *pads = NULL;
  struct media_v2_link *links = NULL;
  guint i;
  gboolean result = FALSE;

  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (self->fd > 0, FALSE);

  g_hash_table_remove_all (self->entities);
  g_hash_table_remove_all (self->interfaces);
  g_hash_table_remove_all (self->pads);
  g_hash_table_remove_all (self->links);

  memset (&arg, 0, sizeof (arg));

  if (ioctl (self->fd, MEDIA_IOC_G_TOPOLOGY, &arg) < 0) {
    GST_WARNING ("%s failed to fetch topoplogy info: %s", self->path,
        g_strerror (errno));
    return FALSE;
  }

  GST_DEBUG ("%s num_entities=%d num_interfaces=%d num_pads=%d num_links=%d",
      self->path, arg.num_entities, arg.num_interfaces, arg.num_pads,
      arg.num_links);

  entities = g_new0 (struct media_v2_entity, arg.num_entities);
  arg.ptr_entities = (guint64) entities;
  interfaces = g_new0 (struct media_v2_interface, arg.num_interfaces);
  arg.ptr_interfaces = (guint64) interfaces;
  pads = g_new0 (struct media_v2_pad, arg.num_pads);
  arg.ptr_pads = (guint64) pads;
  links = g_new0 (struct media_v2_link, arg.num_links);
  arg.ptr_links = (guint64) links;

  if (ioctl (self->fd, MEDIA_IOC_G_TOPOLOGY, &arg) < 0) {
    GST_WARNING ("%s failed to fetch topoplogy details: %s", self->path,
        g_strerror (errno));
    goto out;
  }

  for (i = 0; i < arg.num_entities; i++) {
    struct media_v2_entity *entity = &entities[i];

    GST_TRACE ("  entity id=%d name=%s function=%d flags=%d", entity->id,
        entity->name, entity->function, entity->flags);

    g_hash_table_insert (self->entities, GUINT_TO_POINTER (entity->id),
        media_entity_new (entity));
  }

  for (i = 0; i < arg.num_interfaces; i++) {
    struct media_v2_interface *iface = &interfaces[i];

    GST_TRACE ("  interface id=%d type=%d flags=%d major=%d minor=%d",
        iface->id, iface->intf_type, iface->flags, iface->devnode.major,
        iface->devnode.minor);

    g_hash_table_insert (self->interfaces, GUINT_TO_POINTER (iface->id),
        media_interface_new (iface));
  }

  for (i = 0; i < arg.num_pads; i++) {
    struct media_v2_pad *pad = &pads[i];

    GST_TRACE ("  pad id=%d entity-id=%d flags=%d index=%d",
        pad->id, pad->entity_id, pad->flags, pad->index);

    g_hash_table_insert (self->pads, GUINT_TO_POINTER (pad->id),
        media_pad_new (self, pad));
  }

  for (i = 0; i < arg.num_links; i++) {
    struct media_v2_link *link = &links[i];
    GstV4l2MediaLink *l;

    GST_TRACE ("  link id=%d source_id=%d sink_id=%d flags=%d", link->id,
        link->source_id, link->sink_id, link->flags);

    l = media_link_new (self, link);

    g_hash_table_insert (self->links, GUINT_TO_POINTER (link->id), l);
  }

  result = TRUE;

out:
  g_free (entities);
  g_free (interfaces);
  g_free (pads);
  g_free (links);
  return result;
}

GList *
gst_v4l2_media_get_entities (GstV4l2Media * self)
{
  return g_hash_table_get_values (self->entities);
}

GList *
gst_v4l2_media_find_interfaces_linked_with_entity (GstV4l2Media * self,
    GstV4l2MediaEntity * entity, guint32 interface_type)
{
  GHashTableIter it;
  gpointer v;
  GList *list = NULL;

  g_hash_table_iter_init (&it, self->links);
  while (g_hash_table_iter_next (&it, NULL, &v)) {
    GstV4l2MediaLink *link = v;
    GstV4l2MediaInterface *iface;

    if (link->link_type != GST_V4L2_MEDIA_LINK_TYPE_INTERFACE)
      continue;

    if (link->sink != entity)
      continue;

    iface = link->source;

    if (interface_type && iface->type != interface_type)
      continue;

    list = g_list_append (list, iface);
  }

  return list;
}

GList *
gst_v4l2_media_find_pads_linked_with_sink (GstV4l2Media * self,
    GstV4l2MediaPad * sink)
{
  GHashTableIter it;
  gpointer v;
  GList *list = NULL;

  g_hash_table_iter_init (&it, self->links);
  while (g_hash_table_iter_next (&it, NULL, &v)) {
    GstV4l2MediaLink *link = v;

    if (link->link_type != GST_V4L2_MEDIA_LINK_TYPE_DATA)
      continue;

    if (link->sink != sink)
      continue;

    list = g_list_append (list, link);
  }

  return list;
}

gchar *
gst_v4l2_media_get_interface_device_file (GstV4l2Media * self,
    GstV4l2MediaInterface * interface)
{
  g_return_val_if_fail (interface, NULL);

#ifdef HAVE_GUDEV
  {
    GUdevDevice *device;
    GUdevDeviceNumber number;
    gchar *file;

    number = makedev (interface->major, interface->minor);

    device =
        g_udev_client_query_by_device_number (self->udev,
        G_UDEV_DEVICE_TYPE_CHAR, number);
    if (!device)
      return NULL;

    file = g_strdup (g_udev_device_get_device_file (device));

    g_object_unref (device);
    return file;
  }
#else
  /* TODO: use /sys/dev/char/ */
#endif
  return NULL;
}

/* Get /dev/mediaX media node from /dev/videoX or /dev/v4l-subdevX */
gchar *
gst_v4l2_media_get_device_file (gchar * video_file)
{
  gchar **src_v = NULL;
  gchar *search_path = NULL;
  gchar *search_path_tmp = NULL;
  const gchar *filename = NULL;
  gchar *media_path = NULL;
  GDir *dir;

  g_return_val_if_fail (video_file, NULL);
  src_v = g_strsplit_set (video_file, "/", 3);
  search_path_tmp = g_strjoin ("/", "/sys/class/video4linux", src_v[2], NULL);
  search_path = g_strjoin ("/", search_path_tmp, "device", NULL);
  dir = g_dir_open (search_path, 0, NULL);
  if (dir) {
    while ((filename = g_dir_read_name (dir))) {
      if (g_str_has_prefix (filename, "media"))
        break;
    }
  }

  media_path = g_strjoin ("/", "/dev", filename, NULL);
  g_dir_close (dir);
  g_strfreev (src_v);
  g_free (search_path_tmp);
  g_free (search_path);

  return media_path;

}
