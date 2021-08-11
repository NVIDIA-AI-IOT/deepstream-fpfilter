/*###############################################################################
 * Copyright (c) 2020-2021, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
*/

/**
 * @brief Sample application showing how to use fpfilter plugin. Saves frames based on the metadata attached by fpfilte plugin for active learning.
 */

#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cuda_runtime_api.h>
#include "gstnvdsmeta.h"
#include "gstnvfpfilter.h"
#include <json-glib/json-glib.h>
#include "ds_usr_prompt_handler.h"
#include "ds_dynamic_link_unlink_element.h"
#include "ds_save_frame.h"

/* The muxer output resolution must be set if the input streams will be of
 * different resolution. The muxer will scale all the input frames to this
 * resolution. */
#define MUXER_OUTPUT_WIDTH 960
#define MUXER_OUTPUT_HEIGHT 544

/* Muxer batch formation timeout, for e.g. 40 millisec. Should ideally be set
 * based on the fastest source's framerate. */
#define MUXER_BATCH_TIMEOUT_USEC 40000

#define PRIMARY_DETECTOR_UID 1

#define TRACKER_CONFIG_FILE   "config/ds_tracker_config.txt"
#define FPFILTER_CONFIG_FILE  "config/ds_fpfilter_config.txt"
#define INFER_PEOPLENET_CONFIG_FILE "config/config_infer_peoplenet.txt"
#define INFER_PEOPLESEMSEGNET_CONFIG_FILE "config/config_infer_peoplesemsegnet.txt"
#define CONFIG_GROUP_PROPERTY                 "property"
#define CONFIG_PROPERTY_ENABLE_FP_FILTER      "enable-fp-filter"

#define FALSE_POSITIVE_PERCENTAGE_THRESHOLD    0.5
#define CHECK_ERROR(error) \
    if (error) { \
        g_printerr ("Error while parsing config file: %s\n", error->message); \
        goto done; \
    }

#define CONFIG_GROUP_TRACKER "tracker"
#define CONFIG_GROUP_TRACKER_WIDTH "tracker-width"
#define CONFIG_GROUP_TRACKER_HEIGHT "tracker-height"
#define CONFIG_GROUP_TRACKER_LL_CONFIG_FILE "ll-config-file"
#define CONFIG_GROUP_TRACKER_LL_LIB_FILE "ll-lib-file"
#define CONFIG_GROUP_TRACKER_ENABLE_BATCH_PROCESS "enable-batch-process"
#define CONFIG_GPU_ID "gpu-id"


#define USR_PROMPT_KEY_MESSAGE            "message"
#define USR_PROMPT_KEY_ACTION             "action"
#define USR_PROMPT_KEY_TARGET             "target"
#define USR_PROMPT_KEY_ENABLE             "enable"
#define USR_PROMPT_KEY_DISABLE            "disable"
#define USR_PROMPT_KEY_SAVE_FP_ENABLE     "save-fp-enable"
#define USR_PROMPT_KEY_SAVE_FP_DISABLE    "save-fp-disable"
#define USR_PROMPT_KEY_DURATION           "duration"

gint frame_number = 0;
static gchar output_path[1024] = {0,};
static gchar source_info[1024] = {0,};
static gboolean is_fpfilter_enabled = FALSE;
LinkUnlinkInfo fp_filter_dynamic_link_info = {0,};
static gboolean save_fpfilter_images = FALSE;
static guint fpfilter_image_cnt = 0;
static GMutex fpfilter_images_save_mutex;

static GAsyncQueue *frame_save_queue = NULL;

GstElement *fpfilter_bin = NULL;

/* Taken from ds test2 app */
/* Tracker config parsing */
static gchar *
get_absolute_file_path (gchar *cfg_file_path, gchar *file_path)
{
  gchar abs_cfg_path[PATH_MAX + 1];
  gchar *abs_file_path;
  gchar *delim;

  if (file_path && file_path[0] == '/') {
    return file_path;
  }

  if (!realpath (cfg_file_path, abs_cfg_path)) {
    g_free (file_path);
    return NULL;
  }

  // Return absolute path of config file if file_path is NULL.
  if (!file_path) {
    abs_file_path = g_strdup (abs_cfg_path);
    return abs_file_path;
  }

  delim = g_strrstr (abs_cfg_path, "/");
  *(delim + 1) = '\0';

  abs_file_path = g_strconcat (abs_cfg_path, file_path, NULL);
  g_free (file_path);

  return abs_file_path;
}

static gboolean
set_tracker_properties (GstElement *nvtracker)
{
  gboolean ret = FALSE;
  GError *error = NULL;
  gchar **keys = NULL;
  gchar **key = NULL;
  GKeyFile *key_file = g_key_file_new ();

  if (!g_key_file_load_from_file (key_file, TRACKER_CONFIG_FILE, G_KEY_FILE_NONE,
          &error)) {
    g_printerr ("Failed to load config file: %s\n", error->message);
    goto done;
  }

  keys = g_key_file_get_keys (key_file, CONFIG_GROUP_TRACKER, NULL, &error);
  CHECK_ERROR (error);

  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_GROUP_TRACKER_WIDTH)) {
      gint width =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TRACKER,
          CONFIG_GROUP_TRACKER_WIDTH, &error);
      CHECK_ERROR (error);
      g_object_set (G_OBJECT (nvtracker), "tracker-width", width, NULL);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_TRACKER_HEIGHT)) {
      gint height =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TRACKER,
          CONFIG_GROUP_TRACKER_HEIGHT, &error);
      CHECK_ERROR (error);
      g_object_set (G_OBJECT (nvtracker), "tracker-height", height, NULL);
    } else if (!g_strcmp0 (*key, CONFIG_GPU_ID)) {
      guint gpu_id =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TRACKER,
          CONFIG_GPU_ID, &error);
      CHECK_ERROR (error);
      g_object_set (G_OBJECT (nvtracker), "gpu_id", gpu_id, NULL);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_TRACKER_LL_CONFIG_FILE)) {
      char* ll_config_file = get_absolute_file_path (TRACKER_CONFIG_FILE,
                g_key_file_get_string (key_file,
                    CONFIG_GROUP_TRACKER,
                    CONFIG_GROUP_TRACKER_LL_CONFIG_FILE, &error));
      CHECK_ERROR (error);
      g_object_set (G_OBJECT (nvtracker), "ll-config-file", ll_config_file, NULL);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_TRACKER_LL_LIB_FILE)) {
      char* ll_lib_file = get_absolute_file_path (TRACKER_CONFIG_FILE,
                g_key_file_get_string (key_file,
                    CONFIG_GROUP_TRACKER,
                    CONFIG_GROUP_TRACKER_LL_LIB_FILE, &error));
      CHECK_ERROR (error);
      g_object_set (G_OBJECT (nvtracker), "ll-lib-file", ll_lib_file, NULL);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_TRACKER_ENABLE_BATCH_PROCESS)) {
      gboolean enable_batch_process =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TRACKER,
          CONFIG_GROUP_TRACKER_ENABLE_BATCH_PROCESS, &error);
      CHECK_ERROR (error);
      g_object_set (G_OBJECT (nvtracker), "enable_batch_process",
                    enable_batch_process, NULL);
    } else {
      g_printerr ("Unknown key '%s' for group [%s]", *key,
          CONFIG_GROUP_TRACKER);
    }
  }

  ret = TRUE;
done:

  if (key_file) {
    g_key_file_free (key_file);
  }

  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    g_printerr ("%s failed", __func__);
  }
  return ret;
}

static GstElement *create_filter_elements_bin(gchar *bin_name)
{
  GstElement *bin = NULL, *nvtracker = NULL, *secondary_detector = NULL, *fpfilter = NULL;
  /* Create a source GstBin to abstract this bin's content from the rest of the pipeline */
  bin = gst_bin_new (bin_name);

  /* We need to have a tracker to track the identified objects */
  nvtracker = gst_element_factory_make ("nvtracker", "tracker");
  secondary_detector = gst_element_factory_make ("nvinfer", "primary-nvinference-engine2");
  fpfilter = gst_element_factory_make ("nvfpfilter", "fp-filter");

  if (!nvtracker || !secondary_detector || !fpfilter)
  {
    g_printerr ("One element could not be created. Exiting.\n");
    return NULL;
  }

  /* Set necessary properties of the tracker element. */
  if (!set_tracker_properties(nvtracker)) {
    g_printerr ("Failed to set tracker properties. Exiting.\n");
    return NULL;
  }

  g_object_set (G_OBJECT (secondary_detector), "config-file-path", INFER_PEOPLESEMSEGNET_CONFIG_FILE, NULL);
  g_object_set (G_OBJECT (fpfilter), "config-file-path", FPFILTER_CONFIG_FILE, NULL);
  g_object_set (G_OBJECT (fpfilter), "enable-fp-filter", TRUE, NULL);

  gst_bin_add_many (GST_BIN (bin), nvtracker, secondary_detector, fpfilter, NULL);
  gst_element_link_many (nvtracker, secondary_detector, fpfilter, NULL);

  GstPad *filter_src_pad = gst_element_get_static_pad (fpfilter, "src");
  if (!filter_src_pad)
  {
    g_print ("Unable to get src pad\n");
    return NULL;
  }

  if (!gst_element_add_pad (bin, gst_ghost_pad_new ("src", filter_src_pad))) {
    g_printerr ("Failed to add ghost pad in fpfilter bin\n");
    bin = NULL;
    gst_object_unref(filter_src_pad);
    return bin;
  }

  gst_object_unref(filter_src_pad);

  GstPad *nvtracker_sink_pad = gst_element_get_static_pad (nvtracker, "sink");
  if (!nvtracker_sink_pad)
  {
    g_print ("Unable to get nvtracker sink pad\n");
    return NULL;
  }

  if (!gst_element_add_pad (bin, gst_ghost_pad_new ("sink", nvtracker_sink_pad))) {
    g_printerr ("Failed to add ghost sink pad in fpfilter bin\n");
    bin = NULL;
    gst_object_unref(nvtracker_sink_pad);
    return bin;
  }
  gst_object_unref(nvtracker_sink_pad);

  return bin;
}


static gboolean
get_fpfilter_images_save_status(void)
{
  gboolean ret = FALSE;
  g_mutex_lock(&fpfilter_images_save_mutex);
  ret = save_fpfilter_images;
  g_mutex_unlock(&fpfilter_images_save_mutex);

  return ret;
}

static void
enable_fpfilter_images_save(void)
{
  g_mutex_lock(&fpfilter_images_save_mutex);
  save_fpfilter_images = TRUE;
  g_mutex_unlock(&fpfilter_images_save_mutex);
}

static void
disable_fpfilter_images_save(void)
{
  g_mutex_lock(&fpfilter_images_save_mutex);
  save_fpfilter_images = FALSE;
  g_mutex_unlock(&fpfilter_images_save_mutex);
}

static gboolean
disable_fpfilter_images_save_duration(gpointer user_data)
{
  disable_fpfilter_images_save();
  /* return false to avoid calling this function repeateadly */
  return FALSE;
}

static gboolean
enable_fpfilter_images_save_duration(guint duration_ms)
{
  enable_fpfilter_images_save();
  g_timeout_add (duration_ms, (GSourceFunc) disable_fpfilter_images_save_duration, NULL);
  return FALSE;
}

static void
enable_fpfilter(void)
{
  if (is_fpfilter_enabled)
  {
    g_print("fpfilter is already enabled\n");
    return;
  }

  fpfilter_bin = create_filter_elements_bin("fp-filter-bin");
  if (!fpfilter_bin)
  {
    g_printerr("fp filter bin creation failed\n");
    return;
  }

  fp_filter_dynamic_link_info.main_element = fpfilter_bin;
  disable_fpfilter_images_save();
  add_element_to_pipeline (&fp_filter_dynamic_link_info);
  is_fpfilter_enabled = TRUE;
  g_print("fpfilter enabled\n");
}

static void
disable_fpfilter(void)
{
  if (!is_fpfilter_enabled)
  {
    g_print("fpfilter is already disabled");
    return;
  }

  is_fpfilter_enabled = FALSE;
  disable_fpfilter_images_save();
  remove_element_from_pipeline (&fp_filter_dynamic_link_info);
  fpfilter_bin = NULL;
  fp_filter_dynamic_link_info.main_element = NULL;
  g_print("fpfilter disabled\n");
}

/* Store output in kitti format */
static void
write_kitti_output (gchar *output_path, guint config_index, NvDsBatchMeta *batch_meta)
{
  gchar bbox_file[1024] = { 0 };
  FILE *bbox_params_dump_file = NULL;

  if (!output_path || (strlen(output_path) == 0))
    return;

  mkdir(output_path, 0700);

  for (NvDsMetaList * l_frame = batch_meta->frame_meta_list; l_frame != NULL; l_frame = l_frame->next) {
    NvDsFrameMeta *frame_meta = l_frame->data;
    g_snprintf (bbox_file, sizeof (bbox_file) - 1, "%s/%06lu.txt", output_path, (gulong) frame_meta->frame_num);
    bbox_params_dump_file = fopen (bbox_file, "w");
    if (!bbox_params_dump_file)
      continue;

    for (NvDsMetaList * l_obj = frame_meta->obj_meta_list; l_obj != NULL;
        l_obj = l_obj->next) {
      NvDsObjectMeta *obj = (NvDsObjectMeta *) l_obj->data;

      if (obj->unique_component_id != PRIMARY_DETECTOR_UID)
        continue;

      float left = obj->rect_params.left;
      float top = obj->rect_params.top;
      float right = left + obj->rect_params.width;
      float bottom = top + obj->rect_params.height;

      float confidence = obj->confidence;
      fprintf (bbox_params_dump_file,
          "%s 0.0 0 0.0 %f %f %f %f 0.0 0.0 0.0 0.0 0.0 0.0 0.0 %f\n",
          obj->obj_label, left, top, right, bottom, confidence);
    }
    fclose (bbox_params_dump_file);
  }
}

static void
save_frames_for_processing (NvDsBatchMeta *batch_meta)
{
  NvDsMetaList *l_frame = NULL;
  for (l_frame = batch_meta->frame_meta_list; l_frame != NULL; l_frame = l_frame->next)
  {
    NvDsFrameMeta *frame_meta = (NvDsFrameMeta *) (l_frame->data);
    NvDsUserMetaList *frame_user_meta_list = NULL;
    NvDsUserMeta *user_meta = NULL;
    for (frame_user_meta_list = frame_meta->frame_user_meta_list; frame_user_meta_list != NULL; frame_user_meta_list = frame_user_meta_list->next)
    {
      user_meta = (NvDsUserMeta *)frame_user_meta_list->data;
      if (user_meta->base_meta.meta_type != NVFPFILTER_USER_META)
        continue;
    }

    if (!user_meta)
    {
      return;
    }
    NvFpFilterMeta *fpfilter_meta = (NvFpFilterMeta *) user_meta->user_meta_data;
    g_print("frame_num: %d tp count: %d fp count: %d\n", frame_meta->frame_num, fpfilter_meta->tp_count, fpfilter_meta->fp_count);

    if (!get_fpfilter_images_save_status())
      continue;

    guint total_objects = fpfilter_meta->tp_count + fpfilter_meta->fp_count;
    if (total_objects <= 1)
      continue;

    gdouble fp_percent = ((gdouble)fpfilter_meta->fp_count)/((gdouble) total_objects);
    if (fp_percent >= FALSE_POSITIVE_PERCENTAGE_THRESHOLD)
    {
      FrameInfo *frame_info = (FrameInfo *) malloc(sizeof(FrameInfo));
      frame_info->frame_index = frame_meta->frame_num;
      frame_info->pad_index = frame_meta->pad_index;
      frame_info->source = source_info;
      g_async_queue_push (frame_save_queue, frame_info);
      fpfilter_image_cnt++;
    }
  }
}

static GstPadProbeReturn
after_filter_buffer_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{
  GstBuffer *buf = (GstBuffer *) info->data;
  NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buf);
  write_kitti_output (output_path, 0, batch_meta);
  save_frames_for_processing(batch_meta);
  frame_number++;
  g_print("frame number: %d\n", frame_number);

  return GST_PAD_PROBE_OK;
}

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *error;
      gst_message_parse_error (msg, &error, &debug);
      g_printerr ("ERROR from element %s: %s\n",
          GST_OBJECT_NAME (msg->src), error->message);
      if (debug)
        g_printerr ("Error details: %s\n", debug);
      g_free (debug);
      g_error_free (error);
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

/**
 * Probe function to drop upstream "GST_QUERY_SEEKING" query from h264parse element.
 * This is a WAR to avoid memory leaks from h264parse element.
 */
static GstPadProbeReturn
seek_query_drop_prob (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{
  if (GST_PAD_PROBE_INFO_TYPE(info) &
   GST_PAD_PROBE_TYPE_QUERY_UPSTREAM)
  {
    GstQuery *query = GST_PAD_PROBE_INFO_QUERY(info);
    if(GST_QUERY_TYPE (query) == GST_QUERY_SEEKING)
    {
      return GST_PAD_PROBE_DROP;
    }
  }
  return GST_PAD_PROBE_OK;
}

#ifdef MP4_SRC

GstElement *mp4_h264parser = NULL;

static void
qtdemux_new_pad_cb (GstElement *element, GstPad *pad, gpointer data)
{
  gchar *name = gst_pad_get_name (pad);
  if (strcmp (name, "video_0") == 0)
  {
    GstPad *h264parser_sinkpad = gst_element_get_static_pad (mp4_h264parser, "sink");
    if (!h264parser_sinkpad) {
      g_printerr ("mp4_h264parser getting sink pad failed. Exiting.\n");
      g_free (name);
      return;
    }

    if (gst_pad_link (pad, h264parser_sinkpad) != GST_PAD_LINK_OK) {
      g_printerr ("Failed to link decoder to stream muxer. Exiting.\n");
    }
    gst_object_unref (h264parser_sinkpad);
  }

  g_free (name);
}

/* Create mp4 source element */
static GstElement *
create_source_bin(gchar *bin_name, gchar *location)
{
  GstElement *bin = NULL, *source = NULL, *decoder = NULL, *qtdemux = NULL;
  gboolean multi_file_src = FALSE;

  /* Create a source GstBin to abstract this bin's content from the rest of the pipeline */
  bin = gst_bin_new (bin_name);
  /* Source element for reading from the file */
  source = gst_element_factory_make ("filesrc", "file-source");

  /* We need demuxer to separate audio and video */
  qtdemux = gst_element_factory_make ("qtdemux", "qtdemux");

  /* Since the data format in the input file is h264 stream,
   * we need a h264parser */
  mp4_h264parser = gst_element_factory_make ("h264parse", "h264-parser");
  /* Use nvdec_h264 for hardware accelerated decode on GPU */
  decoder = gst_element_factory_make ("nvv4l2decoder", "nvv4l2-decoder");

  if (!source || !mp4_h264parser || !decoder)
  {
    g_printerr ("One element could not be created. Exiting.\n");
    return NULL;
  }

  g_object_set (G_OBJECT (source), "location", location, NULL);

  gst_bin_add_many (GST_BIN (bin), source, qtdemux, mp4_h264parser, decoder, NULL);

  gst_element_link_many (source, qtdemux, NULL);
  gst_element_link_many (mp4_h264parser, decoder, NULL);

  g_signal_connect (qtdemux, "pad-added", G_CALLBACK (qtdemux_new_pad_cb), NULL);

  /* Create a ghost pad for bin */
  GstPad *decoder_srcpad = gst_element_get_static_pad (decoder, "src");
  if (!decoder_srcpad) {
    g_printerr ("Failed to get src pad of source bin. Exiting.\n");
    return NULL;
  }

  if (!gst_element_add_pad (bin, gst_ghost_pad_new ("src", decoder_srcpad))) {
    g_printerr ("Failed to add ghost pad in source bin\n");
    bin = NULL;
    goto done;
  }

done:
  if (decoder_srcpad)
    gst_object_unref(decoder_srcpad);

  return bin;
}
#endif /* MP4_SRC */

#ifdef H264_ELEMENTARY_SRC
/* Create elementary h264 source element */
static GstElement *
create_source_bin(gchar *bin_name, gchar *location)
{
  GstElement *bin = NULL, *source = NULL, *h264parser = NULL, *decoder = NULL;
  gboolean multi_file_src = FALSE;

  /* Create a source GstBin to abstract this bin's content from the rest of the pipeline */
  bin = gst_bin_new (bin_name);
  /* Source element for reading from the file */
  source = gst_element_factory_make ("filesrc", "file-source");
  /* Since the data format in the input file is elementary h264 stream,
   * we need a h264parser */
  h264parser = gst_element_factory_make ("h264parse", "h264-parser");
  /* Use nvdec_h264 for hardware accelerated decode on GPU */
  decoder = gst_element_factory_make ("nvv4l2decoder", "nvv4l2-decoder");

  if (!source || !h264parser || !decoder)
  {
    g_printerr ("One element could not be created. Exiting.\n");
    return NULL;
  }

  g_object_set (G_OBJECT (source), "location", location, NULL);

  gst_bin_add_many (GST_BIN (bin), source, h264parser, decoder, NULL);

  gst_element_link_many (source, h264parser, decoder, NULL);

  /* Create a ghost pad for bin */

  GstPad *decoder_srcpad = gst_element_get_static_pad (decoder, "src");
  if (!decoder_srcpad) {
    g_printerr ("Failed to get src pad of source bin. Exiting.\n");
    return NULL;
  }

  if (!gst_element_add_pad (bin, gst_ghost_pad_new ("src", decoder_srcpad))) {
    g_printerr ("Failed to add ghost pad in source bin\n");
    bin = NULL;
    goto done;
  }

done:
  if (decoder_srcpad)
    gst_object_unref(decoder_srcpad);

  return bin;
}
#endif

#ifdef MULTI_FILE_SRC
/* Create elementary multifile source element */
static GstElement *
create_source_bin(gchar *bin_name, gchar *location)
{
  GstElement *bin = NULL, *source = NULL, *jpegparser = NULL, *decoder = NULL, *cap_filter=NULL;
  GstCaps *caps = NULL;

  int current_device = -1;
  cudaGetDevice(&current_device);
  struct cudaDeviceProp prop;
  cudaGetDeviceProperties(&prop, current_device);

  /* Create a source GstBin to abstract this bin's content from the rest of the pipeline */
  bin = gst_bin_new (bin_name);
  /* Source element for reading from the file */
  source = gst_element_factory_make("multifilesrc", "source");
  if (!source)
  {
    g_printerr ("multifilesrc create failed. Exiting.\n");
    return NULL;
  }

  caps = gst_caps_from_string ("image/jpeg,framerate=30/1");
  g_object_set (G_OBJECT (source), "caps", caps, NULL);
  if (caps)
    gst_caps_unref (caps);

  jpegparser = gst_element_factory_make ("jpegparse", "jpeg-parser");
  if (!jpegparser)
  {
    g_printerr ("jpegparse create failed. Exiting.\n");
    return NULL;
  }

  /* Use nvdec_h264 for hardware accelerated decode on GPU */
  decoder = gst_element_factory_make ("nvv4l2decoder", "nvv4l2-decoder");
  if (!decoder)
  {
    g_printerr ("nvv4l2decoder create failed. Exiting.\n");
    return NULL;
  }

  g_object_set (G_OBJECT (source), "location", location, NULL);

  if(prop.integrated) {
    g_object_set (G_OBJECT (decoder), "mjpeg", 1, NULL);
  }

  gst_bin_add_many (GST_BIN (bin), source, jpegparser, decoder, NULL);

  gst_element_link_many (source, jpegparser, decoder, NULL);

  /* Create a ghost pad for bin */

  GstPad *decoder_srcpad = gst_element_get_static_pad (decoder, "src");
  if (!decoder_srcpad) {
    g_printerr ("Failed to get src pad of source bin. Exiting.\n");
    return NULL;
  }

  if (!gst_element_add_pad (bin, gst_ghost_pad_new ("src", decoder_srcpad))) {
    g_printerr ("Failed to add ghost pad in source bin\n");
    bin = NULL;
    goto done;
  }

done:
  if (decoder_srcpad)
    gst_object_unref(decoder_srcpad);

  return bin;
}
#endif //MULTI_FILE_SRC

#ifdef VIDEO_RENDER_SINK
/* out_name will be ignored if the sink is video renderer */
static GstElement *
create_sink_bin (gchar *sink_name, gchar *out_name)
{
  GstElement *bin = NULL, *sink=NULL, *transform=NULL;

  int current_device = -1;
  cudaGetDevice(&current_device);
  struct cudaDeviceProp prop;
  cudaGetDeviceProperties(&prop, current_device);

  /* Create a source GstBin to abstract this bin's content from the rest of the
   * pipeline */
  bin = gst_bin_new (sink_name);

#ifdef PLATFORM_TEGRA
  transform = gst_element_factory_make ("nvegltransform", "nvegl-transform");
#endif

  sink = gst_element_factory_make ("nveglglessink", "nvvideo-renderer");
  if (!sink) {
    g_printerr("Failed to create '%s'", "nvvideo-renderer");
    return NULL;
  }

#ifdef PLATFORM_TEGRA
  if(!transform) {
    g_printerr ("One tegra element could not be created. Exiting.\n");
    return NULL;
  }
#endif

GstPad *pad = NULL;
#ifdef PLATFORM_TEGRA

  gst_bin_add_many (GST_BIN (bin), transform, sink, NULL);

  if (!gst_element_link_many (transform, sink, NULL)) {
    g_printerr ("Elements could not be linked: 2. Exiting.\n");
    return NULL;
  }

  /* Create a ghost pad for bin */
  pad = gst_element_get_static_pad (transform, "sink");
  if (!pad) {
    g_printerr ("Failed to get sink pad of transform element. Exiting.\n");
    return NULL;
  }

  if (!gst_element_add_pad (bin, gst_ghost_pad_new ("sink", pad))) {
    g_printerr ("Failed to add ghost pad in sink bin\n");
    return NULL;
  }

#else

  gst_bin_add_many (GST_BIN (bin), sink, NULL);

  /* Create a ghost pad for bin */
  pad = gst_element_get_static_pad (sink, "sink");
  if (!pad) {
    g_printerr ("Failed to get sink pad of sink element. Exiting.\n");
    return NULL;
  }

  if (!gst_element_add_pad (bin, gst_ghost_pad_new ("sink", pad))) {
    g_printerr ("Failed to add ghost pad in sink bin\n");
    return NULL;
  }

#endif

done:
  if (pad)
    gst_object_unref(pad);

  return bin;
}

#endif //VIDEO_RENDER_SINK

#ifdef FILE_SINK
static GstElement *
create_sink_bin (gchar *bin_name, gchar *out_name)
{
  GstElement *bin = NULL;
  GstCaps *caps = NULL;
  int probe_id = 0;
  GstElement *sink_cap_filter = NULL, *sink_encoder = NULL, *sink_codecparse = NULL, *sink_mux = NULL, *sink=NULL;

  int current_device = -1;
  cudaGetDevice(&current_device);
  struct cudaDeviceProp prop;
  cudaGetDeviceProperties(&prop, current_device);

  /* Create a source GstBin to abstract this bin's content from the rest of the
   * pipeline */
  bin = gst_bin_new (bin_name);

  sink_cap_filter = gst_element_factory_make ("capsfilter", "capsfilter0");
  if (!sink_cap_filter) {
    g_printerr("Failed to create '%s'", "capsfilter0");
    return NULL;
  }
  caps = gst_caps_from_string ("video/x-raw(memory:NVMM), format=I420");
  g_object_set (G_OBJECT (sink_cap_filter), "caps", caps, NULL);

  sink_encoder = gst_element_factory_make ("nvv4l2h264enc", "encoder0");
  if (!sink_encoder) {
    g_printerr("Failed to create '%s'", "encoder0");
    return NULL;
  }

  GstPad *gstpad = gst_element_get_static_pad (sink_encoder, "sink");
  if (!gstpad) {
    g_printerr("Could not find '%s' in '%s'", "sink", GST_ELEMENT_NAME(sink_encoder));
    return NULL;
  }
  probe_id = gst_pad_add_probe(gstpad, GST_PAD_PROBE_TYPE_QUERY_UPSTREAM, seek_query_drop_prob, NULL, NULL);
  gst_object_unref (gstpad);

  if(prop.integrated) {
    g_object_set (G_OBJECT (sink_encoder), "bufapi-version", 1, NULL);
  }

  g_object_set (G_OBJECT (sink_encoder), "profile", 0, NULL);
  g_object_set (G_OBJECT (sink_encoder), "iframeinterval", 30, NULL);
  g_object_set (G_OBJECT (sink_encoder), "bitrate", 6000000, NULL);

  sink_codecparse = gst_element_factory_make("h264parse", "h264-parser-sink");
  if (!sink_codecparse) {
    g_printerr("Failed to create '%s'", "h264-parser-sink");
    return NULL;
  }

  sink_mux = gst_element_factory_make ("qtmux", "qtmux-sink");
  if (!sink_mux) {
    g_printerr("Failed to create '%s'", "qtmux-sink");
    return NULL;
  }

  sink = gst_element_factory_make ("filesink", "file-sink");
  if (!sink) {
    g_printerr("Failed to create '%s'", "file-sink");
    return NULL;
  }

  g_object_set (G_OBJECT (sink), "location", out_name, "sync", FALSE, "async", FALSE, NULL);

  gst_bin_add_many (GST_BIN (bin), sink_cap_filter, sink_encoder, sink_codecparse, sink_mux, sink, NULL);

  if (!gst_element_link_many (sink_cap_filter, sink_encoder, sink_codecparse, sink_mux, sink, NULL)) {
    g_printerr ("Elements could not be linked: 2. Exiting.\n");
    return NULL;
  }

  /* Create a ghost pad for bin */
  GstPad *sinkpad = gst_element_get_static_pad (sink_cap_filter, "sink");
  if (!sinkpad) {
    g_printerr ("Failed to get sink pad of filter element. Exiting.\n");
    return NULL;
  }

  if (!gst_element_add_pad (bin, gst_ghost_pad_new ("sink", sinkpad))) {
    g_printerr ("Failed to add ghost pad in sink bin\n");
    bin = NULL;
    goto done;
  }

done:
  if (sinkpad)
    gst_object_unref (sinkpad);

  if (caps) {
    gst_caps_unref (caps);
  }
  return bin;
}

#endif //FILE_SINK

static void
handle_usr_prompt(guchar *msg, guint len)
{
  g_print("%s\n", msg);

  JsonParser *parser = json_parser_new ();
  GError *error;
  gboolean result = json_parser_load_from_data (parser, (const gchar *) msg, len, &error);
  if (!result)
  {
    g_print("message parse failed\n");
    return;
  }

  JsonNode *root_node = json_parser_get_root (parser);
  if (!JSON_NODE_HOLDS_OBJECT(root_node))
  {
    g_print("message parse error\n");
    return;
  }

  JsonObject *root_object = json_node_get_object (root_node);
  if (!json_object_has_member (root_object, USR_PROMPT_KEY_MESSAGE))
  {
    g_print("message parse error: no message object\n");
    return;
  }

  JsonNode *arr_node = json_object_get_member (root_object, USR_PROMPT_KEY_MESSAGE);
  if (!JSON_NODE_HOLDS_ARRAY(arr_node))
  {
    g_print("message parse error: message\n");
    return;
  }

  JsonArray *arr = json_node_get_array (arr_node);
  guint arr_len = json_array_get_length (arr);
  for (guint idx=0; idx<arr_len; idx++)
  {
    JsonObject *arr_obj = json_array_get_object_element (arr, idx);
    if (!json_object_has_member (arr_obj, USR_PROMPT_KEY_TARGET))
    {
      g_print("message parse error: no target object\n");
      continue;
    }

    const gchar *target = json_object_get_string_member (arr_obj, USR_PROMPT_KEY_TARGET);
    if (!g_strcmp0(target, "fpfilter")) 
    {
      if (!json_object_has_member (arr_obj, USR_PROMPT_KEY_ACTION))
      {
        g_print("action not found\n");
        continue;
      }
      const gchar *action = json_object_get_string_member (arr_obj, USR_PROMPT_KEY_ACTION);
      if (!g_strcmp0(action, USR_PROMPT_KEY_ENABLE))
      {
        enable_fpfilter();
      }
      else if (!g_strcmp0(action, USR_PROMPT_KEY_DISABLE))
      {
        disable_fpfilter();
      }
      else if (!g_strcmp0(action, USR_PROMPT_KEY_SAVE_FP_ENABLE))
      {
        if (!json_object_has_member (arr_obj, USR_PROMPT_KEY_DURATION))
        {
          g_print("duration not found\n");
          enable_fpfilter_images_save();
        }
        else
        {
          const guint duration_ms = (guint) json_object_get_int_member (arr_obj, USR_PROMPT_KEY_DURATION);
          g_print("duration ms: %d\n", duration_ms);
          enable_fpfilter_images_save_duration(duration_ms);
        }
      }
      else if (!g_strcmp0(action, USR_PROMPT_KEY_SAVE_FP_DISABLE))
      {
        disable_fpfilter_images_save();
      }
    }
  }

  g_object_unref(parser);
}

gboolean get_fpfilter_status_from_cfg_file(const gchar *cfg_file_path)
{
  GKeyFile *key_file = g_key_file_new ();
  GError *error = NULL;
  gboolean ret = FALSE;

  if (!g_key_file_load_from_file (key_file, cfg_file_path, G_KEY_FILE_NONE, &error))
  {
    g_printerr ("Failed to load config file: %s\n", error->message);
    goto done;
  }

  if (!g_key_file_has_group (key_file, CONFIG_GROUP_PROPERTY))
  {
    g_printerr ("Could not find group %s\n", CONFIG_GROUP_PROPERTY);
    goto done;
  }

  gboolean is_enabled = TRUE;
  if (g_key_file_has_key (key_file, CONFIG_GROUP_PROPERTY, CONFIG_PROPERTY_ENABLE_FP_FILTER, NULL))
  {
    is_enabled = g_key_file_get_boolean (key_file, CONFIG_GROUP_PROPERTY, CONFIG_PROPERTY_ENABLE_FP_FILTER, &error);
    CHECK_ERROR (error);
  }

done:
  if (key_file) {
    g_key_file_free (key_file);
  }

  if (error) {
    g_error_free (error);
  }

  return is_enabled;
}


int
main (int argc, char *argv[])
{
  GMainLoop *loop = NULL;
  GstElement *pipeline = NULL, *source = NULL, *streammux = NULL, *primary_detector = NULL,
    *nvvidconv = NULL, *nvvidconv1 = NULL, *nvosd = NULL, *sink = NULL;

  GstBus *bus = NULL;
  guint bus_watch_id;

  /* Check input arguments */
  if (argc < 4) {
    g_printerr ("Usage: %s <location_of_multifilesrc_input> <location_to_save_kitti_labels> <location_to_save_output_video>\n", argv[0]);
    return -1;
  }

  int current_device = -1;
  cudaGetDevice(&current_device);
  struct cudaDeviceProp prop;
  cudaGetDeviceProperties(&prop, current_device);

  /* Standard GStreamer initialization */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* Create gstreamer elements */
  /* Create Pipeline element that will form a connection of other elements */
  pipeline = gst_pipeline_new ("pipeline");

  snprintf(output_path, 1024, "%s", argv[2]);

  strcpy(source_info, argv[1]);
  source = create_source_bin("source_bin", argv[1]);

  /* Create nvstreammux instance to form batches from one or more sources. */
  streammux = gst_element_factory_make ("nvstreammux", "stream-muxer");

  if (!pipeline || !streammux) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  /* Create two nvinfer instances for the two back-to-back detectors */
  primary_detector = gst_element_factory_make ("nvinfer", "primary-nvinference-engine1");

  /* Use convertor to convert from NV12 to RGBA as required by nvosd */
  nvvidconv = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter");

  nvvidconv1 = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter1");

  /* Create OSD to draw on the converted RGBA buffer */
  nvosd = gst_element_factory_make ("nvdsosd", "nv-onscreendisplay");

  if (!source || !primary_detector || !nvvidconv || !nvvidconv1 || !nvosd) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  g_object_set (G_OBJECT (streammux), "width", MUXER_OUTPUT_WIDTH, "height",
      MUXER_OUTPUT_HEIGHT, "batch-size", 1,
      "batched-push-timeout", MUXER_BATCH_TIMEOUT_USEC, NULL);

  g_object_set (G_OBJECT (primary_detector), "config-file-path", INFER_PEOPLENET_CONFIG_FILE, NULL);

  g_object_set (G_OBJECT (nvosd), "display-mask", 1, NULL);
  g_object_set (G_OBJECT (nvosd), "display-bbox", 1, NULL);
  g_object_set (G_OBJECT (nvosd), "process-mode", 0, NULL);

  /* we add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  sink = create_sink_bin ("sink_bin", argv[3]);

  is_fpfilter_enabled = get_fpfilter_status_from_cfg_file(FPFILTER_CONFIG_FILE);
  if (is_fpfilter_enabled)
  {
    fpfilter_bin = create_filter_elements_bin("fp-filter-bin");
    if (!fpfilter_bin)
    {
      g_printerr("fp filter bin creation failed\n");
      return -1;
    }
  }

  fp_filter_dynamic_link_info.main_element = fpfilter_bin;
  fp_filter_dynamic_link_info.main_prev_element = primary_detector;
  fp_filter_dynamic_link_info.main_prev_prev_element = streammux;
  fp_filter_dynamic_link_info.main_next_element = nvvidconv;
  fp_filter_dynamic_link_info.pipeline = pipeline;
  fp_filter_dynamic_link_info.loop = loop;

  g_mutex_init (&fpfilter_images_save_mutex);

  gst_bin_add_many (GST_BIN (pipeline), source, streammux, primary_detector, nvosd, nvvidconv, nvvidconv1, sink, NULL);
  if (is_fpfilter_enabled)
    gst_bin_add(GST_BIN (pipeline), fpfilter_bin);

  GstPad *sinkpad = NULL, *srcpad = NULL;

  sinkpad = gst_element_get_request_pad (streammux, "sink_0");
  if (!sinkpad) {
    g_printerr ("Streammux request sink pad failed. Exiting.\n");
    return -1;
  }

  srcpad = gst_element_get_static_pad (source, "src");
  if (!srcpad) {
    g_printerr ("Decoder request src pad failed. Exiting.\n");
    gst_object_unref (sinkpad);
    return -1;
  }

  if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK) {
    g_printerr ("Failed to link decoder to stream muxer. Exiting.\n");
    gst_object_unref (sinkpad);
    gst_object_unref (srcpad);
    return -1;
  }

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  if (!is_fpfilter_enabled)
  {
    if (!gst_element_link_many (streammux, primary_detector, nvvidconv, nvosd, nvvidconv1, sink, NULL)) {
      g_printerr ("Elements could not be linked: 2. Exiting.\n");
      return -1;
    }
  }
  else
  {
    if (!gst_element_link_many (streammux, primary_detector, fpfilter_bin, nvvidconv, nvosd, nvvidconv1, sink, NULL)) {
      g_printerr ("Elements could not be linked: 2. Exiting.\n");
      return -1;
    }
  }

  /* Adding probe before and after filter element to save kitti data */
  GstPad *nvvidconv_sink_pad = gst_element_get_static_pad (nvvidconv, "sink");
  if (!nvvidconv_sink_pad)
  {
    g_print ("Unable to get nvvidconv sink pad\n");
    return -1;
  }

  gst_pad_add_probe (nvvidconv_sink_pad, GST_PAD_PROBE_TYPE_BUFFER, after_filter_buffer_probe, NULL, NULL);
  gst_object_unref (nvvidconv_sink_pad);

  frame_save_queue = g_async_queue_new ();
  start_save_frame_task(frame_save_queue);
  /* Set the pipeline to "playing" state */
  g_print ("Now playing: %s\n", argv[1]);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  start_usr_prompt_monitor(handle_usr_prompt);

  /* Wait till pipeline encounters an error or EOS */
  g_print ("Running...\n");
  g_main_loop_run (loop);

  /* Out of the main loop, clean up nicely */
  stop_usr_prompt_monitor();
  stop_save_frame_task();
  g_async_queue_unref(frame_save_queue);
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  g_print("saved images cnt: %d\n", fpfilter_image_cnt);
  return 0;
}
