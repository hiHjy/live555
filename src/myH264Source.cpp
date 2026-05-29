#include "myH264Source.hh"

#include "h264FrameQueue.hh"

#include <cstring>

MyH264Source *MyH264Source::createNew(UsageEnvironment &env, std::shared_ptr<H264FrameQueue> frameQueue)
{
    return new MyH264Source(env, std::move(frameQueue));
}

MyH264Source::MyH264Source(UsageEnvironment &env, std::shared_ptr<H264FrameQueue> frameQueue)
    : FramedSource(env),
      frameQueue_(std::move(frameQueue)),
      frameArrivedTrigger_(0)
{
    fMaxSize = 2 * 1024 * 1024;

    frameArrivedTrigger_ = envir().taskScheduler().createEventTrigger(frameArrivedCallback);
    if (frameQueue_ != nullptr) {
        frameQueue_->registerReader(&envir().taskScheduler(), frameArrivedTrigger_, this);
    }
}

MyH264Source::~MyH264Source()
{
    doStopGettingFrames();

    if (frameQueue_ != nullptr) {
        frameQueue_->unregisterReader(frameArrivedTrigger_);
    }

    if (frameArrivedTrigger_ != 0) {
        envir().taskScheduler().deleteEventTrigger(frameArrivedTrigger_);
        frameArrivedTrigger_ = 0;
    }
}

void MyH264Source::doGetNextFrame()
{
    deliverFrame();
}

void MyH264Source::doStopGettingFrames()
{
    FramedSource::doStopGettingFrames();
}

void MyH264Source::deliverFrame()
{
    if (!isCurrentlyAwaitingData()) {
        return;
    }

    if (frameQueue_ == nullptr) {
        return;
    }

    H264FrameQueue::Nalu nalu;
    if (!frameQueue_->popNalu(nalu)) {
        return;
    }

    const size_t srcSize = nalu.data.size();
    if (srcSize > fMaxSize) {
        fFrameSize = fMaxSize;
        fNumTruncatedBytes = srcSize - fMaxSize;
    } else {
        fFrameSize = srcSize;
        fNumTruncatedBytes = 0;
    }

    std::memcpy(fTo, nalu.data.data(), fFrameSize);
    fPresentationTime = nalu.presentationTime;

    FramedSource::afterGetting(this);
}

void MyH264Source::frameArrivedCallback(void* clientData)
{
    static_cast<MyH264Source*>(clientData)->deliverFrame();
}
