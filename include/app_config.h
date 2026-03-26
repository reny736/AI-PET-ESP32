#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

#ifndef WIFI_SSID
#define WIFI_SSID "STARLINK"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "A413@j00"
#endif

#ifndef DOUBAO_APP_ID
#define DOUBAO_APP_ID "9603609268"
#endif

#ifndef DOUBAO_ACCESS_KEY
#define DOUBAO_ACCESS_KEY "-i32Wp6ERT_osX7WKgp6AkwPXiMBilIi"
#endif

#ifndef OTA_HTTP_TIMEOUT_MS
#define OTA_HTTP_TIMEOUT_MS 15000
#endif

#ifndef OTA_SERVER_BASE_URL
#define OTA_SERVER_BASE_URL "http://172.20.10.2:8080"
#endif

#ifndef OTA_ESP32_MANIFEST_URL
#define OTA_ESP32_MANIFEST_URL OTA_SERVER_BASE_URL "/ESP32_OTA_version.json"
#endif

#ifndef OTA_STM32_MANIFEST_URL
#define OTA_STM32_MANIFEST_URL OTA_SERVER_BASE_URL "/STM32_OTA_version.json"
#endif

#ifndef OTA_ESP32_CURRENT_VERSION
#define OTA_ESP32_CURRENT_VERSION "V1.0"
#endif

#ifndef OTA_STM32_CURRENT_VERSION
#define OTA_STM32_CURRENT_VERSION "V1.0"
#endif

#ifndef OTA_ESP32_DEFAULT_URL
#define OTA_ESP32_DEFAULT_URL OTA_SERVER_BASE_URL "/ESP32_firmware_v2.0.bin"
#endif

#ifndef OTA_STM32_DEFAULT_URL
#define OTA_STM32_DEFAULT_URL OTA_SERVER_BASE_URL "/STM32_hal10_V2.0.bin"
#endif

#ifndef OTA_ESP32_DEFAULT_SHA256
#define OTA_ESP32_DEFAULT_SHA256 ""
#endif

#ifndef OTA_STM32_DEFAULT_SHA256
#define OTA_STM32_DEFAULT_SHA256 ""
#endif

#ifndef OTA_HTTP_BUFFER_BYTES
#define OTA_HTTP_BUFFER_BYTES 1024
#endif

#ifndef OTA_MANIFEST_JSON_BYTES
#define OTA_MANIFEST_JSON_BYTES 1536
#endif

#ifndef OTA_PROGRESS_INTERVAL_MS
#define OTA_PROGRESS_INTERVAL_MS 1000
#endif

#ifndef OTA_AUTO_RETRY_INTERVAL_MS
#define OTA_AUTO_RETRY_INTERVAL_MS 1000
#endif

#ifndef OTA_REQUEST_WINDOW_MS
#define OTA_REQUEST_WINDOW_MS 5000
#endif

#ifndef OTA_BUTTON_DEBOUNCE_MS
#define OTA_BUTTON_DEBOUNCE_MS 30
#endif

#ifndef OTA_BUTTON_ACTIVE_LEVEL
#define OTA_BUTTON_ACTIVE_LEVEL LOW
#endif

#ifndef OTA_ESP32_BUTTON_PIN
#define OTA_ESP32_BUTTON_PIN 38
#endif

#ifndef OTA_STM32_BUTTON_PIN
#define OTA_STM32_BUTTON_PIN 47
#endif

#ifndef STM32_STOP_COMMAND_BYTE
#define STM32_STOP_COMMAND_BYTE 0xDD
#endif

#ifndef SERIAL_COMMAND_MAX_LENGTH
#define SERIAL_COMMAND_MAX_LENGTH 512
#endif

#ifndef STM32_BOOTLOADER_BAUD
#define STM32_BOOTLOADER_BAUD 115200
#endif

#ifndef STM32_FLASH_BASE_ADDRESS
#define STM32_FLASH_BASE_ADDRESS 0x08000000UL
#endif

#ifndef STM32_FLASH_SIZE_BYTES
#define STM32_FLASH_SIZE_BYTES (64UL * 1024UL)
#endif

#ifndef STM32_EXPECTED_DEVICE_ID
#define STM32_EXPECTED_DEVICE_ID 0x0410
#endif

#ifndef STM32_BOOT0_PIN
#define STM32_BOOT0_PIN -1
#endif

#ifndef STM32_BOOT1_PIN
#define STM32_BOOT1_PIN -1
#endif

#ifndef STM32_NRST_PIN
#define STM32_NRST_PIN -1
#endif

#ifndef STM32_BOOT0_BOOTLOADER_LEVEL
#define STM32_BOOT0_BOOTLOADER_LEVEL HIGH
#endif

#ifndef STM32_BOOT0_APP_LEVEL
#define STM32_BOOT0_APP_LEVEL LOW
#endif

#ifndef STM32_BOOT1_BOOTLOADER_LEVEL
#define STM32_BOOT1_BOOTLOADER_LEVEL LOW
#endif

#ifndef STM32_BOOT1_APP_LEVEL
#define STM32_BOOT1_APP_LEVEL LOW
#endif

#ifndef STM32_NRST_ASSERT_LEVEL
#define STM32_NRST_ASSERT_LEVEL LOW
#endif

#ifndef STM32_NRST_DEASSERT_LEVEL
#define STM32_NRST_DEASSERT_LEVEL HIGH
#endif

#ifndef STM32_BOOT_SYNC_TIMEOUT_MS
#define STM32_BOOT_SYNC_TIMEOUT_MS 1200
#endif

#ifndef STM32_COMMAND_TIMEOUT_MS
#define STM32_COMMAND_TIMEOUT_MS 1500
#endif

#ifndef STM32_ERASE_TIMEOUT_MS
#define STM32_ERASE_TIMEOUT_MS 30000
#endif

#ifndef STM32_WRITE_TIMEOUT_MS
#define STM32_WRITE_TIMEOUT_MS 3000
#endif

#ifndef STM32_VERIFY_TIMEOUT_MS
#define STM32_VERIFY_TIMEOUT_MS 3000
#endif

#ifndef STM32_RESET_PULSE_MS
#define STM32_RESET_PULSE_MS 80
#endif

#ifndef STM32_BOOT_SETTLE_MS
#define STM32_BOOT_SETTLE_MS 120
#endif

namespace app {

constexpr char kWifiSsid[] = WIFI_SSID;
constexpr char kWifiPassword[] = WIFI_PASSWORD;

constexpr char kWsHost[] = "openspeech.bytedance.com";
constexpr uint16_t kWsPort = 443;
constexpr char kWsPath[] = "/api/v3/realtime/dialogue";
constexpr char kWsResourceId[] = "volc.speech.dialog";
constexpr char kWsAppId[] = DOUBAO_APP_ID;
constexpr char kWsAccessKey[] = DOUBAO_ACCESS_KEY;
constexpr char kWsAppKey[] = "PlgvMymc7f3tQnJ6";

constexpr bool kAutoSayHello = true;
constexpr char kHelloText[] = "你好，我是豆包，很高兴和你聊天。";

constexpr char kBotName[] = "豆包";
constexpr char kSystemRole[] =
    "你是一个友好的中文语音助手，请用自然、简洁、口语化的中文回复。";
constexpr char kSpeakingStyle[] = "语速适中，表达清楚，语气自然。";
constexpr char kLocationCity[] = "Chongqing";
constexpr char kSpeaker[] = "zh_female_vv_jupiter_bigtts";
constexpr char kDialogModel[] = "1.2.1.1";
constexpr uint32_t kRecvTimeoutSeconds = 60;
constexpr bool kStrictAudit = false;

constexpr int kMicWsPin = 41;
constexpr int kMicSdPin = 2;
constexpr int kMicSckPin = 42;
constexpr i2s_port_t kMicPort = I2S_NUM_1;
constexpr i2s_channel_fmt_t kMicChannelFormat = I2S_CHANNEL_FMT_ONLY_LEFT;

constexpr int kSpkDinPin = 6;
constexpr int kSpkBclkPin = 5;
constexpr int kSpkLrcPin = 4;
constexpr int kSpkAmpEnablePin = 7;
constexpr int kSpkAmpEnableLevel = HIGH;
constexpr i2s_port_t kSpkPort = I2S_NUM_0;

constexpr int kRgbLedPin = 48;
constexpr int kAuxSerialRxPin = 16;
constexpr int kAuxSerialTxPin = 15;
constexpr uint32_t kAuxSerialBaud = 115200;
constexpr uint8_t kApiTriggerByte = 0xEE;
constexpr size_t kSerialCommandMaxLength = SERIAL_COMMAND_MAX_LENGTH;
constexpr int kOtaEsp32ButtonPin = OTA_ESP32_BUTTON_PIN;
constexpr int kOtaStm32ButtonPin = OTA_STM32_BUTTON_PIN;
constexpr uint8_t kOtaButtonActiveLevel = OTA_BUTTON_ACTIVE_LEVEL;
constexpr char kOtaServerBaseUrl[] = OTA_SERVER_BASE_URL;
constexpr char kOtaEsp32ManifestUrl[] = OTA_ESP32_MANIFEST_URL;
constexpr char kOtaStm32ManifestUrl[] = OTA_STM32_MANIFEST_URL;
constexpr char kOtaEsp32CurrentVersion[] = OTA_ESP32_CURRENT_VERSION;
constexpr char kOtaStm32CurrentVersion[] = OTA_STM32_CURRENT_VERSION;
constexpr char kOtaEsp32DefaultUrl[] = OTA_ESP32_DEFAULT_URL;
constexpr char kOtaStm32DefaultUrl[] = OTA_STM32_DEFAULT_URL;
constexpr char kOtaEsp32DefaultSha256[] = OTA_ESP32_DEFAULT_SHA256;
constexpr char kOtaStm32DefaultSha256[] = OTA_STM32_DEFAULT_SHA256;

constexpr uint32_t kInputSampleRate = 16000;
constexpr uint32_t kOutputSampleRate = 16000;
constexpr uint32_t kAudioFrameMs = 20;

constexpr size_t kInputSamplesPerFrame =
    (kInputSampleRate * kAudioFrameMs) / 1000;
constexpr size_t kOutputSamplesPerFrame =
    (kOutputSampleRate * kAudioFrameMs) / 1000;
constexpr size_t kTxFrameBytes = kInputSamplesPerFrame * sizeof(int16_t);
constexpr size_t kRxFrameBytes = kOutputSamplesPerFrame * sizeof(int16_t);

constexpr size_t kAudioFramesPerPacket = 2;
constexpr uint8_t kAudioPacketsPerLoop = 4;
constexpr size_t kSessionIdMaxBytes = 48;
constexpr size_t kAudioWsFrameBufferBytes =
    4 + 4 + 4 + kSessionIdMaxBytes + 4 + (kTxFrameBytes * kAudioFramesPerPacket);

constexpr size_t kTxRingBufferBytes = kTxFrameBytes * 64;
constexpr size_t kRxRingBufferBytes = kRxFrameBytes * 160;
constexpr size_t kPlaybackPrebufferBytes = kRxFrameBytes * 80;
constexpr size_t kPlaybackResumeBytes = kRxFrameBytes * 48;
constexpr uint32_t kPlaybackStartGraceMs = 3200;
constexpr uint32_t kPlaybackDrainGraceMs = 80;
constexpr uint8_t kPlaybackConcealFrames = 2;
constexpr uint8_t kPlaybackUnderrunToleranceFrames = 96;

constexpr uint8_t kMicGainPercent = 220;
constexpr uint8_t kSpeakerVolumeDefaultPercent = 60;
constexpr uint8_t kSpeakerVolumeMinPercent = 0;
constexpr uint8_t kSpeakerVolumeMaxPercent = 200;
constexpr uint8_t kSpeakerVolumeStepPercent = 10;
constexpr char kVolumePrefsNamespace[] = "audio";
constexpr char kVolumePrefsKey[] = "spk_vol";

constexpr int kVadStartThreshold = 900;
constexpr int kVadContinueThreshold = 550;
constexpr uint8_t kVadStartFrames = 3;
constexpr uint8_t kVadSilenceFrames = 12;
constexpr uint32_t kCaptureVadWarmupMs = 350;

constexpr int kI2sDmaBufCount = 8;
constexpr int kI2sDmaBufLen = 256;

constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kWsHandshakeTimeoutMs = 8000;
constexpr uint32_t kWsKeepAliveMs = 15000;
constexpr uint32_t kReconnectDelayMs = 3000;
constexpr uint32_t kMonitorIntervalMs = 5000;
constexpr uint32_t kAudioStatsIntervalMs = 3000;
constexpr uint32_t kOtaHttpTimeoutMs = OTA_HTTP_TIMEOUT_MS;
constexpr size_t kOtaHttpBufferBytes = OTA_HTTP_BUFFER_BYTES;
constexpr size_t kOtaManifestJsonBytes = OTA_MANIFEST_JSON_BYTES;
constexpr uint32_t kOtaProgressIntervalMs = OTA_PROGRESS_INTERVAL_MS;
constexpr uint32_t kOtaAutoRetryIntervalMs = OTA_AUTO_RETRY_INTERVAL_MS;
constexpr uint32_t kOtaRequestWindowMs = OTA_REQUEST_WINDOW_MS;
constexpr uint32_t kOtaButtonDebounceMs = OTA_BUTTON_DEBOUNCE_MS;

constexpr UBaseType_t kAudioTaskPriority = 18;
constexpr BaseType_t kAudioTaskCore = 1;
constexpr uint32_t kAudioTaskStack = 8192;

constexpr size_t kWsReceiveFrameSlack = 1024;
constexpr size_t kWsSendChunkBytes = 512;
constexpr uint8_t kMaxFramesPerLoop = 24;

constexpr uint8_t kLogLevelError = 0;
constexpr uint8_t kLogLevelWarn = 1;
constexpr uint8_t kLogLevelInfo = 2;
constexpr uint8_t kLogLevelDebug = 3;
constexpr uint8_t kActiveLogLevel = kLogLevelInfo;

constexpr uint32_t kStm32BootloaderBaud = STM32_BOOTLOADER_BAUD;
constexpr uint32_t kStm32FlashBaseAddress = STM32_FLASH_BASE_ADDRESS;
constexpr size_t kStm32FlashSizeBytes = STM32_FLASH_SIZE_BYTES;
constexpr uint16_t kStm32ExpectedDeviceId = STM32_EXPECTED_DEVICE_ID;
constexpr uint8_t kStm32StopCommandByte = STM32_STOP_COMMAND_BYTE;

constexpr int kStm32Boot0Pin = STM32_BOOT0_PIN;
constexpr int kStm32Boot1Pin = STM32_BOOT1_PIN;
constexpr int kStm32ResetPin = STM32_NRST_PIN;
constexpr uint8_t kStm32Boot0BootloaderLevel = STM32_BOOT0_BOOTLOADER_LEVEL;
constexpr uint8_t kStm32Boot0AppLevel = STM32_BOOT0_APP_LEVEL;
constexpr uint8_t kStm32Boot1BootloaderLevel = STM32_BOOT1_BOOTLOADER_LEVEL;
constexpr uint8_t kStm32Boot1AppLevel = STM32_BOOT1_APP_LEVEL;
constexpr uint8_t kStm32ResetAssertLevel = STM32_NRST_ASSERT_LEVEL;
constexpr uint8_t kStm32ResetDeassertLevel = STM32_NRST_DEASSERT_LEVEL;

constexpr uint32_t kStm32BootSyncTimeoutMs = STM32_BOOT_SYNC_TIMEOUT_MS;
constexpr uint32_t kStm32CommandTimeoutMs = STM32_COMMAND_TIMEOUT_MS;
constexpr uint32_t kStm32EraseTimeoutMs = STM32_ERASE_TIMEOUT_MS;
constexpr uint32_t kStm32WriteTimeoutMs = STM32_WRITE_TIMEOUT_MS;
constexpr uint32_t kStm32VerifyTimeoutMs = STM32_VERIFY_TIMEOUT_MS;
constexpr uint32_t kStm32ResetPulseMs = STM32_RESET_PULSE_MS;
constexpr uint32_t kStm32BootSettleMs = STM32_BOOT_SETTLE_MS;

}  // namespace app
