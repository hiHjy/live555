#ifndef LIVE555_RTSP_SERVICE_HH
#define LIVE555_RTSP_SERVICE_HH

#include <BasicUsageEnvironment.hh>
#include <RTSPServer.hh>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class H264FrameQueue;

class Live555RtspService {
public:
    Live555RtspService(const Live555RtspService&) = delete;
    Live555RtspService& operator=(const Live555RtspService&) = delete;

    static Live555RtspService& getInstance();

    bool start();
    bool start(unsigned short rtspPort,
               const std::string& streamName,
               const std::string& username,
               const std::string& password);
    void stop();
    void pushH264Frame(const uint8_t* data, size_t size, uint64_t timestampUs);
    bool isRunning() const;
    std::string rtspURL() const;

private:
    Live555RtspService();
public:
    ~Live555RtspService();

private:
    void rtspThreadMain();
    bool setupLive555();
    void cleanupLive555();
    static void stopEventCallback(void* clientData);

private:
    mutable std::mutex mutex_;
    std::thread rtspThread_;
    std::atomic_bool running_;
    std::atomic_bool startOk_;

    EventLoopWatchVariable eventLoopWatchVariable_;

    TaskScheduler* scheduler_;
    UsageEnvironment* env_;
    UserAuthenticationDatabase* authDB_;
    RTSPServer* server_;
    EventTriggerId stopTrigger_;

    std::shared_ptr<H264FrameQueue> frameQueue_;
    std::string streamName_;
    std::string username_;
    std::string password_;
    std::string rtspURL_;
    unsigned short rtspPort_;
};

#endif
