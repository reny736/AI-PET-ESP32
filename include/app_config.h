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

}  // namespace app
