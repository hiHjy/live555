# RTSP camera + MPP 测试记录

## 目标

把摄像头 V4L2 采集、Rockchip MPP H264 编码、live555 RTSP 服务串起来：

```text
V4L2 DQBUF
    |
    | dma-buf fd + buf.timestamp
    v
cameraFrameCallback()
    |
    | rk_mpp_encoder_send_frame(fd, timestamp_us, eos)
    v
Rockchip MPP encoder
    |
    | H264 Annex-B packet + timestamp_us
    v
encoderPacketCallback()
    |
    | Live555RtspService::pushH264Frame(data, size, timestamp_us)
    v
H264FrameQueue
    |
    | strip start code / split NALU / queue
    v
MyH264Source -> H264VideoRTPSink -> RTSP client
```

## 改动点

复制了 WebRTC 测试工程里的模块到当前目录，避免直接改原工程：

```text
/home/hjy/live/test/cam/cam.c
/home/hjy/live/test/cam/cam.h
/home/hjy/live/test/encoder/rkmpp_enc.c
/home/hjy/live/test/encoder/rkmpp_enc.h
```

新增主程序：

```text
/home/hjy/live/test/rtsp_camera_test.cpp
```

新增 aarch64 编译文件：

```text
/home/hjy/live/test/Makefile.aarch64
```

## 时间戳链路

在 V4L2 出队后取：

```c
buf.timestamp
```

转换为微秒：

```c
timestamp_us = buf.timestamp.tv_sec * 1000000 + buf.timestamp.tv_usec;
```

然后一路传给：

```text
cam callback
rk_mpp_encoder_send_frame()
mpp_frame_set_pts()
MPP encoded packet
encoder packet callback
Live555RtspService::pushH264Frame()
```

## 编译

live555 当前已切到 aarch64/no-openssl 配置：

```bash
cd /home/hjy/live
./genMakefiles linux-aarch64-rk
make -j$(nproc)
```

编译 RTSP 摄像头测试：

```bash
cd /home/hjy/live/test
make -f Makefile.aarch64
```

输出：

```text
/home/hjy/live/test/build-aarch64-rtsp/rtsp_camera_test_aarch64
/home/hjy/nfs/rtsp_camera_test_aarch64
```

## 板子运行

在板子上运行：

```bash
./rtsp_camera_test_aarch64
```

程序会启动 RTSP 服务，URL 会在终端打印，通常是：

```text
rtsp://<板子IP>:8554/live
```

VLC 拉流：

```text
rtsp://<板子IP>:8554/live
```

## 注意

当前 live555 静态库已经被编成 aarch64。如果后面要在 Ubuntu x86 上继续编原来的 `service_test`，需要切回 x86：

```bash
cd /home/hjy/live
./genMakefiles linux-64bit
make clean
make -j$(nproc)
```
