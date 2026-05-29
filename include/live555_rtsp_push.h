#ifndef LIVE555_RTSP_PUSH_H
#define LIVE555_RTSP_PUSH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct live555_rtsp_config_t {
    unsigned short rtsp_port;
    const char *stream_name;
    const char *username;
    const char *password;
} live555_rtsp_config_t;

void live555_rtsp_config_init(live555_rtsp_config_t *config);

int live555_rtsp_start(const live555_rtsp_config_t *config);

int live555_rtsp_push_h264_annexb(const uint8_t *data,
                                  size_t size,
                                  uint64_t timestamp_us);

int live555_rtsp_is_running(void);

const char *live555_rtsp_get_url(void);

void live555_rtsp_stop(void);

#ifdef __cplusplus
}
#endif

#endif
