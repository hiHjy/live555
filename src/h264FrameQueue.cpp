#include "h264FrameQueue.hh"

#include <algorithm>
#include <cstring>
#include <string>

H264FrameQueue::H264FrameQueue()
    : maxQueuedNalus_(300)
{
}

void H264FrameQueue::pushAnnexBFrame(const uint8_t* data, size_t size, uint64_t timestampUs)
{
    if (data == nullptr || size == 0) {
        return;
    }

    const timeval presentationTime = timestampToTimeval(timestampUs);
    const std::vector<NalSpan> nals = splitAnnexB(data, size);

    std::lock_guard<std::mutex> lock(mutex_);

    if (nals.empty()) {
        pushPureNaluLocked(data, size, presentationTime);
        wakeReadersLocked();
        return;
    }

    for (const NalSpan& nal : nals) {
        pushPureNaluLocked(data + nal.offset, nal.size, presentationTime);
    }
    
    wakeReadersLocked();
}

bool H264FrameQueue::popNalu(Nalu& nalu)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return false;
    }

    nalu = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

void H264FrameQueue::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
}

void H264FrameQueue::registerReader(TaskScheduler* scheduler, EventTriggerId triggerId, void* clientData)
{
    if (scheduler == nullptr || triggerId == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    readers_.push_back({scheduler, triggerId, clientData});
}

void H264FrameQueue::unregisterReader(EventTriggerId triggerId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    readers_.erase(
        std::remove_if(readers_.begin(), readers_.end(),
                       [triggerId](const Reader& reader) {
                           return reader.triggerId == triggerId;
                       }),
        readers_.end());
}

bool H264FrameQueue::hasSps() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return !sps_.empty();
}

bool H264FrameQueue::hasPps() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return !pps_.empty();
}

size_t H264FrameQueue::findStartCode(const uint8_t* data, size_t size, size_t from, size_t& startCodeSize)
{
    for (size_t i = from; i + 3 <= size; ++i) {
        if (i + 3 <= size &&
            data[i] == 0x00 &&
            data[i + 1] == 0x00 &&
            data[i + 2] == 0x01) {
            startCodeSize = 3;
            return i;
        }

        if (i + 4 <= size &&
            data[i] == 0x00 &&
            data[i + 1] == 0x00 &&
            data[i + 2] == 0x00 &&
            data[i + 3] == 0x01) {
            startCodeSize = 4;
            return i;
        }
    }

    startCodeSize = 0;
    return std::string::npos;
}

std::vector<H264FrameQueue::NalSpan> H264FrameQueue::splitAnnexB(const uint8_t* data, size_t size)
{
    std::vector<NalSpan> nals;

    size_t startCodeSize = 0;
    size_t start = findStartCode(data, size, 0, startCodeSize);
    while (start != std::string::npos) {
        const size_t nalStart = start + startCodeSize;

        size_t nextStartCodeSize = 0;
        const size_t nextStart = findStartCode(data, size, nalStart, nextStartCodeSize);
        const size_t nalEnd = nextStart == std::string::npos ? size : nextStart;

        if (nalEnd > nalStart) {
            nals.push_back({nalStart, nalEnd - nalStart});
        }

        start = nextStart;
        startCodeSize = nextStartCodeSize;
    }

    return nals;
}

timeval H264FrameQueue::timestampToTimeval(uint64_t timestampUs)
{
    timeval result;
    result.tv_sec = timestampUs / 1000000;
    result.tv_usec = timestampUs % 1000000;
    return result;
}

void H264FrameQueue::pushPureNaluLocked(const uint8_t* data, size_t size, const timeval& presentationTime)
{
    if (size == 0) {
        return;
    }

    const uint8_t nalType = data[0] & 0x1F;
    if (nalType == 7 && sps_.empty()) {
        sps_.assign(data, data + size);
    } else if (nalType == 8 && pps_.empty()) {
        pps_.assign(data, data + size);
    }

    while (queue_.size() >= maxQueuedNalus_) {
        queue_.pop_front();
    }

    Nalu nalu;
    nalu.data.assign(data, data + size);
    nalu.presentationTime = presentationTime;
    queue_.push_back(std::move(nalu));
}

void H264FrameQueue::wakeReadersLocked()
{
    for (const Reader& reader : readers_) {
        reader.scheduler->triggerEvent(reader.triggerId, reader.clientData);
    }
}
