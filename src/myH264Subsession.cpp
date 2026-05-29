#include "myH264Subsession.hh"
#include "myH264Source.hh"

#include "h264FrameQueue.hh"

#include <H264VideoRTPSink.hh>
#include <H264VideoStreamDiscreteFramer.hh>
#include <MediaSink.hh>

MyH264Subsession *MyH264Subsession::createNew(UsageEnvironment &env, std::shared_ptr<H264FrameQueue> frameQueue)
{
    return new MyH264Subsession(env, std::move(frameQueue));
}

FramedSource *MyH264Subsession::createNewStreamSource(unsigned clientSessionId, unsigned &estBitrate)
{
    // estBitrate 是估算码率，单位 kbps。
    // live555 会把它写进 SDP 的 b=AS:xxx，用于告诉客户端大概带宽。
    // demo 阶段先填一个保守值，后面可以按真实编码器码率调整。
    estBitrate = 500;

    // 每个客户端 SETUP 时，live555 都会为这个客户端创建一个新的 FramedSource。
    //
    // 第一层是我们自己的 MyH264Source：
    //   MyH264Source 负责从实时队列里取 H264 NAL，并填充 fTo/fFrameSize。
    //
    // 第二层用 H264VideoStreamDiscreteFramer 包起来：
    //   H264VideoRTPSink 要求输入源是 H264VideoStreamFramer 类型。
    //   我们的 MyH264Source 输出的是“完整、离散的 H264 NAL”，不是任意字节流，
    //   所以这里用 DiscreteFramer，而不是 H264VideoStreamFramer。
    //
    // 返回给 live555 的最终 source 是 framer。
    FramedSource *source = MyH264Source::createNew(envir(), frameQueue_);
    if (source == nullptr) {
        return nullptr;
    }

    return H264VideoStreamDiscreteFramer::createNew(envir(), source);
}

RTPSink *MyH264Subsession::createNewRTPSink(Groupsock *rtpGroupsock,
                                            unsigned char rtpPayloadTypeIfDynamic,
                                            FramedSource *inputSource)
{
    // RTPSink 负责把 FramedSource 给出的原始媒体帧打成 RTP 包。
    //
    // 对 H264 来说，使用 live555 自带的 H264VideoRTPSink。
    // 它会处理：
    //   1. RTP payload type
    //   2. H264 RTP payload 格式
    //   3. 大 NAL 的 FU-A 分片
    //   4. SDP 里 H264 相关描述的一部分
    //
    // rtpGroupsock:
    //   live555 创建好的 RTP 传输通道。
    //
    // rtpPayloadTypeIfDynamic:
    //   动态 payload type，通常是 96 这类值。
    //
    // inputSource:
    //   createNewStreamSource() 返回的 source。
    //   H264VideoRTPSink::createNew() 这里暂时不需要直接用它，
    //   但函数签名是父类规定的，所以必须接收这个参数。
    (void)inputSource;

    // 注意：这不是 MyH264Source 里的 fMaxSize。
    //
    // fMaxSize:
    //   FramedSource 接收一帧原始数据时的输入缓冲大小。
    //
    // OutPacketBuffer::maxSize:
    //   RTPSink 把一帧数据打成 RTP 包之前使用的输出缓冲大小。
    //
    // 当前日志里的 “Current value is 60000” 指的是这里。
    // 必须在创建 H264VideoRTPSink 之前设置。
    OutPacketBuffer::maxSize = 2 * 1024 * 1024;

    return H264VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
}

MyH264Subsession::MyH264Subsession(UsageEnvironment &env, std::shared_ptr<H264FrameQueue> frameQueue):
    // 第二个参数 reuseFirstSource：
    //   True  表示多个客户端复用同一个 source。
    //   False 表示每个客户端创建自己的 source。
    //
    // 当前是实时编码器队列，通常希望多个客户端看同一路实时流。
    OnDemandServerMediaSubsession(env, true), frameQueue_(std::move(frameQueue))
{
}

MyH264Subsession::~MyH264Subsession()
{
}
