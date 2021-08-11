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

#ifndef __GST_NVFPFILTER_H__
#define __GST_NVFPFILTER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/*set the user metadata type*/
#define NVFPFILTER_USER_META (nvds_get_user_meta_type(((gchar *)"NVIDIA.NVFPFILTER.USERMETA")))

/**
 * Holds the false positive and true positive information for one frame.
 *
 *
 * This meta data is added as NvDsUserMeta to the frame_user_meta_list of the
 * corresponding frame_meta.
 */
typedef struct
{
  guint fp_count;   /* Holds false positive count as assessed by fpfilter */
  guint tp_count;   /* Holds true positive count as assessed by fpfilter */
} NvFpFilterMeta;


G_END_DECLS

#endif /* __GST_NVFPFILTER_H__ */
