# live555 push H264 数据流记录

当前阶段已经从“RTSP 服务内部读 H264 文件”改成了“外部输入 Annex-B H264 frame，然后由 live555 发 RTP”。

## 总体数据流

```text
MPP 编码线程 / 测试文件线程
    |
    | pushH264Frame(data, size, timestampUs)
    v
Live555RtspService
    |
    | frameQueue_->pushAnnexBFrame(...)
    v
H264FrameQueue
    |
    | 解析 Annex-B 起始码
    | 去掉 00 00 01 / 00 00 00 01
    | 拆成纯 NALU
    | 缓存 SPS/PPS
    | push 到 std::deque 队列
    | triggerEvent() 唤醒 live555
    v
MyH264Source
    |
    | popNalu()
    | memcpy 到 fTo
    | 设置 fFrameSize / fPresentationTime
    | afterGetting()
    v
H264VideoStreamDiscreteFramer
    |
    v
H264VideoRTPSink
    |
    | RTP 打包 / 大 NALU 分片 FU-A
    v
客户端 VLC / ONVIF 客户端
```

## 启动服务

外部先调用：

```cpp
Live555RtspService::getInstance().start();
```

内部会开一个 RTSP 线程，然后创建：

```text
BasicTaskScheduler
BasicUsageEnvironment
RTSPServer
ServerMediaSession
MyH264Subsession
```

最后进入 live555 事件循环：

```cpp
env_->taskScheduler().doEventLoop(&eventLoopWatchVariable_);
```

这个线程后面专门负责 live555 的 RTSP/RTP 事件处理。

## 客户端连接

客户端连接：

```text
rtsp://ip:8554/live
```

live555 会调用：

```cpp
MyH264Subsession::createNewStreamSource()
```

这里创建：

```cpp
MyH264Source
```

然后外面再包一层：

```cpp
H264VideoStreamDiscreteFramer
```

含义是：

```text
MyH264Source 输出一个个完整的纯 NALU
H264VideoStreamDiscreteFramer 告诉 live555 这是离散 H264 NALU
H264VideoRTPSink 负责把 NALU 打成 RTP 包
```

## 外部 push H264

MPP 编码出一帧后调用：

```cpp
Live555RtspService::getInstance().pushH264Frame(data, size, timestampUs);
```

这里的 `data` 可以是 Annex-B 格式，例如：

```text
00 00 00 01 SPS
00 00 00 01 PPS
00 00 00 01 IDR
```

或者普通 P 帧：

```text
00 00 00 01 P-frame NALU
```

进入队列后，`H264FrameQueue::pushAnnexBFrame()` 会：

```text
1. 找起始码
2. 拆出 NALU
3. 去掉起始码
4. 识别 nalType
5. 第一次遇到 SPS/PPS 时缓存一份
6. 把纯 NALU 放进 std::deque
7. triggerEvent() 唤醒 live555 source
```

H264 常见 nalType：

```text
7 = SPS
8 = PPS
5 = IDR 关键帧
1 = 普通 P/B 帧
```

## live555 取数据

live555 需要下一帧数据时，会调用：

```cpp
MyH264Source::doGetNextFrame()
```

里面继续调用：

```cpp
deliverFrame()
```

然后从队列取一个 NALU：

```cpp
frameQueue_->popNalu(nalu);
```

如果队列没数据，就直接返回，不阻塞。

这是很重要的：live555 的事件循环线程不能用 `cv.wait()` 卡死，否则 RTSP 请求、RTP 发送、客户端断开等事件都会被堵住。

如果队列有数据，就填 live555 给的缓冲区：

```cpp
memcpy(fTo, nalu.data.data(), fFrameSize);
fPresentationTime = nalu.presentationTime;
```

然后调用：

```cpp
afterGetting(this);
```

它的意思是告诉 live555：

```text
我已经把一帧/NALU 数据准备好了，你可以继续拿去 RTP 打包和发送了。
```

## RTP 发送

后面交给：

```cpp
H264VideoRTPSink
```

它负责：

```text
生成 RTP header
设置 RTP timestamp
按 H264 RTP payload 格式封装
大 NALU 自动 FU-A 分片
通过 UDP/TCP 发给客户端
```

所以当前项目代码里不需要自己手写 RTP 包。

## 当前真实项目关注点

后面接 MPP 时，重点是：

```text
1. MPP 输出标准 Annex-B H264
2. timestampUs 单调递增，单位是微秒
3. 编码完成后持续调用 pushH264Frame(data, size, timestampUs)
4. 不要在 live555 事件循环线程里阻塞等待
```

当前 `service_test.cpp` 只是模拟 MPP：它从文件读 Annex-B 数据，然后调用 `pushH264Frame()`。真实项目里把这个测试喂数据线程换成 MPP 编码回调即可。
