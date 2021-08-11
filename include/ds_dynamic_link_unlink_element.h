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

#ifndef _DS_DYNAMIC_LINK_UNLINK_ELEMENT_H_
#define _DS_DYNAMIC_LINK_UNLINK_ELEMENT_H_

#include <gst/gst.h>
#include <glib.h>

typedef struct {

    GstElement *main_element;               /* Element to add and remove dynamically*/
    GstElement *main_prev_element;          /* Element previous to the main element */
    GstElement *main_prev_prev_element;     /* Element previous to previous element of main element in the pipeline */
    GstElement *main_next_element;          /* Element next to main element */

    GstElement *pipeline;                   /* Pipeline */
    GMainLoop *loop;                        /* Main event loop  */

} LinkUnlinkInfo;

gboolean add_element_to_pipeline (LinkUnlinkInfo *info);

gboolean remove_element_from_pipeline (LinkUnlinkInfo *info);

#endif //_DS_DYNAMIC_LINK_UNLINK_ELEMENT_H_
