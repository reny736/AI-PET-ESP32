#pragma once

#include <Arduino.h>
#include <esp32-hal-log.h>
#include <stdarg.h>

#include "app_config.h"

/**
 * @namespace logutil
 * @brief 日志工具命名空间
 * @details 提供日志输出和十六进制数据dump功能
 */
namespace logutil {

/**
 * @brief 通用日志输出函数
 * @param level 日志级别
 * @param tag 日志标签
 * @param fmt 格式化字符串
 * @param ... 可变参数
 */
inline void vlog(const char* level, const char* tag, const char* fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_printf("[%10lu][%s][%s] %s\n", millis(), level, tag, buffer);
}

/**
 * @brief 十六进制数据 dump 函数
 * @param tag 日志标签
 * @param data 数据指针
 * @param len 数据长度
 * @param max_len 最大输出长度，默认16
 */
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

/**
 * @brief 错误级别日志宏
 * @param tag 日志标签
 * @param fmt 格式化字符串
 * @param ... 可变参数
 */
#define LOGE(tag, fmt, ...) logutil::vlog("ERR", tag, fmt, ##__VA_ARGS__)

/**
 * @brief 警告级别日志宏
 * @param tag 日志标签
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @note 仅当日志级别大于等于警告级别时输出
 */
#define LOGW(tag, fmt, ...) \
    do { \
        if (app::kActiveLogLevel >= app::kLogLevelWarn) { \
            logutil::vlog("WRN", tag, fmt, ##__VA_ARGS__); \
        } \
    } while (0)

/**
 * @brief 信息级别日志宏
 * @param tag 日志标签
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @note 仅当日志级别大于等于信息级别时输出
 */
#define LOGI(tag, fmt, ...) \
    do { \
        if (app::kActiveLogLevel >= app::kLogLevelInfo) { \
            logutil::vlog("INF", tag, fmt, ##__VA_ARGS__); \
        } \
    } while (0)

/**
 * @brief 调试级别日志宏
 * @param tag 日志标签
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @note 仅当日志级别大于等于调试级别时输出
 */
#define LOGD(tag, fmt, ...) \
    do { \
        if (app::kActiveLogLevel >= app::kLogLevelDebug) { \
            logutil::vlog("DBG", tag, fmt, ##__VA_ARGS__); \
        } \
    } while (0)
