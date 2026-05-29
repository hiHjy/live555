#include "live555RtspService.hh"

#include "h264FrameQueue.hh"
#include "myH264Subsession.hh"

#include <iostream>

Live555RtspService &Live555RtspService::getInstance()
{
    static Live555RtspService instance;
    return instance;
}

bool Live555RtspService::start()
{
    return start(rtspPort_, streamName_, username_, password_);
}

bool Live555RtspService::start(unsigned short rtspPort,
                               const std::string& streamName,
                               const std::string& username,
                               const std::string& password)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return true;
    }

    rtspPort_ = rtspPort == 0 ? 8554 : rtspPort;
    streamName_ = streamName.empty() ? "live" : streamName;
    username_ = username;
    password_ = password;
    rtspURL_.clear();

    eventLoopWatchVariable_.store(0);
    startOk_ = false;
    running_ = true;

    rtspThread_ = std::thread(&Live555RtspService::rtspThreadMain, this);
    return true;
}

void Live555RtspService::stop()
{
    TaskScheduler* scheduler = nullptr;
    EventTriggerId stopTrigger = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }

        eventLoopWatchVariable_.store(1);
        scheduler = scheduler_;
        stopTrigger = stopTrigger_;
    }

    if (scheduler != nullptr && stopTrigger != 0) {
        scheduler->triggerEvent(stopTrigger, this);
    }

    if (rtspThread_.joinable()) {
        rtspThread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    std::cout << "正常退出" << std::endl;
}

void Live555RtspService::pushH264Frame(const uint8_t* data, size_t size, uint64_t timestampUs)
{
    if (frameQueue_ == nullptr) {
        return;
    }

    frameQueue_->pushAnnexBFrame(data, size, timestampUs);
}

bool Live555RtspService::isRunning() const
{
    return running_.load();
}

std::string Live555RtspService::rtspURL() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return rtspURL_;
}

Live555RtspService::Live555RtspService()
    : running_(false),
      startOk_(false),
      eventLoopWatchVariable_(0),
      scheduler_(nullptr),
      env_(nullptr),
      authDB_(nullptr),
      server_(nullptr),
      stopTrigger_(0),
      frameQueue_(std::make_shared<H264FrameQueue>()),
      streamName_("live"),
      username_(),
      password_(),
      rtspURL_(),
      rtspPort_(8554)
{
}

Live555RtspService::~Live555RtspService()
{
    stop();
}

void Live555RtspService::rtspThreadMain()
{
    if (!setupLive555()) {
        cleanupLive555();
        running_ = false;
        return;
    }

    startOk_ = true;
    env_->taskScheduler().doEventLoop(&eventLoopWatchVariable_);

    cleanupLive555();
}

bool Live555RtspService::setupLive555()
{
    scheduler_ = BasicTaskScheduler::createNew();
    if (scheduler_ == nullptr) {
        return false;
    }

    env_ = BasicUsageEnvironment::createNew(*scheduler_);
    if (env_ == nullptr) {
        return false;
    }

    stopTrigger_ = scheduler_->createEventTrigger(stopEventCallback);

    if (!username_.empty() || !password_.empty()) {
        authDB_ = new UserAuthenticationDatabase;
        authDB_->addUserRecord(username_.c_str(), password_.c_str());
    }

    server_ = RTSPServer::createNew(*env_, rtspPort_, authDB_);
    if (server_ == nullptr) {
        *env_ << "Failed to create RTSP server: " << env_->getResultMsg() << "\n";
        return false;
    }

    ServerMediaSession* sms = ServerMediaSession::createNew(
        *env_,
        streamName_.c_str(),
        streamName_.c_str(),
        "Live555 RTSP Service H264 Stream");

    sms->addSubsession(MyH264Subsession::createNew(*env_, frameQueue_));
    server_->addServerMediaSession(sms);

    char* url = server_->rtspURL(sms);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        rtspURL_ = url != nullptr ? url : "";
    }
    *env_ << "RTSP server started\n";
    *env_ << "URL: " << url << "\n";
    delete[] url;

    return true;
}

void Live555RtspService::cleanupLive555()
{
    std::cout << "clearup called"  << std::endl;
    if (scheduler_ != nullptr && stopTrigger_ != 0) {
        scheduler_->deleteEventTrigger(stopTrigger_);
        stopTrigger_ = 0;
    }

    if (server_ != nullptr) {
        Medium::close(server_);
        server_ = nullptr;
    }

    delete authDB_;
    authDB_ = nullptr;

    if (env_ != nullptr) {
        env_->reclaim();
        env_ = nullptr;
    }

    delete scheduler_;
    scheduler_ = nullptr;

    if (frameQueue_ != nullptr) {
        frameQueue_->clear();
    }
}

void Live555RtspService::stopEventCallback(void* clientData)
{

    (void)clientData;
    std::cout << "stopEventCallback  called" << std::endl;
}
