# live555 RTSP 推流模块 C 接口说明

这份文档给上层项目框架使用。

当前模块只负责 RTSP/RTP 推流，不负责摄像头、解码器、编码器。

## 1. 模块边界

上层需要负责：

```text
摄像头采集
  -> 图像预处理
  -> H264 编码
  -> 输出完整 Annex-B H264 数据
```

RTSP 模块负责：

```text
完整 Annex-B H264
  -> 拆成纯 NALU
  -> live555 FramedSource
  -> H264VideoRTPSink
  -> RTSP/RTP 推流
```

也就是说，上层编码器每输出一帧完整 H264 数据后，直接调用：

```c
live555_rtsp_push_h264_annexb(data, size, timestamp_us);
```

## 2. 输入数据要求

`live555_rtsp_push_h264_annexb()` 的输入必须是标准 Annex-B H264。

也就是 NALU 前面带起始码：

```text
00 00 01
```

或：

```text
00 00 00 01
```

典型输入可能是：

```text
00 00 00 01 SPS
00 00 00 01 PPS
00 00 00 01 IDR
```

也可能是：

```text
00 00 00 01 P-frame
```

模块内部会自动去掉起始码，拆成 live555 `H264VideoStreamDiscreteFramer` 需要的纯 NALU。

上层不要提前去掉起始码。

## 3. 公共头文件

```c
#include "live555_rtsp_push.h"
```

头文件路径：

```text
include/live555_rtsp_push.h
```

## 4. 对外接口

### 初始化默认配置

```c
void live555_rtsp_config_init(live555_rtsp_config_t *config);
```

默认值：

```text
rtsp_port  = 8554
stream_name = "live"
username = NULL
password = NULL
```

### 启动 RTSP 服务

```c
int live555_rtsp_start(const live555_rtsp_config_t *config);
```

返回值：

```text
0   成功
<0  失败
```

如果传 `NULL`，使用默认配置：

```c
live555_rtsp_start(NULL);
```

### 推送 H264 Annex-B 数据

```c
int live555_rtsp_push_h264_annexb(const uint8_t *data,
                                  size_t size,
                                  uint64_t timestamp_us);
```

参数：

```text
data:
  一帧或一组完整 Annex-B H264 数据。

size:
  data 字节长度。

timestamp_us:
  时间戳，单位微秒。
  建议使用编码器 PTS 或摄像头 monotonic timestamp。
```

返回值：

```text
0   成功入队
-1  data 为空或 size 为 0
-2  RTSP 服务未启动
```

注意：

```text
这个函数会拷贝 data 内容。
调用返回后，上层可以释放或复用 data 指向的编码器输出 buffer。
```

### 查询运行状态

```c
int live555_rtsp_is_running(void);
```

返回值：

```text
1  正在运行
0  未运行
```

### 获取 RTSP URL

```c
const char *live555_rtsp_get_url(void);
```

返回示例：

```text
rtsp://192.168.1.56:8554/live
```

如果服务尚未准备好，可能返回 `NULL`。

返回的字符串由模块内部维护，上层不要 free。

### 停止 RTSP 服务

```c
void live555_rtsp_stop(void);
```

停止后会关闭 live555 事件线程，并清空内部 H264 队列。

## 5. 上层典型调用方式

```c
#include "live555_rtsp_push.h"

int start_rtsp(void)
{
    live555_rtsp_config_t cfg;
    live555_rtsp_config_init(&cfg);

    cfg.rtsp_port = 8554;
    cfg.stream_name = "live";

    // 如需简单鉴权：
    // cfg.username = "admin";
    // cfg.password = "123456";

    return live555_rtsp_start(&cfg);
}

void on_h264_encoded(const uint8_t *data,
                     size_t size,
                     uint64_t pts_us)
{
    /*
     * data 必须是完整 Annex-B H264。
     * 例如 MPP 编码器输出的 00 00 00 01 + NALU。
     */
    int ret = live555_rtsp_push_h264_annexb(data, size, pts_us);
    if (ret != 0) {
        // 上层按需打日志即可。
    }
}

void stop_rtsp(void)
{
    live555_rtsp_stop();
}
```

## 6. 和上层编码器的关系

上层编码器只需要保证：

```text
1. 输出 H264 Annex-B。
2. 每次 push 的 data/size 是完整的一帧或完整的一组 NALU。
3. 关键帧前最好带 SPS/PPS。
4. timestamp_us 单调递增。
```

当前模块不关心：

```text
摄像头怎么采集
MPP 怎么配置
码率怎么控制
IDR 怎么触发
图像格式怎么转换
```

这些都由上层项目已有编码链路负责。

## 7. 新客户端中途加入

当前模块会把编码器输出的 H264 原样按队列送给 live555。

建议上层编码器保持：

```text
每个 IDR 前带 SPS/PPS
```

这样新客户端中途加入时，最多等到下一个 IDR，就可以正常解码。

后续如果需要更强的工程能力，可以继续增加：

```text
新客户端接入时强制 IDR
新客户端等待 IDR 后再发流
SPS/PPS 缓存和补发
按帧/GOP 丢队列
```

这些属于后续加固项，不影响当前接口接入。

## 8. 编译和链接

当前工程编译输出：

```text
lib/librtsp_push.a
```

链接时需要同时链接本模块和 live555 静态库：

```text
lib/librtsp_push.a
lib/libliveMedia.a
lib/libgroupsock.a
lib/libBasicUsageEnvironment.a
lib/libUsageEnvironment.a
```

同时需要系统库：

```text
pthread
dl
```

典型链接顺序：

```bash
aarch64-linux-gnu-g++ ... \
  your_objects.o \
  lib/librtsp_push.a \
  lib/libliveMedia.a \
  lib/libgroupsock.a \
  lib/libBasicUsageEnvironment.a \
  lib/libUsageEnvironment.a \
  -lpthread -ldl
```

如果最终工程是 C 项目，也建议最后用 `g++` 链接，因为本模块内部是 C++ 实现。

## 9. 当前模块内部数据流

```text
live555_rtsp_push_h264_annexb()
  -> Live555RtspService::pushH264Frame()
  -> H264FrameQueue::pushAnnexBFrame()
  -> splitAnnexB()
  -> queue 纯 NALU
  -> triggerEvent()
  -> MyH264Source::deliverFrame()
  -> H264VideoStreamDiscreteFramer
  -> H264VideoRTPSink
```

上层只需要理解：

```text
start 一次
编码器每出一帧就 push 一次
退出时 stop 一次
```
