#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

/**
 * @file app_config.h
 * @brief 应用程序配置文件
 * @details 包含所有应用程序相关的配置项和常量定义，包括WiFi配置、API配置、硬件引脚定义等
 */

// Wi-Fi 配置
#ifndef WIFI_SSID
#define WIFI_SSID "-------"  // Wi-Fi网络名称
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "-------"  // Wi-Fi密码
#endif

// 豆包API配置
#ifndef DOUBAO_APP_ID
#define DOUBAO_APP_ID "-------"  // 豆包应用ID
#endif

#ifndef DOUBAO_ACCESS_KEY
#define DOUBAO_ACCESS_KEY "-------"  // 豆包访问密钥
#endif

// OTA更新配置
#ifndef OTA_HTTP_TIMEOUT_MS
#define OTA_HTTP_TIMEOUT_MS 15000  // OTA HTTP请求超时时间（毫秒）
#endif

#ifndef OTA_SERVER_BASE_URL
#define OTA_SERVER_BASE_URL "-------"  // OTA服务器基础URL
#endif

#ifndef OTA_ESP32_MANIFEST_URL
#define OTA_ESP32_MANIFEST_URL OTA_SERVER_BASE_URL "/ESP32_OTA_version.json"  // ESP32固件清单URL
#endif

#ifndef OTA_STM32_MANIFEST_URL
#define OTA_STM32_MANIFEST_URL OTA_SERVER_BASE_URL "/STM32_OTA_version.json"  // STM32固件清单URL
#endif

#ifndef OTA_ESP32_CURRENT_VERSION
#define OTA_ESP32_CURRENT_VERSION "V1.0"  // ESP32当前固件版本
#endif

#ifndef OTA_STM32_CURRENT_VERSION
#define OTA_STM32_CURRENT_VERSION "V1.0"  // STM32当前固件版本
#endif

#ifndef OTA_ESP32_DEFAULT_URL
#define OTA_ESP32_DEFAULT_URL OTA_SERVER_BASE_URL "/ESP32_firmware_v2.0.bin"  // ESP32默认固件URL
#endif

#ifndef OTA_STM32_DEFAULT_URL
#define OTA_STM32_DEFAULT_URL OTA_SERVER_BASE_URL "/STM32_hal10_V2.0.bin"  // STM32默认固件URL
#endif

#ifndef OTA_ESP32_DEFAULT_SHA256
#define OTA_ESP32_DEFAULT_SHA256 ""  // ESP32默认固件SHA256校验和
#endif

#ifndef OTA_STM32_DEFAULT_SHA256
#define OTA_STM32_DEFAULT_SHA256 ""  // STM32默认固件SHA256校验和
#endif

#ifndef OTA_HTTP_BUFFER_BYTES
#define OTA_HTTP_BUFFER_BYTES 1024  // OTA HTTP缓冲区大小（字节）
#endif

#ifndef OTA_MANIFEST_JSON_BYTES
#define OTA_MANIFEST_JSON_BYTES 1536  // OTA清单JSON大小（字节）
#endif

#ifndef OTA_PROGRESS_INTERVAL_MS
#define OTA_PROGRESS_INTERVAL_MS 1000  // OTA进度报告间隔（毫秒）
#endif

#ifndef OTA_AUTO_RETRY_INTERVAL_MS
#define OTA_AUTO_RETRY_INTERVAL_MS 1000  // OTA自动重试间隔（毫秒）
#endif

#ifndef OTA_REQUEST_WINDOW_MS
#define OTA_REQUEST_WINDOW_MS 5000  // OTA请求窗口时间（毫秒）
#endif

#ifndef OTA_BUTTON_DEBOUNCE_MS
#define OTA_BUTTON_DEBOUNCE_MS 30  // OTA按钮去抖时间（毫秒）
#endif

#ifndef OTA_BUTTON_ACTIVE_LEVEL
#define OTA_BUTTON_ACTIVE_LEVEL LOW  // OTA按钮激活电平
#endif

#ifndef OTA_ESP32_BUTTON_PIN
#define OTA_ESP32_BUTTON_PIN 38  // ESP32 OTA按钮引脚
#endif

#ifndef OTA_STM32_BUTTON_PIN
#define OTA_STM32_BUTTON_PIN 47  // STM32 OTA按钮引脚
#endif

// STM32相关配置
#ifndef STM32_STOP_COMMAND_BYTE
#define STM32_STOP_COMMAND_BYTE 0xDD  // STM32停止命令字节
#endif

#ifndef SERIAL_COMMAND_MAX_LENGTH
#define SERIAL_COMMAND_MAX_LENGTH 512  // 串口命令最大长度
#endif

#ifndef STM32_BOOTLOADER_BAUD
#define STM32_BOOTLOADER_BAUD 115200  // STM32 bootloader波特率
#endif

#ifndef STM32_FLASH_BASE_ADDRESS
#define STM32_FLASH_BASE_ADDRESS 0x08000000UL  // STM32闪存基地址
#endif

#ifndef STM32_FLASH_SIZE_BYTES
#define STM32_FLASH_SIZE_BYTES (64UL * 1024UL)  // STM32闪存大小（字节）
#endif

#ifndef STM32_EXPECTED_DEVICE_ID
#define STM32_EXPECTED_DEVICE_ID 0x0410  // 期望的STM32设备ID
#endif

#ifndef STM32_BOOT0_PIN
#define STM32_BOOT0_PIN -1  // STM32 BOOT0引脚
#endif

#ifndef STM32_BOOT1_PIN
#define STM32_BOOT1_PIN -1  // STM32 BOOT1引脚
#endif

#ifndef STM32_NRST_PIN
#define STM32_NRST_PIN -1  // STM32 NRST引脚
#endif

#ifndef STM32_BOOT0_BOOTLOADER_LEVEL
#define STM32_BOOT0_BOOTLOADER_LEVEL HIGH  // STM32 BOOT0 bootloader电平
#endif

#ifndef STM32_BOOT0_APP_LEVEL
#define STM32_BOOT0_APP_LEVEL LOW  // STM32 BOOT0应用电平
#endif

#ifndef STM32_BOOT1_BOOTLOADER_LEVEL
#define STM32_BOOT1_BOOTLOADER_LEVEL LOW  // STM32 BOOT1 bootloader电平
#endif

#ifndef STM32_BOOT1_APP_LEVEL
#define STM32_BOOT1_APP_LEVEL LOW  // STM32 BOOT1应用电平
#endif

#ifndef STM32_NRST_ASSERT_LEVEL
#define STM32_NRST_ASSERT_LEVEL LOW  // STM32 NRST断言电平
#endif

#ifndef STM32_NRST_DEASSERT_LEVEL
#define STM32_NRST_DEASSERT_LEVEL HIGH  // STM32 NRST去断言电平
#endif

#ifndef STM32_BOOT_SYNC_TIMEOUT_MS
#define STM32_BOOT_SYNC_TIMEOUT_MS 1200  // STM32启动同步超时时间（毫秒）
#endif

#ifndef STM32_COMMAND_TIMEOUT_MS
#define STM32_COMMAND_TIMEOUT_MS 1500  // STM32命令超时时间（毫秒）
#endif

#ifndef STM32_ERASE_TIMEOUT_MS
#define STM32_ERASE_TIMEOUT_MS 30000  // STM32擦除超时时间（毫秒）
#endif

#ifndef STM32_WRITE_TIMEOUT_MS
#define STM32_WRITE_TIMEOUT_MS 3000  // STM32写入超时时间（毫秒）
#endif

#ifndef STM32_VERIFY_TIMEOUT_MS
#define STM32_VERIFY_TIMEOUT_MS 3000  // STM32验证超时时间（毫秒）
#endif

#ifndef STM32_RESET_PULSE_MS
#define STM32_RESET_PULSE_MS 80  // STM32复位脉冲时间（毫秒）
#endif

#ifndef STM32_BOOT_SETTLE_MS
#define STM32_BOOT_SETTLE_MS 120  // STM32启动稳定时间（毫秒）
#endif

/**
 * @namespace app
 * @brief 应用程序配置命名空间
 * @details 包含所有应用程序运行时使用的常量和配置
 */
namespace app {

// Wi-Fi配置
constexpr char kWifiSsid[] = WIFI_SSID;  // Wi-Fi网络名称
constexpr char kWifiPassword[] = WIFI_PASSWORD;  // Wi-Fi密码

// WebSocket配置
constexpr char kWsHost[] = "openspeech.bytedance.com";  // WebSocket主机
constexpr uint16_t kWsPort = 443;  // WebSocket端口
constexpr char kWsPath[] = "/api/v3/realtime/dialogue";  // WebSocket路径
constexpr char kWsResourceId[] = "volc.speech.dialog";  // WebSocket资源ID
constexpr char kWsAppId[] = DOUBAO_APP_ID;  // WebSocket应用ID
constexpr char kWsAccessKey[] = DOUBAO_ACCESS_KEY;  // WebSocket访问密钥
constexpr char kWsAppKey[] = "PlgvMymc7f3tQnJ6";  // WebSocket应用密钥

// 语音助手配置
constexpr bool kAutoSayHello = true;  // 是否自动打招呼
constexpr char kHelloText[] = "你好，我是豆包，很高兴和你聊天。";  // 打招呼文本

// 对话配置
constexpr char kBotName[] = "豆包";  // 机器人名称
constexpr char kSystemRole[] =
    "你是一个友好的中文语音助手，请用自然、简洁、口语化的中文回复。";  // 系统角色
constexpr char kSpeakingStyle[] = "语速适中，表达清楚，语气自然。";  // 说话风格
constexpr char kLocationCity[] = "Chongqing";  // 位置城市
constexpr char kSpeaker[] = "zh_female_vv_jupiter_bigtts";  //  speakers
constexpr char kDialogModel[] = "1.2.1.1";  // 对话模型版本
constexpr uint32_t kRecvTimeoutSeconds = 60;  // 接收超时时间（秒）
constexpr bool kStrictAudit = false;  // 是否严格审核

// 麦克风配置
constexpr int kMicWsPin = 41;  // 麦克风WS引脚
constexpr int kMicSdPin = 2;  // 麦克风SD引脚
constexpr int kMicSckPin = 42;  // 麦克风SCK引脚
constexpr i2s_port_t kMicPort = I2S_NUM_1;  // 麦克风I2S端口
constexpr i2s_channel_fmt_t kMicChannelFormat = I2S_CHANNEL_FMT_ONLY_LEFT;  // 麦克风通道格式

// 扬声器配置
constexpr int kSpkDinPin = 6;  // 扬声器DIN引脚
constexpr int kSpkBclkPin = 5;  // 扬声器BCLK引脚
constexpr int kSpkLrcPin = 4;  // 扬声器LRC引脚
constexpr int kSpkAmpEnablePin = 7;  // 扬声器放大器使能引脚
constexpr int kSpkAmpEnableLevel = HIGH;  // 扬声器放大器使能电平
constexpr i2s_port_t kSpkPort = I2S_NUM_0;  // 扬声器I2S端口

// 硬件配置
constexpr int kRgbLedPin = 48;  // RGB LED引脚
constexpr int kAuxSerialRxPin = 16;  // 辅助串口接收引脚
constexpr int kAuxSerialTxPin = 15;  // 辅助串口发送引脚
constexpr uint32_t kAuxSerialBaud = 115200;  // 辅助串口波特率
constexpr uint8_t kApiTriggerByte = 0xEE;  // API触发字节
constexpr size_t kSerialCommandMaxLength = SERIAL_COMMAND_MAX_LENGTH;  // 串口命令最大长度

// OTA配置
constexpr int kOtaEsp32ButtonPin = OTA_ESP32_BUTTON_PIN;  // ESP32 OTA按钮引脚
constexpr int kOtaStm32ButtonPin = OTA_STM32_BUTTON_PIN;  // STM32 OTA按钮引脚
constexpr uint8_t kOtaButtonActiveLevel = OTA_BUTTON_ACTIVE_LEVEL;  // OTA按钮激活电平
constexpr char kOtaServerBaseUrl[] = OTA_SERVER_BASE_URL;  // OTA服务器基础URL
constexpr char kOtaEsp32ManifestUrl[] = OTA_ESP32_MANIFEST_URL;  // ESP32固件清单URL
constexpr char kOtaStm32ManifestUrl[] = OTA_STM32_MANIFEST_URL;  // STM32固件清单URL
constexpr char kOtaEsp32CurrentVersion[] = OTA_ESP32_CURRENT_VERSION;  // ESP32当前固件版本
constexpr char kOtaStm32CurrentVersion[] = OTA_STM32_CURRENT_VERSION;  // STM32当前固件版本
constexpr char kOtaEsp32DefaultUrl[] = OTA_ESP32_DEFAULT_URL;  // ESP32默认固件URL
constexpr char kOtaStm32DefaultUrl[] = OTA_STM32_DEFAULT_URL;  // STM32默认固件URL
constexpr char kOtaEsp32DefaultSha256[] = OTA_ESP32_DEFAULT_SHA256;  // ESP32默认固件SHA256校验和
constexpr char kOtaStm32DefaultSha256[] = OTA_STM32_DEFAULT_SHA256;  // STM32默认固件SHA256校验和

// 音频配置
constexpr uint32_t kInputSampleRate = 16000;  // 输入采样率
constexpr uint32_t kOutputSampleRate = 16000;  // 输出采样率
constexpr uint32_t kAudioFrameMs = 20;  // 音频帧大小（毫秒）

// 音频帧计算
constexpr size_t kInputSamplesPerFrame =
    (kInputSampleRate * kAudioFrameMs) / 1000;  // 每帧输入样本数
constexpr size_t kOutputSamplesPerFrame =
    (kOutputSampleRate * kAudioFrameMs) / 1000;  // 每帧输出样本数
constexpr size_t kTxFrameBytes = kInputSamplesPerFrame * sizeof(int16_t);  // 发送帧字节数
constexpr size_t kRxFrameBytes = kOutputSamplesPerFrame * sizeof(int16_t);  // 接收帧字节数

// WebSocket音频配置
constexpr size_t kAudioFramesPerPacket = 2;  // 每包音频帧数
constexpr uint8_t kAudioPacketsPerLoop = 4;  // 每循环音频包数
constexpr size_t kSessionIdMaxBytes = 48;  // 会话ID最大字节数
constexpr size_t kAudioWsFrameBufferBytes =
    4 + 4 + 4 + kSessionIdMaxBytes + 4 + (kTxFrameBytes * kAudioFramesPerPacket);  // WebSocket音频帧缓冲区大小

// 环形缓冲区配置
constexpr size_t kTxRingBufferBytes = kTxFrameBytes * 64;  // 发送环形缓冲区大小
constexpr size_t kRxRingBufferBytes = kRxFrameBytes * 160;  // 接收环形缓冲区大小
constexpr size_t kPlaybackPrebufferBytes = kRxFrameBytes * 80;  // 播放预缓冲区大小
constexpr size_t kPlaybackResumeBytes = kRxFrameBytes * 48;  // 播放恢复缓冲区大小
constexpr uint32_t kPlaybackStartGraceMs = 3200;  // 播放启动宽限时间（毫秒）
constexpr uint32_t kPlaybackDrainGraceMs = 80;  // 播放排空宽限时间（毫秒）
constexpr uint8_t kPlaybackConcealFrames = 2;  // 播放 conceal 帧数
constexpr uint8_t kPlaybackUnderrunToleranceFrames = 96;  // 播放欠载容忍帧数

// 音量配置
constexpr uint8_t kMicGainPercent = 220;  // 麦克风增益百分比
constexpr uint8_t kSpeakerVolumeDefaultPercent = 60;  // 扬声器默认音量百分比
constexpr uint8_t kSpeakerVolumeMinPercent = 0;  // 扬声器最小音量百分比
constexpr uint8_t kSpeakerVolumeMaxPercent = 200;  // 扬声器最大音量百分比
constexpr uint8_t kSpeakerVolumeStepPercent = 10;  // 扬声器音量步长百分比
constexpr char kVolumePrefsNamespace[] = "audio";  // 音量偏好设置命名空间
constexpr char kVolumePrefsKey[] = "spk_vol";  // 音量偏好设置键

// VAD（语音活动检测）配置
constexpr int kVadStartThreshold = 900;  // VAD启动阈值
constexpr int kVadContinueThreshold = 550;  // VAD继续阈值
constexpr uint8_t kVadStartFrames = 3;  // VAD启动帧数
constexpr uint8_t kVadSilenceFrames = 12;  // VAD静音帧数
constexpr uint32_t kCaptureVadWarmupMs = 350;  // 捕获VAD预热时间（毫秒）

// I2S配置
constexpr int kI2sDmaBufCount = 8;  // I2S DMA缓冲区数量
constexpr int kI2sDmaBufLen = 256;  // I2S DMA缓冲区长度

// 网络配置
constexpr uint32_t kWifiConnectTimeoutMs = 15000;  // Wi-Fi连接超时时间（毫秒）
constexpr uint32_t kWsHandshakeTimeoutMs = 8000;  // WebSocket握手超时时间（毫秒）
constexpr uint32_t kWsKeepAliveMs = 15000;  // WebSocket保持连接时间（毫秒）
constexpr uint32_t kReconnectDelayMs = 3000;  // 重连延迟时间（毫秒）
constexpr uint32_t kMonitorIntervalMs = 5000;  // 监控间隔时间（毫秒）
constexpr uint32_t kAudioStatsIntervalMs = 3000;  // 音频统计间隔时间（毫秒）

// OTA配置
constexpr uint32_t kOtaHttpTimeoutMs = OTA_HTTP_TIMEOUT_MS;  // OTA HTTP超时时间（毫秒）
constexpr size_t kOtaHttpBufferBytes = OTA_HTTP_BUFFER_BYTES;  // OTA HTTP缓冲区大小（字节）
constexpr size_t kOtaManifestJsonBytes = OTA_MANIFEST_JSON_BYTES;  // OTA清单JSON大小（字节）
constexpr uint32_t kOtaProgressIntervalMs = OTA_PROGRESS_INTERVAL_MS;  // OTA进度报告间隔（毫秒）
constexpr uint32_t kOtaAutoRetryIntervalMs = OTA_AUTO_RETRY_INTERVAL_MS;  // OTA自动重试间隔（毫秒）
constexpr uint32_t kOtaRequestWindowMs = OTA_REQUEST_WINDOW_MS;  // OTA请求窗口时间（毫秒）
constexpr uint32_t kOtaButtonDebounceMs = OTA_BUTTON_DEBOUNCE_MS;  // OTA按钮去抖时间（毫秒）

// 任务配置
constexpr UBaseType_t kAudioTaskPriority = 18;  // 音频任务优先级
constexpr BaseType_t kAudioTaskCore = 1;  // 音频任务核心
constexpr uint32_t kAudioTaskStack = 8192;  // 音频任务栈大小

// WebSocket配置
constexpr size_t kWsReceiveFrameSlack = 1024;  // WebSocket接收帧 slack
constexpr size_t kWsSendChunkBytes = 512;  // WebSocket发送块大小（字节）
constexpr uint8_t kMaxFramesPerLoop = 24;  // 每循环最大帧数

// 日志配置
constexpr uint8_t kLogLevelError = 0;  // 错误日志级别
constexpr uint8_t kLogLevelWarn = 1;  // 警告日志级别
constexpr uint8_t kLogLevelInfo = 2;  // 信息日志级别
constexpr uint8_t kLogLevelDebug = 3;  // 调试日志级别
constexpr uint8_t kActiveLogLevel = kLogLevelInfo;  // 当前活动日志级别

// STM32配置
constexpr uint32_t kStm32BootloaderBaud = STM32_BOOTLOADER_BAUD;  // STM32 bootloader波特率
constexpr uint32_t kStm32FlashBaseAddress = STM32_FLASH_BASE_ADDRESS;  // STM32闪存基地址
constexpr size_t kStm32FlashSizeBytes = STM32_FLASH_SIZE_BYTES;  // STM32闪存大小（字节）
constexpr uint16_t kStm32ExpectedDeviceId = STM32_EXPECTED_DEVICE_ID;  // 期望的STM32设备ID
constexpr uint8_t kStm32StopCommandByte = STM32_STOP_COMMAND_BYTE;  // STM32停止命令字节

// STM32引脚配置
constexpr int kStm32Boot0Pin = STM32_BOOT0_PIN;  // STM32 BOOT0引脚
constexpr int kStm32Boot1Pin = STM32_BOOT1_PIN;  // STM32 BOOT1引脚
constexpr int kStm32ResetPin = STM32_NRST_PIN;  // STM32 NRST引脚
constexpr uint8_t kStm32Boot0BootloaderLevel = STM32_BOOT0_BOOTLOADER_LEVEL;  // STM32 BOOT0 bootloader电平
constexpr uint8_t kStm32Boot0AppLevel = STM32_BOOT0_APP_LEVEL;  // STM32 BOOT0应用电平
constexpr uint8_t kStm32Boot1BootloaderLevel = STM32_BOOT1_BOOTLOADER_LEVEL;  // STM32 BOOT1 bootloader电平
constexpr uint8_t kStm32Boot1AppLevel = STM32_BOOT1_APP_LEVEL;  // STM32 BOOT1应用电平
constexpr uint8_t kStm32ResetAssertLevel = STM32_NRST_ASSERT_LEVEL;  // STM32 NRST断言电平
constexpr uint8_t kStm32ResetDeassertLevel = STM32_NRST_DEASSERT_LEVEL;  // STM32 NRST去断言电平

// STM32超时配置
constexpr uint32_t kStm32BootSyncTimeoutMs = STM32_BOOT_SYNC_TIMEOUT_MS;  // STM32启动同步超时时间（毫秒）
constexpr uint32_t kStm32CommandTimeoutMs = STM32_COMMAND_TIMEOUT_MS;  // STM32命令超时时间（毫秒）
constexpr uint32_t kStm32EraseTimeoutMs = STM32_ERASE_TIMEOUT_MS;  // STM32擦除超时时间（毫秒）
constexpr uint32_t kStm32WriteTimeoutMs = STM32_WRITE_TIMEOUT_MS;  // STM32写入超时时间（毫秒）
constexpr uint32_t kStm32VerifyTimeoutMs = STM32_VERIFY_TIMEOUT_MS;  // STM32验证超时时间（毫秒）
constexpr uint32_t kStm32ResetPulseMs = STM32_RESET_PULSE_MS;  // STM32复位脉冲时间（毫秒）
constexpr uint32_t kStm32BootSettleMs = STM32_BOOT_SETTLE_MS;  // STM32启动稳定时间（毫秒）

}  // namespace app
