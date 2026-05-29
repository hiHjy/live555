# live555 RTSP Push Module

这个目录是板子调试用的 aarch64 RTSP 推流模块工程。

本模块只负责：

```text
标准 Annex-B H264
  -> live555 队列
  -> RTSP/RTP 推流
```

不负责摄像头采集、MJPEG 解码、MPP 编码。上层框架只需要在编码器输出完整 H264 Annex-B 数据后调用 C 接口 push。

## 目录

```text
src/      RTSP 模块源码
include/ C 接口头文件和 live555 头文件
lib/     aarch64 live555 静态库和本模块静态库
build/   编译中间文件
docs/    阶段文档和数据流记录
```

## 编译

在 Ubuntu 主机上执行：

```bash
cd ~/nfs/live
./build.sh
```

输出：

```text
librtsp_push.a
```

## 对外 C 接口

公共头文件：

```c
#include "live555_rtsp_push.h"
```

典型调用：

```c
live555_rtsp_config_t cfg;
live555_rtsp_config_init(&cfg);
cfg.rtsp_port = 8554;
cfg.stream_name = "live";

live555_rtsp_start(&cfg);

// 编码器每输出一帧完整 Annex-B H264 后调用：
live555_rtsp_push_h264_annexb(data, size, timestamp_us);

live555_rtsp_stop();
```

详细说明见：

```text
docs/RTSP_PUSH_C_API.md
```

## 当前链路边界

```text
上层编码器
  -> 完整 Annex-B H264
  -> H264FrameQueue
  -> MyH264Source
  -> H264VideoRTPSink
  -> VLC
```
