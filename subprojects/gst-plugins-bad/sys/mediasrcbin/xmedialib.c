/*
 * Copyright (C) 2020 – 2021 Xilinx, Inc.  All rights reserved.
 *
 * Authors:
 *   Naveen Cherukuri <naveen.cherukuri@xilinx.com>
 *   Vishal Sagar <vishal.sagar@xilinx.com>
 *   Ronak Shah <ronak.shah@xilinx.com>
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * XILINX BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the Xilinx shall not be used
 * in advertising or otherwise to promote the sale, use or other dealings in
 * this Software without prior written authorization from Xilinx.
 */

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>


#include <linux/media.h>
#include <linux/types.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>

#include <glib.h>
#include <glib/gstring.h>
#include <gmodule.h>
#include <gst/gst.h>

#include "xmedialib.h"

GST_DEBUG_CATEGORY_EXTERN (gst_media_src_bin_debug);
#define GST_CAT_DEFAULT gst_media_src_bin_debug

/*
 * Controls are required to be set to do IMX274 MIPI camera tuning
 * for green shade issue.
 *
 * This is workaround in plugin as CSC driver overwrites the controls
 * to default value on set format function call. It should be removed
 * once issue is fixed into the CSC driver.
 *
 */
static const struct
{
  unsigned int id;
  int64_t val;
} default_ctrls[] = {
  {
  0x0098c981, 4},               /* MIPI Active Lanes 4 */
  {
  0x0098c9a1, 80},              /* CSC Brightness set */
  {
  0x0098c9a2, 55},              /* CSC Contrast set */
  {
  0x0098c9a3, 40},              /* CSC Red gain set */
  {
  0x0098c9a4, 24},              /* CSC Green gain set */
  {
  0x0098c9a5, 35},              /* CSC Blue gain set */
};

static void
PrintEntityInfo (gpointer data, gpointer user_data)
{
  struct media_entity *entity = (struct media_entity *) data;
  GST_DEBUG ("%s \n", media_entity_get_devname (entity));
}


static int
query_control (int fd, unsigned int id, struct v4l2_queryctrl *query)
{
  int ret;

  memset (query, 0, sizeof (*query));
  query->id = id;

  ret = ioctl (fd, VIDIOC_QUERYCTRL, query);

  if (ret < 0 && errno != EINVAL)
    GST_DEBUG ("unable to query control 0x%8.8x: %s (%d).\n",
        id, strerror (errno), errno);
  return ret;
}

static int
set_control (int fd, unsigned int id, int64_t val)
{
  struct v4l2_ext_controls ctrls;
  struct v4l2_ext_control ctrl;
  struct v4l2_queryctrl query;
  int is_64, ret;
  int64_t old_val = val;

  ret = query_control (fd, id, &query);
  if (ret < 0)
    return ret;

  is_64 = query.type == V4L2_CTRL_TYPE_INTEGER64;

  memset (&ctrls, 0, sizeof (ctrls));
  memset (&ctrl, 0, sizeof (ctrl));

  ctrls.ctrl_class = V4L2_CTRL_ID2CLASS (id);
  ctrls.count = 1;
  ctrls.controls = &ctrl;

  ctrl.id = id;
  if (is_64)
    ctrl.value64 = val;
  else
    ctrl.value = val;

  ret = ioctl (fd, VIDIOC_S_EXT_CTRLS, &ctrls);
  if (ret != -1) {
    if (is_64)
      val = ctrl.value64;
    else
      val = ctrl.value;
  } else if (!is_64 && query.type != V4L2_CTRL_TYPE_STRING &&
      (errno == EINVAL || errno == ENOTTY)) {
    struct v4l2_control old;

    old.id = id;
    old.value = val;
    ret = ioctl (fd, VIDIOC_S_CTRL, &old);
    if (ret != -1)
      val = old.value;
  }
  if (ret == -1) {
    GST_DEBUG ("unable to set control 0x%8.8x: %s (%d).\n",
        id, strerror (errno), errno);
    return ret;
  }
  GST_DEBUG ("Control 0x%08x set to %" PRId64 ", is %" PRId64 "\n",
      id, old_val, val);
  return ret;
}

static int
set_default_controls (struct media_entity *entity)
{
  int ret, i;

  /* Set the default ctrls */
  ret = v4l2_subdev_open (entity);
  if (ret < 0) {
    GST_DEBUG ("%s - Unable to open %s\n", __func__, entity->dev_name);
    return ret;
  }

  for (i = 0; i < ARRAY_SIZE (default_ctrls); ++i) {
    ret = set_control (entity->fd, default_ctrls[i].id, default_ctrls[i].val);
    if (ret < 0)
      continue;
    else {
      GST_DEBUG ("set the control %u\n", default_ctrls[i].id);
    }
  }
  v4l2_subdev_close (entity);

  ret = 0;
  return ret;
}

/*
 * This function initialize the pipeline by traversing the media graph
 * from the source entities to sink entities and try to set source
 * entities format whereever possible into the pipeline
 *
 * As an e.g. consider below pipeline
 * source element -> element1 -> element2 -> sink element
 *
 *   sink element  :   sink pad <------
 *                                    |
 *                                    |
 *        element2 : source pad -------
 *                              |
 *                     sink pad <------
 *                                    |
 *                                    |
 *        element1 : source pad -------
 *                              |
 *                     sink pad <------
 *                                    |
 *                                    |
 * source element  : source pad -------
 *
 * - Set source element source pad format on element1 sink pad
 * - Set element1 sink pad format on element1 source pad
 * - Set element1 source pad format on element2 sink pad
 * - Set element2 sink pad format on element2 source pad
 * - Set element2 source pad format on sink element sink pad
 *
 */
static void
xml_srcent_init (gpointer data, gpointer user_data)
{
  struct media_entity *root = (struct media_entity *) data;
  struct media_entity *entity = root;
  struct xml_vdev_fmt_info *vfmt = (struct xml_vdev_fmt_info *) user_data;
  unsigned int def_width = vfmt->width;
  unsigned int def_height = vfmt->height;
  GList *list = NULL;
  int j, ret;
  int entity_index, entity_list_length;

  list = g_list_append (list, entity);
  entity_index = 0;
  entity_list_length = g_list_length (list);

  /* Create a list of entities from source entity till sink entity */
  while (entity_index < entity_list_length) {

    entity = g_list_nth_data (list, entity_index);

    /* if entity is v4l-subdev */
    if (media_entity_type (entity) & MEDIA_ENT_T_V4L2_SUBDEV) {
      const struct media_entity_desc *info;
      int pad_count;
      GST_DEBUG ("%s - entity - %s = %s\n", __func__,
          media_entity_get_devname (entity), entity->info.name);
      info = media_entity_get_info (entity);
      pad_count = info->pads;

      for (j = 0; j < pad_count; j++) {
        const struct media_pad *pad = media_entity_get_pad (entity, j);

        if (pad->flags & MEDIA_PAD_FL_SOURCE) {

          struct v4l2_mbus_framefmt format, remote_format;
          struct v4l2_fract interval = { 0, 0 };
          int i;

          enum v4l2_subdev_format_whence which = V4L2_SUBDEV_FORMAT_ACTIVE;

          ret = v4l2_subdev_get_format (entity, &format, j, which);
          if (ret != 0) {
            GST_DEBUG (" %s : failed to get format\n", __func__);
            return;
          }

          ret = v4l2_subdev_get_frame_interval (entity, &interval, j);
          if (ret != 0 && ret != -ENOTTY && ret != -EINVAL) {
            GST_DEBUG ("%s - Failed to get frame interval for %s!\n",
                __func__, media_entity_get_devname (entity));
          }

          GST_DEBUG ("\t get format [pad:%d fmt:%s/%ux%u@%u/%u\n", j,
              v4l2_subdev_pixelcode_to_string (format.code),
              format.width, format.height,
              interval.numerator, interval.denominator);

          format.width = def_width;
          format.height = def_height;

          interval.numerator = 1;
          interval.denominator = 60;

          /* Set sink pad format on same entity source pad if possible */
          if (pad_count > 1) {
            int k;

            for (k = 0; k < pad_count; k++) {
              const struct media_pad *sink_pad =
                  media_entity_get_pad (entity, k);

              if (sink_pad->flags & MEDIA_PAD_FL_SINK) {
                struct v4l2_mbus_framefmt sink_format;
                struct v4l2_fract sink_interval = { 0, 0 };

                ret = v4l2_subdev_get_format (entity, &sink_format, k, which);
                if (ret != 0) {
                  GST_DEBUG (" %s : failed to get format\n", __func__);
                  return;
                }

                ret =
                    v4l2_subdev_get_frame_interval (entity, &sink_interval, k);
                if (ret != 0 && ret != -ENOTTY && ret != -EINVAL) {
                  GST_DEBUG ("%s - Failed to get frame interval for %s!\n",
                      __func__, media_entity_get_devname (entity));
                }

                format.width = sink_format.width;
                format.height = sink_format.height;
                format.code = sink_format.code;

                interval.numerator = sink_interval.numerator;
                interval.denominator = sink_interval.denominator;

                break;
              }
            }
          }

          ret = v4l2_subdev_set_format (entity, &format, j, which);
          if (ret != 0) {
            GST_DEBUG (" %s : failed to set format on src pad\n", __func__);
          }

          ret = v4l2_subdev_set_frame_interval (entity, &interval, j);
          if (ret < 0) {
            GST_DEBUG ("ret = %d src set frame interval was %u/%u\n", ret,
                interval.numerator, interval.denominator);
          }

          GST_DEBUG ("\t set src format [pad:%d fmt:%s/%ux%u@%u/%u\n", j,
              v4l2_subdev_pixelcode_to_string (format.code),
              format.width, format.height,
              interval.numerator, interval.denominator);

          ret = set_default_controls (entity);
          if (ret < 0) {
            GST_DEBUG ("\t Default control set failed\n");
            return;
          }

          /* get remote pad */
          for (i = 0; i < pad->entity->num_links; ++i) {
            struct media_link *link = &pad->entity->links[i];

            if (!(link->flags & MEDIA_LNK_FL_ENABLED))
              continue;

            if (link->source == pad &&
                link->sink->entity->info.type == MEDIA_ENT_T_V4L2_SUBDEV) {
              remote_format = format;

              GST_DEBUG ("remote entity = %s\n", link->sink->entity->info.name);

              ret =
                  v4l2_subdev_set_format (link->sink->entity, &remote_format,
                  link->sink->index, which);
              if (ret != 0) {
                GST_DEBUG (" %s : failed to set format on sink pad\n",
                    __func__);
              }

              ret =
                  v4l2_subdev_set_frame_interval (link->sink->entity,
                  &interval, link->sink->index);

              GST_DEBUG ("\t set sink format [pad:%d fmt:%s/%ux%u@%u/%u\n",
                  link->sink->index,
                  v4l2_subdev_pixelcode_to_string (format.code), format.width,
                  format.height, interval.numerator, interval.denominator);

              /*
               * Add the remote entity to the list, if not added already, so that
               * in upcoming iterations its source pads are set
               */
              if (!g_list_find (list, link->sink->entity)) {
                list = g_list_append (list, link->sink->entity);
                GST_DEBUG ("Adding %s to entity list\n",
                    link->sink->entity->dev_name);
              }
            }
          }

          /* This is done in case the source pad and sink format pad don't match */
          if (format.code != remote_format.code) {
            GST_DEBUG
                ("sink pad format %s doesn't match source pad format %s!\n",
                v4l2_subdev_pixelcode_to_string (remote_format.code),
                v4l2_subdev_pixelcode_to_string (format.code));

            format = remote_format;

            ret = v4l2_subdev_set_format (entity, &format, j, which);
            if (ret != 0) {
              GST_DEBUG (" %s : failed to set format on src pad\n", __func__);
            }
          }

          if (format.code != remote_format.code) {
            GST_DEBUG
                ("ERROR - the source and sink pad formats don't match!\n");
          }
        }
      }
    }
    entity_index++;
    entity_list_length = g_list_length (list);
  }
  g_list_free (list);
}

/*
 * This is internal function that adds an entity to a list if it has only source pads
 *
 * input - media device
 * input - An empty GList
 *
 * output - GList containing media_entity * which have only source pads
 */
static GList *
xml_get_src_entity (struct media_device *media, GList *src_list)
{
  int nents = media_get_entities_count (media);
  int i;
  struct media_entity *entity;

  for (i = 0; i < nents; i++) {
    int pad_count, j;
    int has_sink;
    const struct media_entity_desc *info;

    entity = media_get_entity (media, i);
    info = media_entity_get_info (entity);
    pad_count = info->pads;

    has_sink = 0;
    for (j = 0; j < pad_count; j++) {
      const struct media_pad *pad = media_entity_get_pad (entity, j);
      if (pad->flags & MEDIA_PAD_FL_SINK) {
        has_sink = 1;
        break;
      }
    }

    if (!has_sink) {
      /* add entity to list */
      src_list = g_list_prepend (src_list, entity);
    }
  }
  return src_list;
}

/*
 * This is internal function that adds an entity to a list if it has only sink pads
 *
 * input - media device
 * input - An empty GList
 *
 * output - GList containing media_entity * which have only sink pads
 */
static GList *
xml_get_sink_entity (struct media_device *media, GList *sink_list)
{
  int nents = media_get_entities_count (media);
  int i;
  struct media_entity *entity;
  const char *name;

  for (i = 0; i < nents; i++) {
    int pad_count, j;
    int has_sink;
    const struct media_entity_desc *info;

    entity = media_get_entity (media, i);
    info = media_entity_get_info (entity);
    pad_count = info->pads;
    name = media_entity_get_devname (entity);

    GST_DEBUG ("%s : dev name of entity is %s!\n", __func__, name);

    has_sink = 0;
    for (j = 0; j < pad_count; j++) {
      const struct media_pad *pad = media_entity_get_pad (entity, j);
      if (pad->flags & MEDIA_PAD_FL_SINK) {
        has_sink = 1;
        GST_DEBUG ("%s - has_sink!\n", __func__);
        break;
      }
    }

    /* check if sink and of name /dev/video */
    if (has_sink && name &&
        !(strncmp ("/dev/video", name, strlen ("/dev/video")))) {
      /* add entity to list */
      sink_list = g_list_prepend (sink_list, entity);
    } else {
      GST_DEBUG ("%s : has_sink = %d and dev name = %s\n", __func__, has_sink,
          name);
    }
  }
  return sink_list;
}

/*
 * This function auto initializes the media device's entities pads to default values.
 * it also has some exception rules like setting default control values.
 *
 * input - media dev path e.g, /dev/media0
 *
 * output - 0 in case of success else error code
 */
int
xml_init_media_dev (const char *mediadev_path)
{
  struct media_device *media;
  GList *src_ent_list = NULL;
  unsigned int nents;
  struct stat buffer;

  int ret = -1;

  if (!mediadev_path || (stat (mediadev_path, &buffer) != 0))
    return -EINVAL;

  media = media_device_new (mediadev_path);
  if (media == NULL) {
    GST_DEBUG ("Failed to create media device\n");
    goto out;
  }

  /* Enumerate entities, pads and links. */
  ret = media_device_enumerate (media);
  if (ret < 0) {
    GST_DEBUG ("Failed to enumerate %s (%d)\n", mediadev_path, ret);
    goto out;
  }

  /* get a list of entities */
  nents = media_get_entities_count (media);
  GST_DEBUG ("number of entities = %d\n", nents);

  /* get list of source entities */
  src_ent_list = xml_get_src_entity (media, src_ent_list);

  g_list_foreach (src_ent_list, PrintEntityInfo, NULL);

  if (!g_list_length (src_ent_list)) {
    GST_DEBUG ("ERROR - No source entities found!\n");
    ret = -1;
    goto out;
  }

  g_list_free (src_ent_list);

out:if (media)
    media_device_unref (media);

  return ret ? -1 : 0;
}

/*
 * Returns number of media devices present.
 *
 * input - void
 *
 * output - number of /dev/mediaX devices present
 *
 * Limitations - checks for /dev/media0 to /dev/media255
 *
 */
int
xml_get_media_dev_count (void)
{
  int i;
  char media_dev_path[32];
  int count = 0;
  struct stat buffer;

  for (i = 0; i < MAX_MEDIA_DEVS; i++) {

    memset (media_dev_path, 0, sizeof (media_dev_path));
    snprintf (media_dev_path, sizeof (media_dev_path), "/dev/media%d", i);

    if (stat (media_dev_path, &buffer) != 0)
      continue;
    count++;
  }
  return count;
}

/*
 * This function counts the number of /dev/video devices of sink type.
 *
 * input - media dev path e.g, /dev/media0
 *
 * output - number of /dev/videoX devices present else -ve error code
 */
int
xml_get_video_dev_count (char *mediadev_path)
{
  struct media_device *media;
  GList *sink_ent_list = NULL;
  int vid_dev_count;
  int ret = -1;
  unsigned int nents;
  struct stat buffer;

  if (!mediadev_path || (stat (mediadev_path, &buffer) != 0))
    return -EINVAL;

  media = media_device_new (mediadev_path);
  if (media == NULL) {
    GST_DEBUG ("Failed to create media device\n");
    goto out;
  }

  /* Enumerate entities, pads and links. */
  ret = media_device_enumerate (media);
  if (ret < 0) {
    GST_DEBUG ("Failed to enumerate %s (%d)\n", mediadev_path, ret);
    goto out;
  }

  /* get a list of entities */
  nents = media_get_entities_count (media);
  GST_DEBUG ("number of entities = %d\n", nents);

  /* get list of sink entities */
  sink_ent_list = NULL;
  sink_ent_list = xml_get_sink_entity (media, sink_ent_list);

  g_list_foreach (sink_ent_list, PrintEntityInfo, NULL);

  vid_dev_count = g_list_length (sink_ent_list);

  if (!vid_dev_count) {
    GST_DEBUG ("ERROR - No video sink entities found!\n");
    vid_dev_count = -1;
  }

  g_list_free (sink_ent_list);

  ret = vid_dev_count;

out:if (media)
    media_device_unref (media);

  return ret;
}

/*
 * This function returns the name of /dev/video device of sink type for
 * an index number in the media device.
 *
 * input - media dev path e.g, /dev/media0
 * input - index of video device
 * input - allocated array for video device name
 *
 * output - 0 on success else -ve on error
 */
int
xml_get_video_dev_name (char *mediadev_path, unsigned int index,
    char video_dev_name[])
{
  struct media_device *media;
  GList *sink_ent_list = NULL;
  int vid_dev_count;
  int ret = -1;
  const char *dev_name;
  struct media_entity *sink_media_entity;
  unsigned int nents;
  struct stat buffer;

  if (!mediadev_path || (stat (mediadev_path, &buffer) != 0))
    return -EINVAL;

  media = media_device_new (mediadev_path);
  if (media == NULL) {
    GST_DEBUG ("Failed to create media device\n");
    goto out;
  }

  /* Enumerate entities, pads and links. */
  ret = media_device_enumerate (media);
  if (ret < 0) {
    GST_DEBUG ("Failed to enumerate %s (%d)\n", mediadev_path, ret);
    goto out;
  }

  /* get a list of entities */
  nents = media_get_entities_count (media);
  GST_DEBUG ("number of entities = %d\n", nents);

  /* get list of sink entities */
  sink_ent_list = NULL;
  sink_ent_list = xml_get_sink_entity (media, sink_ent_list);

  g_list_foreach (sink_ent_list, PrintEntityInfo, NULL);

  vid_dev_count = g_list_length (sink_ent_list);

  if (index > vid_dev_count) {
    GST_INFO ("index number out of bound!\n");
    GST_DEBUG ("requested index = %d, number of video devices = %d\n",
        index, vid_dev_count);
    ret = -1;
    g_list_free (sink_ent_list);
    goto out;
  }

  sink_media_entity =
      (struct media_entity *) g_list_nth_data (sink_ent_list, index);
  dev_name = media_entity_get_devname (sink_media_entity);
  if (!dev_name) {
    GST_DEBUG ("no device name found!\n");
    ret = -1;
    g_list_free (sink_ent_list);
    goto out;
  }

  strncpy (video_dev_name, dev_name, 32);

  g_list_free (sink_ent_list);

  ret = vid_dev_count;

out:if (media)
    media_device_unref (media);

  return ret;
}

/* Get /dev/mediaX media node from /dev/videoX or /dev/v4l-subdevX */
static gchar *
gv4l2_media_get_device_file (gchar *video_file)
{
  gchar **src_v = NULL;
  gchar *search_path = NULL;
  gchar *search_path_tmp = NULL;
  const gchar *file_name = NULL;
  gchar *media_path = NULL;
  GDir *dir;

  g_return_val_if_fail (video_file, NULL);
  src_v = g_strsplit_set (video_file, "/", 3);
  search_path_tmp = g_strjoin ("/", "/sys/class/video4linux", src_v[2], NULL);
  search_path = g_strjoin ("/", search_path_tmp, "device", NULL);
  dir = g_dir_open (search_path, 0, NULL);
  if (dir) {
    while ((file_name = g_dir_read_name (dir))) {
      if (g_str_has_prefix (file_name, "media"))
        break;
    }
  }

  media_path = g_strjoin ("/", "/dev", file_name, NULL);
  g_dir_close (dir);
  g_strfreev (src_v);
  g_free (search_path_tmp);
  g_free (search_path);

  return media_path;
}

/*
 * This function is used to set the format of the source pad connected to the sink pad
 * of V4L2 video device based on caps or format data provided by user. It is also used
 * to set the format of the source pad connected to the sink pad of V4L2 sub-device if
 * two linked entities source pad and sink pad format mismatch.
 *
 * This is useful in scenarios where a transform like scaling/color conversion is required
 * before the video is written to the memory.
 *
 * input - vfmt - Structure containing the video format to be applied to the source pad
 * 		  of entity connected to the video device.
 * input - video_dev_path - video device path like /dev/video2
 *
 * output - 0 in case the new format is set. Else error.
 *
 */
int
xml_set_connected_src_pad (const char *video_dev_path,
    struct xml_vdev_fmt_info *vfmt)
{
  char *media_path;
  struct media_device *media;
  struct media_entity *entity = NULL;
  unsigned int nents;
  int ret, i;
  GList *src_ent_list = NULL;
  static gboolean once = TRUE;
  GList *list = NULL;
  int entity_index, entity_list_length;

  media_path = gv4l2_media_get_device_file ((gchar *) video_dev_path);

  media = media_device_new (media_path);
  if (media == NULL) {
    GST_DEBUG ("Failed to create media device\n");
    ret = -1;
    goto out;
  }

  /* Enumerate entities, pads and links. */
  ret = media_device_enumerate (media);
  if (ret < 0) {
    GST_DEBUG ("Failed to enumerate %s (%d)\n", media_path, ret);
    goto out;
  }

  /* get a list of entities */
  nents = media_get_entities_count (media);

  /* run media pipe initialization only once */
  if (once) {
    /* get list of source entities */
    src_ent_list = xml_get_src_entity (media, src_ent_list);
    if (!g_list_length (src_ent_list)) {
      GST_DEBUG ("ERROR - No source entities found!\n");
      ret = -1;
      goto out;
    }

    /* configure media pipe */
    g_list_foreach (src_ent_list, xml_srcent_init, vfmt);
    g_list_free (src_ent_list);

    once = FALSE;
  }

  /* Traverse media graph from sink entities to source entities */
  for (i = 0; i < nents; i++) {
    entity = media_get_entity (media, i);

    if (!strcmp (entity->dev_name, video_dev_path))
      break;
  }

  list = g_list_append (list, entity);
  entity_index = 0;
  entity_list_length = g_list_length (list);

  while (entity_index < entity_list_length) {

    int pad_count, j;
    const struct media_entity_desc *info;

    entity = g_list_nth_data (list, entity_index);
    info = media_entity_get_info (entity);
    pad_count = info->pads;

    GST_DEBUG ("%s - media device name = %s\n", __func__, entity->dev_name);

    for (j = 0; j < pad_count; j++) {
      const struct media_pad *pad = media_entity_get_pad (entity, j);
      if (pad->flags & MEDIA_PAD_FL_SINK) {
        int k;
        /* Get the remote pad and apply the fmt on it */

        for (k = 0; k < pad->entity->num_links; ++k) {
          struct media_link *link = &pad->entity->links[k];

          if (!(link->flags & MEDIA_LNK_FL_ENABLED) ||
              (link->flags & MEDIA_LNK_FL_IMMUTABLE))
            continue;

          if (link->sink == pad &&
              link->source->entity->info.type == MEDIA_ENT_T_V4L2_SUBDEV) {

            struct v4l2_mbus_framefmt format;
            struct v4l2_fract interval = { 0, 0 };
            enum v4l2_subdev_format_whence which = V4L2_SUBDEV_FORMAT_ACTIVE;
            struct media_entity *remote_entity = link->source->entity;
            int remote_pad_index = link->source->index;

            ret =
                v4l2_subdev_get_format (remote_entity, &format,
                remote_pad_index, which);
            if (ret != 0) {
              GST_DEBUG (" %s : failed to get format\n", __func__);
              goto out;
            }

            ret =
                v4l2_subdev_get_frame_interval (remote_entity, &interval,
                remote_pad_index);
            if (ret != 0 && ret != -ENOTTY && ret != -EINVAL) {
              GST_DEBUG ("%s - Failed to get frame interval for %s!\n",
                  __func__, media_entity_get_devname (entity));
            }

            GST_DEBUG ("\t get format [pad:%d fmt:%s/%ux%u@%u/%u\n", j,
                v4l2_subdev_pixelcode_to_string (format.code),
                format.width, format.height,
                interval.numerator, interval.denominator);

            /* Configure the source pad connected to sink pad of video device
             * based on caps or format data provided by user
             * Configure the source pad connected to sink pad of V4L2 subdevice
             * if two linked entities source pad and sink pad format mismatch
             */
            if (!strcmp (entity->dev_name, video_dev_path)) {
              GST_DEBUG ("Video entity found!\n");

              format.width = vfmt->width;
              format.height = vfmt->height;
              format.code = vfmt->code;

              interval.denominator = vfmt->frm_den;
              interval.numerator = vfmt->frm_num;
            } else {
              struct v4l2_mbus_framefmt sink_format;
              struct v4l2_fract sink_interval = { 0, 0 };

              ret = v4l2_subdev_get_format (entity, &sink_format, j, which);
              if (ret != 0) {
                GST_DEBUG (" %s : failed to get format\n", __func__);
                goto out;
              }

              ret = v4l2_subdev_get_frame_interval (entity, &sink_interval, j);
              if (ret != 0 && ret != -ENOTTY && ret != -EINVAL) {
                GST_DEBUG ("%s - Failed to get frame interval for %s!\n",
                    __func__, media_entity_get_devname (entity));
              }

              if (format.code == sink_format.code) {
                ret = 0;
                continue;
              }

              format.width = sink_format.width;
              format.height = sink_format.height;
              format.code = sink_format.code;

              interval.denominator = sink_interval.denominator;
              interval.numerator = sink_interval.numerator;
            }

            GST_DEBUG ("\t try to set new format [pad:%d fmt:%s/%ux%u@%u/%u\n",
                remote_pad_index,
                v4l2_subdev_pixelcode_to_string (format.code), format.width,
                format.height, interval.numerator, interval.denominator);

            ret =
                v4l2_subdev_set_format (remote_entity, &format,
                remote_pad_index, which);
            if (ret != 0) {
              GST_DEBUG (" %s : failed to set format on source pad\n",
                  __func__);
              goto out;
            }

            ret =
                v4l2_subdev_set_frame_interval (remote_entity, &interval,
                remote_pad_index);

            GST_DEBUG ("\t set src format [pad:%d fmt:%s/%ux%u@%u/%u\n",
                remote_pad_index,
                v4l2_subdev_pixelcode_to_string (format.code), format.width,
                format.height, interval.numerator, interval.denominator);

            ret = set_default_controls (remote_entity);
            if (ret < 0) {
              GST_DEBUG ("\t Default control set failed\n");
              goto out;
            }

            if (!g_list_find (list, remote_entity)) {
              list = g_list_append (list, remote_entity);
              GST_DEBUG ("Adding %s to entity list\n", remote_entity->dev_name);
            }

            ret = 0;
          }
        }
      }
    }
    entity_index++;
    entity_list_length = g_list_length (list);
  }

out:if (media)
    media_device_unref (media);
  g_list_free (list);

  return ret ? -1 : 0;
}
