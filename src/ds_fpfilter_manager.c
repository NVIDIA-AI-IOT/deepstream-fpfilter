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

#include <unistd.h>
#include <stdio.h>
#include "glib.h"
#include <sys/socket.h>
#include <netinet/ip.h>

/**
 * @brief   Implements simple client application to send messages to server hosted by DS app.
 */

#define DEFAULT_MONITOR_PORT     43434
#define MAX_PACKET_LEN          (4 * 1024)

//send data to server.
static void _send_buf_to_ds_app(const guchar *buffer, guint buffer_len)
{
    guint sock = 0; 
    struct sockaddr_in serv_addr; 

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    { 
        g_print("Socket creation error \n"); 
        return;
    }

    serv_addr.sin_family = AF_INET; 
    serv_addr.sin_port = htons(DEFAULT_MONITOR_PORT);
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) 
    {
        g_print("Connection Failed \n");
        return;
    }

    guchar write_buffer[MAX_PACKET_LEN] = {0,};
    guint write_buffer_len = 0;

    guint16 len = (guint16) buffer_len;
    memcpy(write_buffer, &len, 2);
    write_buffer_len += 2;

    memcpy(write_buffer + write_buffer_len, buffer, buffer_len);
    write_buffer_len += buffer_len;

    int sent_len = 0;
    while (sent_len != write_buffer_len)
    {
        sent_len += write(sock, write_buffer + sent_len, write_buffer_len - sent_len);
    }

    close(sock); //close socket
}

int
main(int argc, char *argv[])
{
    if (argc != 3)
    {
        g_print("usage: %s -m <message>\n", argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "-m") != 0)
    {
        g_print("usage: %s -m <message>\n", argv[0]);
        return 0;
    }

    FILE *file = NULL;
    file = fopen (argv[2], "r");
    if (!file)
    {
        g_print("file open failed\n");
        return 0;
    }

    guchar buffer[MAX_PACKET_LEN] = {0,};
    size_t len = fread(buffer, 1, MAX_PACKET_LEN - 1, file);
    _send_buf_to_ds_app(buffer, len);
    return 0;
}
