#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>

#include "app_config.h"
#include "ring_buffer.h"

/**
 * @file audio_pipeline.h
 * @brief 音频处理管道
 * @details 负责音频捕获、播放和处理的核心类，包括麦克风输入、扬声器输出、语音活动检测等功能
 */

/**
 * @enum AudioMode
 * @brief 音频模式枚举
 */
enum class AudioMode {
    Idle,        // 空闲模式
    Capturing,   // 捕获模式
    Prebuffering, // 预缓冲模式
    Playing      // 播放模式
};

/**
 * @struct AudioStats
 * @brief 音频统计信息
 */
struct AudioStats {
    uint32_t captured_frames = 0;  // 捕获的帧数
    uint32_t played_frames = 0;     // 播放的帧数
    uint32_t tx_overflows = 0;      // 发送缓冲区溢出次数
    uint32_t rx_overflows = 0;      // 接收缓冲区溢出次数
    uint32_t rx_underruns = 0;      // 接收缓冲区欠载次数
    int last_mic_rms = 0;           // 最后一次麦克风RMS值
    size_t tx_buffered_bytes = 0;    // 发送缓冲区已用字节数
    size_t rx_buffered_bytes = 0;    // 接收缓冲区已用字节数
};

/**
 * @class AudioPipeline
 * @brief 音频处理管道类
 * @details 负责音频的捕获、播放和处理，包括麦克风数据采集、语音活动检测、音频播放等功能
 */
class AudioPipeline {
public:
    /**
     * @typedef SpeechCallback
     * @brief 语音活动回调函数类型
     * @param active 是否检测到语音活动
     */
    using SpeechCallback = std::function<void(bool)>;

    /**
     * @brief 构造函数
     */
    AudioPipeline();
    
    /**
     * @brief 析构函数
     */
    ~AudioPipeline();

    /**
     * @brief 初始化音频管道
     * @return 初始化成功返回true，失败返回false
     */
    bool begin();
    
    /**
     * @brief 释放音频管道资源
     */
    void end();

    /**
     * @brief 设置语音活动回调函数
     * @param callback 回调函数
     */
    void setSpeechCallback(SpeechCallback callback) { speech_callback_ = callback; }

    /**
     * @brief 开始捕获音频
     */
    void startCapture();
    
    /**
     * @brief 停止捕获音频
     */
    void stopCapture();
    
    /**
     * @brief 开始播放音频
     */
    void startPlayback();
    
    /**
     * @brief 停止播放音频
     * @param clear_buffer 是否清空缓冲区
     */
    void stopPlayback(bool clear_buffer = true);
    
    /**
     * @brief 标记播放流结束
     */
    void markPlaybackStreamEnded();
    
    /**
     * @brief 设置扬声器音量百分比
     * @param volume_percent 音量百分比（0-200）
     */
    void setSpeakerVolumePercent(uint8_t volume_percent);
    
    /**
     * @brief 获取扬声器音量百分比
     * @return 音量百分比
     */
    uint8_t speakerVolumePercent() const { return speaker_volume_percent_; }

    /**
     * @brief 清空捕获缓冲区
     */
    void clearCaptureBuffer();
    
    /**
     * @brief 清空播放缓冲区
     */
    void clearPlaybackBuffer();

    /**
     * @brief 读取捕获的音频数据
     * @param data 数据缓冲区
     * @param max_len 最大读取长度
     * @return 实际读取的长度
     */
    size_t readCaptureAudio(uint8_t* data, size_t max_len);
    
    /**
     * @brief 获取捕获缓冲区已用字节数
     * @return 已用字节数
     */
    size_t captureBufferedBytes();

    /**
     * @brief 推送播放音频数据
     * @param data 数据指针
     * @param len 数据长度
     * @return 推送成功返回true，失败返回false
     */
    bool pushPlaybackAudio(const uint8_t* data, size_t len);
    
    /**
     * @brief 获取播放缓冲区已用字节数
     * @return 已用字节数
     */
    size_t playbackBufferedBytes();

    /**
     * @brief 获取当前音频模式
     * @return 音频模式
     */
    AudioMode mode() const { return mode_; }
    
    /**
     * @brief 检查是否正在捕获
     * @return 正在捕获返回true，否则返回false
     */
    bool isCapturing() const { return mode_ == AudioMode::Capturing; }
    
    /**
     * @brief 检查是否正在播放
     * @return 正在播放返回true，否则返回false
     */
    bool isPlaying() const {
        return mode_ == AudioMode::Playing || mode_ == AudioMode::Prebuffering;
    }

    /**
     * @brief 获取音频统计信息
     * @return 音频统计信息
     */
    AudioStats stats();

private:
    /**
     * @brief 音频任务入口函数
     * @param arg 任务参数
     */
    static void audioTaskEntry(void* arg);
    
    /**
     * @brief 音频任务主函数
     */
    void audioTask();

    /**
     * @brief 初始化输入I2S
     * @return 初始化成功返回true，失败返回false
     */
    bool initInputI2s();
    
    /**
     * @brief 初始化输出I2S
     * @return 初始化成功返回true，失败返回false
     */
    bool initOutputI2s();
    
    /**
     * @brief 分配缓冲区
     * @return 分配成功返回true，失败返回false
     */
    bool allocateBuffers();
    
    /**
     * @brief 释放缓冲区
     */
    void freeBuffers();

    /**
     * @brief 处理捕获帧
     */
    void processCaptureFrame();
    
    /**
     * @brief 处理播放帧
     */
    void processPlaybackFrame();
    
    /**
     * @brief 构建conceal帧
     * @param conceal_index conceal索引
     */
    void buildConcealFrame(uint8_t conceal_index);
    
    /**
     * @brief 计算RMS值
     * @param samples 样本数据
     * @param count 样本数量
     * @return RMS值
     */
    int calculateRms(const int16_t* samples, size_t count) const;
    
    /**
     * @brief 应用音量
     * @param samples 样本数据
     * @param count 样本数量
     */
    void applyVolume(int16_t* samples, size_t count);
    
    /**
     * @brief 设置扬声器放大器使能状态
     * @param enabled 是否使能
     */
    void setSpeakerAmpEnabled(bool enabled);

    RingBuffer tx_ring_;  // 发送环形缓冲区
    RingBuffer rx_ring_;  // 接收环形缓冲区

    TaskHandle_t task_handle_;  // 任务句柄
    AudioMode mode_;  // 音频模式
    SpeechCallback speech_callback_;  // 语音活动回调函数
    AudioStats stats_;  // 音频统计信息

    int32_t* mic_raw_frame_;  // 麦克风原始帧
    int16_t* mic_pcm_frame_;  // 麦克风PCM帧
    int16_t* playback_frame_;  // 播放帧
    int16_t* silence_frame_;  // 静音帧

    bool running_;  // 是否运行中
    bool voice_active_;  // 语音是否活动
    bool require_full_prebuffer_;  // 是否需要完整预缓冲
    bool playback_stream_ended_;  // 播放流是否结束
    volatile uint8_t speaker_volume_percent_;  // 扬声器音量百分比
    uint8_t speech_frames_;  // 语音帧数
    uint8_t silence_frames_;  // 静音帧数
    uint8_t underrun_frames_;  // 欠载帧数
    uint16_t playback_fade_samples_remaining_;  // 播放淡入淡出剩余样本数
    uint32_t capture_started_ms_;  // 捕获开始时间
    uint32_t playback_prebuffer_started_ms_;  // 播放预缓冲开始时间
    uint32_t last_audio_log_ms_;  // 最后一次音频日志时间
};
