/*
 * main.cpp
 * 应用程序入口文件
 * 负责初始化RealtimeVoiceApp并启动主循环，是应用程序的启动点
 */

#include <Arduino.h>

#include "realtime_voice_app.h"

namespace {

// 全局RealtimeVoiceApp实例
RealtimeVoiceApp g_app;

}  // namespace

/**
 * @brief Arduino初始化函数
 * 初始化RealtimeVoiceApp，如果初始化失败则进入死循环
 */
void setup() {
    if (!g_app.begin()) {
        while (true) {
            delay(1000);
        }
    }
}

/**
 * @brief Arduino主循环函数
 * 调用RealtimeVoiceApp的loop方法处理实时语音任务
 */
void loop() {
    g_app.loop();
}
