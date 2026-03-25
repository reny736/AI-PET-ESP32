#pragma once

#include <Arduino.h>
#include <esp32-hal-log.h>
#include <stdarg.h>

#include "app_config.h"

namespace logutil {

inline void vlog(const char* level, const char* tag, const char* fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_printf("[%10lu][%s][%s] %s\n", millis(), level, tag, buffer);
}

inline void hexDump(const char* tag, const uint8_t* data, size_t len, size_t max_len = 16) {
    if (data == nullptr || len == 0) {
        return;
    }
    const size_t dump_len = len < max_len ? len : max_len;
    log_printf("[%10lu][HEX][%s] ", millis(), tag);
    for (size_t i = 0; i < dump_len; ++i) {
        log_printf("%02X ", data[i]);
    }
    if (dump_len < len) {
        log_printf("...");
    }
    log_printf("\n");
}

}  // namespace logutil

#define LOGE(tag, fmt, ...) logutil::vlog("ERR", tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) \
    do { \
        if (app::kActiveLogLevel >= app::kLogLevelWarn) { \
            logutil::vlog("WRN", tag, fmt, ##__VA_ARGS__); \
        } \
    } while (0)
#define LOGI(tag, fmt, ...) \
    do { \
        if (app::kActiveLogLevel >= app::kLogLevelInfo) { \
            logutil::vlog("INF", tag, fmt, ##__VA_ARGS__); \
        } \
    } while (0)
#define LOGD(tag, fmt, ...) \
    do { \
        if (app::kActiveLogLevel >= app::kLogLevelDebug) { \
            logutil::vlog("DBG", tag, fmt, ##__VA_ARGS__); \
        } \
    } while (0)
