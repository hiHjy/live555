# RTSP Camera Demo 总体逻辑

这个 demo 当前验证的是一条完整链路：

```text
V4L2 摄像头 MJPEG
  -> MPP MJPEG 解码成 NV12
  -> MPP H264 编码
  -> Live555 队列缓存 H264 NALU
  -> Live555 RTSP/RTP 推流
  -> VLC/客户端播放
```

当前摄像头只输出 MJPEG，所以不能直接送 H264 编码器。必须先经过 MJPEG 解码器，把压缩 JPEG 图像解成 NV12 原始图像，再送 H264 编码器。

## 1. 程序启动

入口文件：

- `rtsp_camera_test.cpp`

主流程在 `main()`：

```cpp
Live555RtspService::getInstance().start();
camera_init();
decoder_init(&ctx.decoder, MPP_VIDEO_CodingMJPEG);
decoder_register_frame_callback(&ctx.decoder, decodedFrameCallback, &ctx);
cam_register_frame_callback(cameraFrameCallback, &ctx);
std::thread cameraThread(camera_run);
```

含义：

- 先启动 RTSP 服务线程。
- 初始化 V4L2 摄像头。
- 初始化 MPP MJPEG 解码器。
- 注册“解码完成回调”。
- 注册“摄像头出帧回调”。
- 单独开一个摄像头采集线程。

## 2. RTSP 服务启动

相关文件：

- `live555RtspService.hh`
- `live555RtspService.cpp`

启动入口：

```cpp
Live555RtspService::start()
```

它会创建一个线程：

```cpp
rtspThread_ = std::thread(&Live555RtspService::rtspThreadMain, this);
```

RTSP 线程里执行：

```cpp
setupLive555();
env_->taskScheduler().doEventLoop(&eventLoopWatchVariable_);
cleanupLive555();
```

`setupLive555()` 里创建：

- `BasicTaskScheduler`
- `BasicUsageEnvironment`
- `RTSPServer`
- `ServerMediaSession`
- `MyH264Subsession`

关键代码：

```cpp
server_ = RTSPServer::createNew(*env_, rtspPort_, authDB_);

ServerMediaSession* sms = ServerMediaSession::createNew(
    *env_,
    streamName_.c_str(),
    streamName_.c_str(),
    "Live555 RTSP Service H264 Stream");

sms->addSubsession(MyH264Subsession::createNew(*env_, frameQueue_));
server_->addServerMediaSession(sms);
```

这里的 `frameQueue_` 是整个 RTSP 服务和编码器之间的桥。

## 3. 摄像头采集

相关文件：

- `cam/cam.c`
- `cam/cam.h`

采集线程入口：

```cpp
camera_run();
```

内部调用：

```cpp
run();
```

`run()` 的核心循环：

```text
poll 等摄像头有数据
  -> VIDIOC_DQBUF 取出一帧
  -> 调用 g_frame_callback()
  -> VIDIOC_QBUF 把 buffer 还给驱动
```

关键回调位置：

```cpp
g_frame_callback(dmafd[buf.index],
                 buf.index,
                 width,
                 height,
                 bytesperline,
                 buf.bytesused,
                 timestamp_us,
                 g_frame_callback_userdata);
```

这里传出去的核心参数：

- `dmafd[buf.index]`：当前 MJPEG 帧所在的 dma-buf fd。
- `buf.index`：V4L2 buffer 槽位号。
- `buf.bytesused`：这一帧 MJPEG 的真实长度。
- `timestamp_us`：摄像头给出的时间戳。

想看摄像头有没有持续出帧，可以在 `cam/cam.c` 的 `run()` 里 `DQBUF` 成功后加日志。

## 4. 摄像头帧进入 MJPEG 解码器

相关函数：

```cpp
cameraFrameCallback(...)
```

位置：

- `rtsp_camera_test.cpp`

这个回调收到的是摄像头的一帧 MJPEG。

核心逻辑：

```cpp
ctx->timestampsUs[index] = timestampUs;

decoder_do_task_advanced(&ctx->decoder,
                         fd,
                         index,
                         w,
                         h,
                         stride,
                         static_cast<int>(size));
```

这里做两件事：

- 保存当前 `index` 对应的时间戳。
- 把 MJPEG dmafd 送进 MPP 解码器。

`index` 很重要，因为解码器输出后还会带回同一个 index，后面靠它找回原始时间戳。

## 5. MJPEG 解码成 NV12

相关文件：

- `decoder_mjpeg.c`
- `decoder_mjpeg.h`

核心函数：

```cpp
decoder_do_task_advanced(...)
```

这一步把：

```text
MJPEG 压缩数据 dmafd
```

解码成：

```text
NV12 原始图像 dmafd
```

核心阶段：

```text
1. mmap 输入 dmafd
2. 从 JPEG 头解析 width / height
3. 准备输出 dma-buf
4. 输入 fd import 成 MppBuffer / MppPacket
5. 输出 fd import 成 MppBuffer / MppFrame
6. MPP task 模式提交硬件解码
7. 解码完成后调用 frame_callback
```

解码完成后，会调用之前注册的：

```cpp
decodedFrameCallback(...)
```

想看解码是否成功，可以在 `decoder_do_task_advanced()` 里打印：

- 输入 fd
- 输入 size
- 解析出的 width / height
- 输出 fd
- 输出 frame format
- err / discard 标记

## 6. 解码完成后送 H264 编码器

相关函数：

```cpp
decodedFrameCallback(...)
```

位置：

- `rtsp_camera_test.cpp`

第一次收到 NV12 帧时，初始化 H264 编码器：

```cpp
rk_mpp_encoder_init(&ctx->encoder,
                    MPP_VIDEO_CodingAVC,
                    width,
                    height,
                    horStride,
                    verStride,
                    MPP_FMT_YUV420SP,
                    ctx->videoFps,
                    0,
                    ctx->videoFps,
                    nullptr);
```

然后注册编码器输出回调：

```cpp
rk_mpp_encoder_set_packet_callback(&ctx->encoder, encoderPacketCallback, ctx);
```

之后每一帧都送编码器：

```cpp
rk_mpp_encoder_send_frame(&ctx->encoder, fd, timestampUs, 0);
```

这里的 `fd` 已经不是摄像头 MJPEG fd，而是解码后的 NV12 dmafd。

## 7. H264 编码器输出码流

相关文件：

- `encoder/rkmpp_enc.c`
- `encoder/rkmpp_enc.h`

核心函数：

```cpp
rk_mpp_encoder_send_frame(...)
```

它把：

```text
NV12 dmafd
```

编码成：

```text
Annex-B H264 packet
```

核心步骤：

```text
1. 外部 dmafd import 成 MppBuffer
2. 创建 MppFrame
3. 设置 width / height / stride / format / pts
4. 把 MppBuffer 绑定到 MppFrame
5. 创建输出 MppPacket
6. encode_put_frame()
7. encode_get_packet()
8. 调用 packet_callback
```

编码器输出回调：

```cpp
encoderPacketCallback(...)
```

位置：

- `rtsp_camera_test.cpp`

它把编码后的 H264 推给 RTSP 服务：

```cpp
Live555RtspService::getInstance().pushH264Frame(data, size, timestampUs);
```

想看编码器是否持续输出，可以在 `encoderPacketCallback()` 里打印：

- `size`
- `timestampUs`
- `isHeader`
- `eos`
- NALU 起始几个字节

## 8. H264 进入 Live555 队列

相关文件：

- `h264FrameQueue.hh`
- `h264FrameQueue.cpp`

入口：

```cpp
Live555RtspService::pushH264Frame(...)
```

内部调用：

```cpp
frameQueue_->pushAnnexBFrame(data, size, timestampUs);
```

`pushAnnexBFrame()` 做的事情：

```text
1. 判空
2. timestampUs 转 timeval
3. splitAnnexB() 按 00 00 01 / 00 00 00 01 拆 NALU
4. 去掉起始码，只保存纯 NALU
5. push 到 queue_
6. wakeReadersLocked() 唤醒 live555 source
```

关键点：

```cpp
pushPureNaluLocked(data + nal.offset, nal.size, presentationTime);
```

这里进入队列的已经是纯 NALU，不带 `00 00 00 01` 起始码。

因为 `H264VideoStreamDiscreteFramer` 要求输入是完整离散 NALU，不能带 Annex-B start code。

## 9. 队列唤醒 MyH264Source

相关函数：

```cpp
registerReader(...)
wakeReadersLocked()
```

`MyH264Source` 构造时会创建 live555 事件：

```cpp
frameArrivedTrigger_ = envir().taskScheduler().createEventTrigger(frameArrivedCallback);
```

然后把自己注册到队列：

```cpp
frameQueue_->registerReader(&envir().taskScheduler(), frameArrivedTrigger_, this);
```

队列里有新 NALU 后调用：

```cpp
reader.scheduler->triggerEvent(reader.triggerId, reader.clientData);
```

意思是：

```text
告诉 live555 事件循环：
  有新帧到了，请回调 frameArrivedCallback(this)
```

然后进入：

```cpp
MyH264Source::frameArrivedCallback(...)
```

再调用：

```cpp
deliverFrame();
```

## 10. MyH264Source 交帧给 live555

相关文件：

- `myH264Source.hh`
- `myH264Source.cpp`

核心函数：

```cpp
MyH264Source::deliverFrame()
```

它做的事情：

```text
1. 判断 live555 当前是否正在等数据
2. 从 H264FrameQueue 取一个 NALU
3. 拷贝到 live555 给的 fTo 缓冲区
4. 设置 fFrameSize
5. 设置 fPresentationTime
6. 调用 FramedSource::afterGetting(this)
```

关键代码：

```cpp
if (!isCurrentlyAwaitingData()) {
    return;
}

H264FrameQueue::Nalu nalu;
if (!frameQueue_->popNalu(nalu)) {
    return;
}

std::memcpy(fTo, nalu.data.data(), fFrameSize);
fPresentationTime = nalu.presentationTime;

FramedSource::afterGetting(this);
```

`afterGetting(this)` 的含义：

```text
我这次 getNextFrame() 要的数据已经准备好了，
你 live555 可以继续往 RTP sink 走了。
```

之前出现过：

```text
attempting to read more than once at the same time
```

就是因为交帧节奏不对。现在改成直接 `afterGetting(this)`，避免重复挂 0 延迟任务。

## 11. Subsession 接到 RTP Sink

相关文件：

- `myH264Subsession.hh`
- `myH264Subsession.cpp`

客户端连接 RTSP 后，live555 会调用：

```cpp
createNewStreamSource(...)
```

这里创建你的数据源：

```cpp
FramedSource *source = MyH264Source::createNew(envir(), frameQueue_);
```

再包一层 H264 离散帧 framer：

```cpp
return H264VideoStreamDiscreteFramer::createNew(envir(), source);
```

然后创建 RTP sink：

```cpp
return H264VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
```

`H264VideoRTPSink` 负责：

- RTP payload type
- H264 RTP payload 格式
- 大 NALU 的 FU-A 分片
- SDP 里的 H264 描述
- 通过 RTP 发给客户端

## 12. 最重要的线程关系

当前 demo 至少有两条主线程逻辑：

```text
主线程
  -> 启动服务
  -> 启动摄像头线程
  -> 等待退出信号

摄像头线程
  -> V4L2 DQBUF
  -> MJPEG 解码
  -> H264 编码
  -> pushH264Frame()

live555 线程
  -> doEventLoop()
  -> RTSP 连接处理
  -> 收到 triggerEvent 后调用 MyH264Source
  -> RTP 发包
```

两个线程之间通过 `H264FrameQueue` 交互：

```text
编码线程 push NALU
live555 线程 pop NALU
```

所以 `H264FrameQueue` 内部用 `std::mutex` 保护队列。

## 13. 当前 demo 的核心结论

当前 demo 已经验证了：

```text
V4L2 MJPEG 摄像头
  -> MPP advance 模式 MJPEG 解码
  -> MPP H264 编码
  -> Live555 RTSP 推流
```

你现在已经知道调试点应该加在哪里：

- 摄像头是否出帧：`cam/cam.c` 的 `run()`
- MJPEG 是否送进解码器：`cameraFrameCallback()`
- 解码是否成功：`decoder_do_task_advanced()`
- NV12 是否送进编码器：`decodedFrameCallback()`
- H264 是否输出：`encoderPacketCallback()`
- H264 是否入队：`H264FrameQueue::pushAnnexBFrame()`
- live555 是否取帧：`MyH264Source::deliverFrame()`
- RTSP source/sink 是否创建：`MyH264Subsession`

## 14. 后续工程加固项

这些不是当前核心逻辑，后面真实项目再补：

- 新客户端中途加入时等待 IDR。
- 必要时强制编码器出 IDR。
- 新客户端同步时补 SPS/PPS。
- 队列按“帧”管理，而不只是按 NALU 管理。
- 队列满时优先丢旧帧，并尽量丢到下一个 IDR。
- 清理过多调试打印，避免影响性能。
- 完善错误恢复，例如摄像头断开、MPP 解码失败、RTSP 端口占用。

当前阶段重点是：主链路已经跑通，并且你已经能看懂每一段数据怎么流动。
