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
 * @brief   Implements simple server to handle user prompts. Reads message from user and invokes application callback with message.
 * 
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include "glib.h"
#include "ds_usr_prompt_handler.h"

#define DEFAULT_MONITOR_PORT     43434
#define MAX_PACKET_LEN          (4 * 1024)

struct usr_prompt_info
{
    user_prompt_callback msg_cb;
};

static gint g_server_port = -1;
static gboolean g_stop_server = FALSE;
static GMutex g_stop_server_mutex;
static GThread *g_monitor_thread;

static void _read_bytes(gint fd, guint read_len, guchar *buffer)
{
    guint len = 0;
    while (len < read_len)
    {
        len += read(fd, buffer + len, read_len - len);
    }
}

static guint _read_message(gint fd, guchar *msg)
{
    g_print("reading user message\n");
    /* First read len of the message */
    guchar len_buff[2] = {0,};
    _read_bytes(fd, 2, len_buff);
    guint16 len = 0;
    memcpy(&len, len_buff, 2);

    /* read message */
    _read_bytes(fd, len, msg);
    return (guint) len;
}

/* Server task to monitor for user prompts */
static gpointer _server_task(gpointer arg)
{
    guint sockfd;
    struct sockaddr_in servaddr;

    struct usr_prompt_info *info = (struct usr_prompt_info *) arg;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) { 
        g_print("socket creation failed...\n");
        exit(0);
    }

    memset(&servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET; 
    servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    servaddr.sin_port = htons(g_server_port);

    if (bind(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) != 0) { 
        g_print("socket bind failed, port: %d\n", g_server_port);
        return NULL;
    }

    int connfd;
    unsigned int len = 0;
    struct sockaddr_in cli;

    // listen
    if ((listen(sockfd, 1)) != 0) { 
        g_print("Listen failed...\n"); 
        exit(0); 
    }
    len = sizeof(cli);

    g_mutex_lock(&g_stop_server_mutex);
    while(!g_stop_server)
    {
        g_mutex_unlock(&g_stop_server_mutex);

        // Accept the data packet from client
        connfd = accept(sockfd, (struct sockaddr *) &cli, &len);
        if (connfd < 0) { 
            g_print("server accept failed...\n"); 
            exit(0); 
        }

        g_print("accepting connection success\n");

        guchar msg[MAX_PACKET_LEN] = {0,};
        guint msg_len = _read_message(connfd, msg);
        g_print("read message success\n");

        // Trigger application callback with message.
        info->msg_cb(msg, msg_len);

        close(connfd);
        g_mutex_lock (&g_stop_server_mutex);
        g_usleep (1000); /* waiting for 1ms before checking for user prompt again */
    }
    g_mutex_unlock(&g_stop_server_mutex);

    close(sockfd);
    free(info);
    g_mutex_clear(&g_stop_server_mutex);
    return NULL;
}

void start_usr_prompt_monitor(user_prompt_callback cb)
{
    g_stop_server = FALSE;
    /* mutex to stop monitoring from a different thread. */
    g_mutex_init (&g_stop_server_mutex);
    
#ifdef DS_APP_USR_PROMPT_MONITOR_PORT
    g_server_port = DS_APP_USR_PROMPT_MONITOR_PORT;
#else
    g_server_port = DEFAULT_MONITOR_PORT;
#endif

    struct usr_prompt_info *info = (struct usr_prompt_info *) malloc(sizeof(struct usr_prompt_info));
    info->msg_cb = cb;

    /* Start the monitor task. */
    g_monitor_thread = g_thread_new ("DS app user prompt monitor thread", _server_task, (gpointer) info);
}

void stop_usr_prompt_monitor(void)
{
    g_mutex_lock(&g_stop_server_mutex);
    g_stop_server = TRUE;
    g_mutex_unlock(&g_stop_server_mutex);

    g_thread_unref (g_monitor_thread);
}
