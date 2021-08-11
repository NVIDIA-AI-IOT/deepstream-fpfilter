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
 * 
 * @brief   Implements apis to upload images to cloud. Uploading is done asynchronously with pipeline through queues. For uploading, application just triggers 
 *          a simple python script to upload to cloud.
 * 
 */

#include <unistd.h>
#include "ds_save_frame.h"

static gboolean g_stop_save_frame_thread = FALSE;
static GMutex g_stop_save_frame_thread_mutex;
static GAsyncQueue *g_frames_queue = NULL;

/* task to save frames */
static gpointer
_save_frame_task(gpointer arg)
{
    g_mutex_lock(&g_stop_save_frame_thread_mutex);
    while(!g_stop_save_frame_thread)
    {
        g_mutex_unlock(&g_stop_save_frame_thread_mutex);

        while (g_async_queue_length (g_frames_queue) != 0)
        {
            FrameInfo *frame_info = (FrameInfo *) g_async_queue_pop (g_frames_queue);
            gchar cmd[1024] = {0,};
            g_snprintf(cmd, 1024, "%s %s %d %d", "./src/save_image.sh", frame_info->source, frame_info->pad_index, frame_info->frame_index);
            g_print("saving: %s %d %d\n", frame_info->source, frame_info->pad_index, frame_info->frame_index);
            system(cmd);
            free(frame_info);
        }

        usleep(1000); /* Sleep for 1ms until next frames are availble */
        g_mutex_lock (&g_stop_save_frame_thread_mutex);
    }
    g_mutex_unlock(&g_stop_save_frame_thread_mutex);
    g_mutex_clear(&g_stop_save_frame_thread_mutex);
    return NULL;
}

void
start_save_frame_task(GAsyncQueue *queue)
{
    g_stop_save_frame_thread = FALSE;
    g_frames_queue = queue;
    /* mutex to stop the task. */
    g_mutex_init (&g_stop_save_frame_thread_mutex);
    /* Start the thread. */
    GThread *g_save_frame_thread = g_thread_new ("DS save frames thread", _save_frame_task, NULL);
    g_thread_unref (g_save_frame_thread);
}

void
stop_save_frame_task(void)
{
    /* wait until all the queued frames are uploaded */
    while (g_async_queue_length (g_frames_queue) != 0)
    {
        usleep(1000);
    }

    g_mutex_lock(&g_stop_save_frame_thread_mutex);
    g_stop_save_frame_thread = TRUE;
    g_mutex_unlock(&g_stop_save_frame_thread_mutex);
}

