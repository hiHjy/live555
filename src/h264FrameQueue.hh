#ifndef H264_FRAME_QUEUE_HH
#define H264_FRAME_QUEUE_HH

#include <BasicUsageEnvironment.hh>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <sys/time.h>
#include <vector>

class H264FrameQueue {
public:
    struct Nalu {
        std::vector<uint8_t> data;
        timeval presentationTime;
    };

    H264FrameQueue();

    void pushAnnexBFrame(const uint8_t* data, size_t size, uint64_t timestampUs);
    bool popNalu(Nalu& nalu);
    void clear();

    void registerReader(TaskScheduler* scheduler, EventTriggerId triggerId, void* clientData);
    void unregisterReader(EventTriggerId triggerId);

    bool hasSps() const;
    bool hasPps() const;

private:
    struct Reader {
        TaskScheduler* scheduler;
        EventTriggerId triggerId;
        void* clientData;
    };

    struct NalSpan {
        size_t offset;
        size_t size;
    };

    static size_t findStartCode(const uint8_t* data, size_t size, size_t from, size_t& startCodeSize);
    static std::vector<NalSpan> splitAnnexB(const uint8_t* data, size_t size);
    static timeval timestampToTimeval(uint64_t timestampUs);

    void pushPureNaluLocked(const uint8_t* data, size_t size, const timeval& presentationTime);
    void wakeReadersLocked();

private:
    mutable std::mutex mutex_;
    std::deque<Nalu> queue_;
    std::vector<Reader> readers_;
    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;
    size_t maxQueuedNalus_;
};

#endif
