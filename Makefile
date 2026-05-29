SYSROOT ?= $(HOME)/rk3568_sysroot_fixed
CROSS_COMPILE ?= aarch64-linux-gnu-

CC := $(CROSS_COMPILE)gcc
CXX := $(CROSS_COMPILE)g++
AR := $(CROSS_COMPILE)ar

PROJECT_ROOT := $(CURDIR)
SRC_DIR := $(PROJECT_ROOT)/src
BUILD_DIR := $(PROJECT_ROOT)/build/aarch64
TARGET := $(PROJECT_ROOT)/lib/librtsp_push.a

COMMON_DEFS := -DSOCKLEN_T=socklen_t -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64 -D_DEFAULT_SOURCE -DNO_OPENSSL=1
COMMON_INCLUDES := \
	-I$(PROJECT_ROOT)/include \
	-I$(SRC_DIR) \
	-I$(PROJECT_ROOT)/include/BasicUsageEnvironment \
	-I$(PROJECT_ROOT)/include/UsageEnvironment \
	-I$(PROJECT_ROOT)/include/groupsock \
	-I$(PROJECT_ROOT)/include/liveMedia \
	-I$(SYSROOT)/usr/include

CFLAGS := --sysroot=$(SYSROOT) -Wall -O2 -std=c11 $(COMMON_DEFS) $(COMMON_INCLUDES)
CXXFLAGS := --sysroot=$(SYSROOT) -Wall -O2 -std=c++2a -pthread $(COMMON_DEFS) $(COMMON_INCLUDES)

LIVE_LIBS := \
	$(PROJECT_ROOT)/lib/libliveMedia.a \
	$(PROJECT_ROOT)/lib/libgroupsock.a \
	$(PROJECT_ROOT)/lib/libBasicUsageEnvironment.a \
	$(PROJECT_ROOT)/lib/libUsageEnvironment.a

CPP_SRCS := \
	live555RtspService.cpp \
	myH264Source.cpp \
	myH264Subsession.cpp \
	h264FrameQueue.cpp \
	live555_rtsp_c_api.cpp

OBJS := $(CPP_SRCS:%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) $(LIVE_LIBS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(OBJS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
