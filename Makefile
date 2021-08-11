################################################################################
# Copyright (c) 2020-2021, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
################################################################################

CUDA_VER?=
ifeq ($(CUDA_VER),)
  $(error "CUDA_VER is not set")
endif

APP:= deepstream-fpfilter-app
USER_PROMPT_APP:=ds-fpfilter-manager

TARGET_DEVICE = $(shell gcc -dumpmachine | cut -f1 -d -)

NVDS_VERSION:=5.1

LIB_INSTALL_DIR?=/opt/nvidia/deepstream/deepstream-$(NVDS_VERSION)/lib/
APP_INSTALL_DIR?=/opt/nvidia/deepstream/deepstream-$(NVDS_VERSION)/bin/

ifeq ($(TARGET_DEVICE),aarch64)
  CFLAGS:= -DPLATFORM_TEGRA
endif

SRCS:=src/deepstream_fpfilter_app.c src/ds_usr_prompt_handler.c src/ds_dynamic_link_unlink_element.c src/ds_save_frame.c
USER_PROMPT_SRCS:=src/ds_fpfilter_manager.c

INCS:= $(wildcard include/*.h)

PKGS:= gstreamer-1.0 json-glib-1.0

OBJS:= $(SRCS:.c=.o)
USER_PROMPT_OBJS:= $(USER_PROMPT_SRCS:.c=.o)

CFLAGS+= -DMP4_SRC
#CFLAGS+= -DH264_ELEMENTARY_SRC
#CFLAGS+= -DMULTI_FILE_SRC

CFLAGS+= -DFILE_SINK
#CFLAGS+= -DVIDEO_RENDER_SINK

CFLAGS += -I./include
CFLAGS+= -I/opt/nvidia/deepstream/deepstream-5.1/sources/includes \
				-I /usr/local/cuda-$(CUDA_VER)/include

CFLAGS+= $(shell pkg-config --cflags $(PKGS))

LIBS:= $(shell pkg-config --libs $(PKGS))

LIBS+= -L/usr/local/cuda-$(CUDA_VER)/lib64/ -lcudart \
				-L$(LIB_INSTALL_DIR) -lnvdsgst_meta -lnvds_meta \
				-lcuda -Wl,-rpath,$(LIB_INSTALL_DIR)

all: $(APP) $(USER_PROMPT_APP)

%.o: %.c $(INCS) Makefile
	$(CC) -c -o $@ $(CFLAGS) $<

$(APP): $(OBJS) Makefile
	$(CC) -o $(APP) $(OBJS) $(LIBS)

$(USER_PROMPT_APP): $(USER_PROMPT_OBJS) Makefile
	$(CC) -o $(USER_PROMPT_APP) $(USER_PROMPT_OBJS) $(LIBS)

install: $(APP)
	cp -rv $(APP) $(APP_INSTALL_DIR)

clean:
	rm -rf $(OBJS) $(APP) $(USER_PROMPT_APP) $(USER_PROMPT_OBJS)
