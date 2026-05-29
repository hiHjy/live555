# RTSP 推流模块接入说明

## 1. 模块定位

本模块基于 live555 封装了一个 RTSP H264 推流服务，对上层提供 C 接口。

上层项目只需要在 H264 编码完成后，把编码器输出的完整 Annex-B H264 数据推给本模块即可。

本模块负责：

```text
Annex-B H264 输入
  -> 拆分 H264 NALU
  -> live555 RTSP 服务
  -> RTP H264 打包
  -> 客户端通过 RTSP 拉流
```

本模块不负责：

```text
摄像头采集
图像格式转换
H264 编码
码率控制
IDR 触发策略
```

这些仍由上层现有视频编码链路负责。

## 2. 对外文件

公共头文件：

```text
include/live555_rtsp_push.h
```

静态库：

```text
lib/librtsp_push.a
```

依赖的 live555 静态库：

```text
lib/libliveMedia.a
lib/libgroupsock.a
lib/libBasicUsageEnvironment.a
lib/libUsageEnvironment.a
```

## 3. 输入数据要求

推送接口要求输入为标准 Annex-B H264 码流。

也就是 NALU 前带起始码：

```text
00 00 01
```

或：

```text
00 00 00 01
```

示例：

```text
00 00 00 01 SPS
00 00 00 01 PPS
00 00 00 01 IDR
```

或：

```text
00 00 00 01 P-frame
```

上层不需要去掉起始码，模块内部会自动拆分并转换成 live555 需要的纯 NALU。

建议编码器配置为：

```text
每个 IDR 前携带 SPS/PPS
```

这样客户端中途接入时，最多等待下一个 IDR 即可正常解码。

## 4. 接口调用顺序

典型调用顺序：

```text
1. live555_rtsp_config_init()
2. live555_rtsp_start()
3. 编码器每输出一帧 H264，调用 live555_rtsp_push_h264_annexb()
4. 程序退出或停止推流时调用 live555_rtsp_stop()
```

## 5. 示例代码

```c
#include "live555_rtsp_push.h"

int rtsp_module_start(void)
{
    live555_rtsp_config_t cfg;

    live555_rtsp_config_init(&cfg);
    cfg.rtsp_port = 8554;
    cfg.stream_name = "live";

    /*
     * 如需 RTSP 鉴权，可设置：
     *
     * cfg.username = "admin";
     * cfg.password = "123456";
     */

    return live555_rtsp_start(&cfg);
}

void on_h264_encoded(const uint8_t *data,
                     size_t size,
                     uint64_t pts_us)
{
    /*
     * data:
     *   编码器输出的完整 Annex-B H264 数据。
     *
     * pts_us:
     *   时间戳，单位微秒。
     *   建议使用编码器 PTS 或摄像头 monotonic timestamp。
     */
    int ret = live555_rtsp_push_h264_annexb(data, size, pts_us);
    if (ret != 0) {
        /*
         * ret < 0 表示推送失败。
         * 上层可以按需打日志或统计丢帧。
         */
    }
}

void rtsp_module_stop(void)
{
    live555_rtsp_stop();
}
```

## 6. 接口说明

### live555_rtsp_config_init

```c
void live555_rtsp_config_init(live555_rtsp_config_t *config);
```

初始化默认配置。

默认值：

```text
rtsp_port = 8554
stream_name = "live"
username = NULL
password = NULL
```

### live555_rtsp_start

```c
int live555_rtsp_start(const live555_rtsp_config_t *config);
```

启动 RTSP 服务线程。

返回值：

```text
0   成功
<0  失败
```

### live555_rtsp_push_h264_annexb

```c
int live555_rtsp_push_h264_annexb(const uint8_t *data,
                                  size_t size,
                                  uint64_t timestamp_us);
```

推送一帧或一组完整 Annex-B H264 数据。

返回值：

```text
0   成功入队
-1  参数非法
-2  RTSP 服务未运行
```

说明：

```text
模块内部会拷贝 data。
函数返回后，上层可以复用或释放编码器输出 buffer。
```

### live555_rtsp_get_url

```c
const char *live555_rtsp_get_url(void);
```

获取当前 RTSP URL。

示例：

```text
rtsp://192.168.1.56:8554/live
```

返回字符串由模块内部维护，上层不要释放。

### live555_rtsp_is_running

```c
int live555_rtsp_is_running(void);
```

返回：

```text
1  正在运行
0  未运行
```

### live555_rtsp_stop

```c
void live555_rtsp_stop(void);
```

停止 RTSP 服务，关闭 live555 事件线程，并清空内部队列。

## 7. 编译链接

本模块当前以静态库方式提供。

链接时建议使用 `g++` 作为最终链接器，因为模块内部由 C++ 实现。

典型链接顺序：

```bash
aarch64-linux-gnu-g++ \
  your_objects.o \
  lib/librtsp_push.a \
  lib/libliveMedia.a \
  lib/libgroupsock.a \
  lib/libBasicUsageEnvironment.a \
  lib/libUsageEnvironment.a \
  -lpthread -ldl
```

如果项目使用 CMake，可按相同顺序加入 target link libraries。

## 8. 客户端访问

服务启动后，客户端可通过 RTSP URL 拉流。

默认地址格式：

```text
rtsp://<设备IP>:8554/live
```

例如：

```text
rtsp://192.168.1.56:8554/live
```

可用 VLC、ffplay 或支持 RTSP 的客户端验证。

## 9. 注意事项

1. 推送前必须先调用 `live555_rtsp_start()`。

2. 输入必须是 Annex-B H264，不能是 AVCC 格式。

3. 时间戳单位是微秒，建议单调递增。

4. 建议每个 IDR 前带 SPS/PPS，便于客户端中途加入。

5. 当前模块只负责视频 H264，不包含音频。

6. 当前模块不主动控制编码器 IDR，如需新客户端秒开，后续可由上层在客户端接入时触发 IDR。

## 10. 一句话接入方式

上层框架只需要：

```text
启动时 start RTSP；
编码器每输出一帧完整 Annex-B H264 就 push；
退出时 stop RTSP。
```
