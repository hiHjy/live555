#ifndef MY_H264_SUBSESSION_HH
#define MY_H264_SUBSESSION_HH

#include <OnDemandServerMediaSubsession.hh>

#include <memory>

class H264FrameQueue;

class MyH264Subsession : public OnDemandServerMediaSubsession
{
public:
    // live555 常用 createNew() 工厂函数，而不是让外部直接 new。
    //
    // env:
    //   live555 的运行环境，用于日志、错误信息、事件调度等。
    //
    // fileName:
    //   demo 阶段要读取的 H264 Annex-B 文件路径。
    //   后面真实项目里，这个参数可以替换成你的编码器队列/数据源对象。
    static MyH264Subsession *createNew(UsageEnvironment& env, std::shared_ptr<H264FrameQueue> frameQueue);

protected:
    // 每个客户端 SETUP 这路流时，live555 会调用这个函数创建输入源。
    //
    // clientSessionId:
    //   live555 给当前客户端会话分配的 ID。
    //   demo 阶段一般用不到，但真实项目里可以用它区分不同客户端。
    //
    // estBitrate:
    //   估算码率，单位是 kbps。
    //   live555 会把它用于 SDP 里的带宽描述，例如 b=AS:xxx。
    //
    // 返回值:
    //   返回一个 FramedSource。
    //   我们这里后面会返回 MyH264Source，它负责产出 H264 NAL。
    FramedSource* createNewStreamSource(unsigned clientSessionId,
                                        unsigned& estBitrate) override;

    // 每个客户端 SETUP 这路流时，live555 也会调用这个函数创建 RTP sink。
    //
    // rtpGroupsock:
    //   live555 已经创建好的 RTP socket/传输通道封装。
    //   RTP 包最终会通过它发给客户端。
    //
    // rtpPayloadTypeIfDynamic:
    //   动态 RTP payload type。
    //   H264 常用动态 payload type，例如 96。
    //   这个值会出现在 SDP 里：
    //     m=video 0 RTP/AVP 96
    //
    // inputSource:
    //   上面 createNewStreamSource() 创建出来的输入源。
    //   对 H264 来说，就是 MyH264Source。
    //
    // 返回值:
    //   返回一个 RTPSink。
    //   我们这里后面会返回 H264VideoRTPSink，它负责把 H264 NAL 打成 RTP 包。
    RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
                              unsigned char rtpPayloadTypeIfDynamic,
                              FramedSource* inputSource) override;

protected:
    // 构造函数放 protected，是 live555 示例里的常见写法：
    // 外部通过 createNew() 创建对象，而不是直接调用构造函数。
    MyH264Subsession(UsageEnvironment& env, std::shared_ptr<H264FrameQueue> frameQueue);
    ~MyH264Subsession() override;

private:
    std::shared_ptr<H264FrameQueue> frameQueue_;
};

#endif
