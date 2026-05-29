#ifndef MY_H264_SOURCE_HH
#define MY_H264_SOURCE_HH

#include <FramedSource.hh>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <sys/time.h>
#include <vector>

class H264FrameQueue;

class MyH264Source : public FramedSource {
public:
    static MyH264Source *createNew(UsageEnvironment &env, std::shared_ptr<H264FrameQueue> frameQueue);

private:
    MyH264Source(UsageEnvironment &env, std::shared_ptr<H264FrameQueue> frameQueue);
    ~MyH264Source() override;

private:
    void doGetNextFrame() override;
    void doStopGettingFrames() override;

    void deliverFrame();
    static void frameArrivedCallback(void* clientData);

private:
    std::shared_ptr<H264FrameQueue> frameQueue_;
    EventTriggerId frameArrivedTrigger_;
};

#endif
