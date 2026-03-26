#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

#include "audio_pipeline.h"
#include "doubao_protocol.h"
#include "doubao_ws_client.h"
#include "ota_manager.h"
#include "status_led.h"

/**
 * @file realtime_voice_app.h
 * @brief 实时语音应用
 * @details 负责管理整个语音助手应用的状态和流程，包括WiFi连接、API通信、会话管理、OTA更新等功能
 */

/**
 * @enum AppState
 * @brief 应用状态枚举
 */
enum class AppState {
    Booting,         // 启动中
    WifiConnecting,  // Wi-Fi连接中
    Standby,         // 待机
    ApiConnecting,   // API连接中
    SessionStarting, // 会话启动中
    Ota,            // OTA更新模式
    Listening,       // 监听中
    Thinking,        // 思考中
    Speaking,        // 说话中
    Error            // 错误
};

/**
 * @class RealtimeVoiceApp
 * @brief 实时语音应用类
 * @details 管理整个语音助手应用的状态和流程，包括WiFi连接、API通信、会话管理、OTA更新等功能
 */
class RealtimeVoiceApp {
public:
    /**
     * @brief 构造函数
     */
    RealtimeVoiceApp();

    /**
     * @brief 初始化应用
     * @return 初始化成功返回true，失败返回false
     */
    bool begin();
    
    /**
     * @brief 主循环
     */
    void loop();

private:
    /**
     * @brief 连接WiFi
     * @return 连接成功返回true，失败返回false
     */
    bool connectWiFi();
    
    /**
     * @brief 连接豆包API
     * @return 连接成功返回true，失败返回false
     */
    bool connectDoubao();
    
    /**
     * @brief 初始化辅助串口
     * @return 初始化成功返回true，失败返回false
     */
    bool beginAuxSerial();
    
    /**
     * @brief 初始化OTA按钮
     */
    void beginOtaButtons();
    
    /**
     * @brief 发送开始连接消息
     * @return 发送成功返回true，失败返回false
     */
    bool sendStartConnection();
    
    /**
     * @brief 发送开始会话消息
     * @return 发送成功返回true，失败返回false
     */
    bool sendStartSession();
    
    /**
     * @brief 发送打招呼消息
     * @return 发送成功返回true，失败返回false
     */
    bool sendSayHello();
    
    /**
     * @brief 开始监听
     */
    void startListening();
    
    /**
     * @brief 停止监听，进入思考状态
     */
    void stopListeningForThinking();
    
    /**
     * @brief 处理串口命令
     */
    void serviceSerialCommands();
    
    /**
     * @brief 处理辅助串口
     */
    void serviceAuxSerial();
    
    /**
     * @brief 处理OTA按钮
     */
    void serviceOtaButtons();
    
    /**
     * @brief 处理OTA模式
     */
    void serviceOtaMode();
    
    /**
     * @brief 处理串口命令
     * @param command 命令字符串
     */
    void handleSerialCommand(const String& command);
    
    /**
     * @brief 请求API激活
     */
    void requestApiActivation();
    
    /**
     * @brief 停用API连接
     * @param reason 停用原因
     */
    void deactivateApiConnection(const char* reason);
    
    /**
     * @brief 加载扬声器音量偏好
     */
    void loadSpeakerVolumePreference();
    
    /**
     * @brief 保存扬声器音量偏好
     * @param volume_percent 音量百分比
     */
    void saveSpeakerVolumePreference(uint8_t volume_percent);
    
    /**
     * @brief 应用扬声器音量
     * @param volume_percent 音量百分比
     * @param persist 是否持久化
     * @param source 来源
     */
    void applySpeakerVolume(int volume_percent, bool persist, const char* source);
    
    /**
     * @brief 打印串口帮助信息
     */
    void printSerialHelp();
    
    /**
     * @brief 打印OTA帮助信息
     */
    void printOtaHelp();
    
    /**
     * @brief 处理OTA命令
     * @param command 命令字符串
     */
    void handleOtaCommand(const String& command);
    
    /**
     * @brief 进入OTA模式
     * @param target OTA目标
     * @param reason 进入原因
     * @return 成功返回true，失败返回false
     */
    bool enterOtaMode(OtaTarget target, const char* reason);
    
    /**
     * @brief 退出OTA模式
     * @param reason 退出原因
     */
    void exitOtaMode(const char* reason);
    
    /**
     * @brief 准备OTA更新
     * @param reason 准备原因
     * @return 成功返回true，失败返回false
     */
    bool prepareForOta(const char* reason);
    
    /**
     * @brief 运行OTA更新
     * @param target OTA目标
     * @param url 固件URL
     * @param expected_sha256 期望的SHA256校验和
     * @param resolved_version 解析的版本
     * @return 更新成功返回true，失败返回false
     */
    bool runOtaUpdate(
        OtaTarget target,
        const String& url,
        const String& expected_sha256,
        const String& resolved_version = "");
    
    /**
     * @brief 读取OTA按钮状态
     * @param pin 引脚
     * @return 按钮按下返回true，否则返回false
     */
    bool readOtaButton(int pin) const;
    
    /**
     * @brief 检测按钮按下
     * @param pin 引脚
     * @param enabled 是否启用
     * @param last_raw 上一次原始状态
     * @param stable_pressed 稳定按下状态
     * @param last_change_ms 最后一次变化时间
     * @return 检测到按下返回true，否则返回false
     */
    bool detectButtonPress(
        int pin,
        bool enabled,
        bool& last_raw,
        bool& stable_pressed,
        uint32_t& last_change_ms);
    
    /**
     * @brief 发送STM32停止命令
     */
    void sendStm32StopCommand();
    
    /**
     * @brief 设置应用状态
     * @param state 新状态
     */
    void setState(AppState state);
    
    /**
     * @brief 安排重连
     * @param reason 重连原因
     * @param allow_playback_drain 是否允许播放排空
     */
    void scheduleReconnect(const char* reason, bool allow_playback_drain = false);
    
    /**
     * @brief 检查是否需要重连
     */
    void reconnectIfNeeded();
    
    /**
     * @brief 处理音频上行链路
     */
    void serviceAudioUplink();
    
    /**
     * @brief 处理会话状态
     */
    void serviceConversationState();
    
    /**
     * @brief 打印监控信息
     */
    void printMonitor();

    /**
     * @brief 处理语音状态变化
     * @param speaking 是否正在说话
     */
    void handleSpeechStateChanged(bool speaking);
    
    /**
     * @brief 处理socket消息
     * @param data 消息数据
     * @param len 消息长度
     */
    void handleSocketMessage(const uint8_t* data, size_t len);
    
    /**
     * @brief 处理socket断开连接
     */
    void handleSocketDisconnected();
    
    /**
     * @brief 处理服务器事件
     * @param message 服务器消息
     */
    void handleServerEvent(const doubao::ServerMessage& message);
    
    /**
     * @brief 处理服务器音频
     * @param message 服务器消息
     */
    void handleServerAudio(const doubao::ServerMessage& message);

    StatusLed led_;  // 状态LED
    AudioPipeline audio_;  // 音频管道
    DoubaoWsClient ws_client_;  // 豆包WebSocket客户端
    OtaManager ota_;  // OTA管理器
    HardwareSerial aux_serial_;  // 辅助串口
    doubao::SessionConfig session_config_;  // 会话配置

    AppState state_;  // 应用状态
    String session_id_;  // 会话ID

    bool session_ready_;  // 会话是否就绪
    bool tts_ended_;  // TTS是否结束
    bool reconnect_scheduled_;  // 是否安排了重连
    bool reconnect_after_playback_;  // 是否在播放后重连
    bool audio_uplink_enabled_;  // 音频上行链路是否启用
    bool api_activation_requested_;  // API激活是否请求
    bool ota_mode_request_seen_;  // OTA模式请求是否已见
    bool ota_esp32_button_enabled_;  // ESP32 OTA按钮是否启用
    bool ota_stm32_button_enabled_;  // STM32 OTA按钮是否启用
    bool ota_esp32_button_raw_;  // ESP32 OTA按钮原始状态
    bool ota_stm32_button_raw_;  // STM32 OTA按钮原始状态
    bool ota_esp32_button_pressed_;  // ESP32 OTA按钮是否按下
    bool ota_stm32_button_pressed_;  // STM32 OTA按钮是否按下
    String deferred_reconnect_reason_;  // 延迟重连原因

    uint32_t reconnect_at_ms_;  // 重连时间
    uint32_t last_audio_rx_ms_;  // 最后一次音频接收时间
    uint32_t last_tts_ended_ms_;  // 最后一次TTS结束时间
    uint32_t last_monitor_ms_;  // 最后一次监控时间
    uint32_t listening_started_ms_;  // 开始监听时间
    uint32_t thinking_started_ms_;  // 开始思考时间
    uint32_t ota_mode_started_ms_;  // OTA模式开始时间
    uint32_t ota_last_attempt_ms_;  // 最后一次OTA尝试时间
    uint32_t ota_esp32_button_changed_ms_;  // ESP32 OTA按钮最后一次变化时间
    uint32_t ota_stm32_button_changed_ms_;  // STM32 OTA按钮最后一次变化时间
    String serial_command_buffer_;  // 串口命令缓冲区
};
