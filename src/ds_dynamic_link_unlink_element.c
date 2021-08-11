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

#include "ds_dynamic_link_unlink_element.h"

/**
 * 
 * @brief   Implements apis to link and unlink an element from pipeline dynamically (during runtime).
 * @todo    Apis only work when the elments in the LinkUnlinkInfo struct has static pads. Needs to be extended to support request pads.
 * 
 */

static GstPadProbeReturn
add_elem_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  LinkUnlinkInfo *link_unlink_info = (LinkUnlinkInfo *)user_data;
  gst_pad_remove_probe (pad, GST_PAD_PROBE_INFO_ID (info));
  g_print ("Adding element\n");

  gst_bin_add (GST_BIN (link_unlink_info->pipeline), link_unlink_info->main_element);

  GstPad *src_pad  = gst_element_get_static_pad (link_unlink_info->main_prev_element, "src");
  GstPad *sink_pad = gst_element_get_static_pad (link_unlink_info->main_next_element, "sink");

  if (!src_pad || !sink_pad)
  {
    g_print("src_pad or sink_pad is NULL\n");
    return FALSE;
  }

  gboolean unlink_status = gst_pad_unlink (src_pad, sink_pad);
  if (!unlink_status)
  {
    g_print("unlink failed\n");
    return GST_PAD_PROBE_DROP;
  }

  gst_object_unref (src_pad);
  gst_object_unref (sink_pad);


  g_print("linking..\n");
  gst_element_link_many (link_unlink_info->main_prev_element, link_unlink_info->main_element, link_unlink_info->main_next_element, NULL);
  gst_element_set_state (link_unlink_info->main_element, GST_STATE_PLAYING);
  free(link_unlink_info);
  return GST_PAD_PROBE_DROP;
}


static GstPadProbeReturn
remove_elem_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  LinkUnlinkInfo *link_unlink_info = (LinkUnlinkInfo *)user_data;

  if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_DATA (info)) != GST_EVENT_EOS)
    return GST_PAD_PROBE_PASS;

  gst_pad_remove_probe (pad, GST_PAD_PROBE_INFO_ID (info));

  gst_element_set_state (link_unlink_info->main_element, GST_STATE_NULL);

  /* remove unlinks automatically */
  gst_bin_remove (GST_BIN (link_unlink_info->pipeline), link_unlink_info->main_element);

  gst_element_link_many (link_unlink_info->main_prev_element, link_unlink_info->main_next_element, NULL);
  free(link_unlink_info);
  return GST_PAD_PROBE_DROP;
}

static GstPadProbeReturn
pad_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstPad *srcpad = NULL, *sinkpad = NULL;
  LinkUnlinkInfo *link_unlink_info = (LinkUnlinkInfo *)user_data;
  gst_pad_remove_probe (pad, GST_PAD_PROBE_INFO_ID (info));

  /* install new probe for EOS */
  srcpad = gst_element_get_static_pad (link_unlink_info->main_element, "src");
  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, remove_elem_probe_cb, user_data, NULL);
  gst_object_unref (srcpad);

  /* push EOS into the element, the probe will be fired when the
   * EOS leaves the element and it has thus drained all of its data */
  sinkpad = gst_element_get_static_pad (link_unlink_info->main_element, "sink");
  gst_pad_send_event (sinkpad, gst_event_new_eos ());
  gst_object_unref (sinkpad);

  return GST_PAD_PROBE_OK;
}

static LinkUnlinkInfo *make_copy_link_unlink_info(LinkUnlinkInfo *info)
{
  LinkUnlinkInfo *info_copy = (LinkUnlinkInfo *) malloc(sizeof(LinkUnlinkInfo));
  info_copy->main_element = info->main_element;
  info_copy->main_prev_element = info->main_prev_element;
  info_copy->main_prev_prev_element = info->main_prev_prev_element;
  info_copy->main_next_element = info->main_next_element;
  info_copy->pipeline = info->pipeline;
  info_copy->loop = info->loop;

  return info_copy;
}

gboolean
add_element_to_pipeline (LinkUnlinkInfo *info)
{
  info = make_copy_link_unlink_info(info);
  GstPad *block_src_pad = gst_element_get_static_pad (info->main_prev_prev_element, "src"); /* Needs to be extended to support request pads */
  if (!block_src_pad)
  {
    g_print("block_src_pad is NULL\n");
    return FALSE;
  }

  gst_pad_add_probe (block_src_pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, add_elem_probe_cb, info, NULL);
  gst_object_unref (block_src_pad);
  return TRUE;
}

gboolean
remove_element_from_pipeline (LinkUnlinkInfo *info)
{
  info = make_copy_link_unlink_info(info);
  GstPad *block_src_pad = gst_element_get_static_pad (info->main_prev_prev_element, "src"); /* Needs to be extended to support request pads */
  if (!block_src_pad)
  {
    g_print("block_src_pad is NULL\n");
    return FALSE;
  }

  gst_pad_add_probe (block_src_pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, pad_probe_cb, info, NULL);
  gst_object_unref (block_src_pad);
  return TRUE;
}
