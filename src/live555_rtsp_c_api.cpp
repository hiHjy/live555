#include "live555_rtsp_push.h"

#include "live555RtspService.hh"

#include <chrono>
#include <string>
#include <thread>

namespace {

static std::string g_rtspURL;

static const char *safeCString(const char *value, const char *fallback)
{
    return value != nullptr ? value : fallback;
}

} // namespace

void live555_rtsp_config_init(live555_rtsp_config_t *config)
{
    if (config == nullptr) {
        return;
    }

    config->rtsp_port = 8554;
    config->stream_name = "live";
    config->username = nullptr;
    config->password = nullptr;
}

int live555_rtsp_start(const live555_rtsp_config_t *config)
{
    live555_rtsp_config_t defaultConfig;
    if (config == nullptr) {
        live555_rtsp_config_init(&defaultConfig);
        config = &defaultConfig;
    }

    const unsigned short port = config->rtsp_port == 0 ? 8554 : config->rtsp_port;
    const char *streamName = safeCString(config->stream_name, "live");
    const char *username = safeCString(config->username, "");
    const char *password = safeCString(config->password, "");

    const bool ok = Live555RtspService::getInstance().start(
        port,
        streamName,
        username,
        password);

    if (!ok) {
        return -1;
    }

    for (int i = 0; i < 100; ++i) {
        g_rtspURL = Live555RtspService::getInstance().rtspURL();
        if (!g_rtspURL.empty()) {
            break;
        }
        if (!Live555RtspService::getInstance().isRunning()) {
            return -2;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
}

int live555_rtsp_push_h264_annexb(const uint8_t *data,
                                  size_t size,
                                  uint64_t timestamp_us)
{
    if (data == nullptr || size == 0) {
        return -1;
    }
    if (!Live555RtspService::getInstance().isRunning()) {
        return -2;
    }

    Live555RtspService::getInstance().pushH264Frame(data, size, timestamp_us);
    return 0;
}

int live555_rtsp_is_running(void)
{
    return Live555RtspService::getInstance().isRunning() ? 1 : 0;
}

const char *live555_rtsp_get_url(void)
{
    g_rtspURL = Live555RtspService::getInstance().rtspURL();
    return g_rtspURL.empty() ? nullptr : g_rtspURL.c_str();
}

void live555_rtsp_stop(void)
{
    Live555RtspService::getInstance().stop();
    g_rtspURL.clear();
}
