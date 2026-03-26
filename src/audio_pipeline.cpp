#include "audio_pipeline.h"

#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#include "logger.h"

namespace {

/**
 * @brief 分配PSRAM内存
 * @param size 所需内存大小
 * @return 分配的内存指针，失败返回nullptr
 * @note 优先尝试从PSRAM分配内存，失败则使用普通内存
 */
void* allocPsram(size_t size) {
    // 尝试从PSRAM分配内存，要求8位对齐
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == nullptr) {
        // PSRAM分配失败，尝试普通内存分配
        ptr = malloc(size);
    }
    return ptr;
}

}  // namespace

/**
 * @brief 音频管道构造函数
 */
AudioPipeline::AudioPipeline()
    : tx_ring_(app::kTxRingBufferBytes),           // 发送环形缓冲区
      rx_ring_(app::kRxRingBufferBytes),           // 接收环形缓冲区
      task_handle_(nullptr),                       // 任务句柄
      mode_(AudioMode::Idle),                      // 初始状态为空闲
      mic_raw_frame_(nullptr),                     // 麦克风原始帧缓冲区
      mic_pcm_frame_(nullptr),                     // 麦克风PCM帧缓冲区
      playback_frame_(nullptr),                    // 播放帧缓冲区
      silence_frame_(nullptr),                     // 静音帧缓冲区
      running_(false),                             // 运行状态标记
      voice_active_(false),                        // 语音活动标记
      require_full_prebuffer_(false),              // 是否需要完整预缓冲
      playback_stream_ended_(false),               // 播放流结束标记
      speaker_volume_percent_(app::kSpeakerVolumeDefaultPercent),  // 默认音量百分比
      speech_frames_(0),                           // 语音帧数
      silence_frames_(0),                          // 静音帧数
      underrun_frames_(0),                         // 欠载帧数
      playback_fade_samples_remaining_(0),         // 播放淡入淡出剩余样本数
      capture_started_ms_(0),                      // 捕获开始时间
      playback_prebuffer_started_ms_(0),           // 播放预缓冲开始时间
      last_audio_log_ms_(0) {                     // 最后一次音频日志时间
}

/**
 * @brief 音频管道析构函数
 * @note 调用end()方法释放资源
 */
AudioPipeline::~AudioPipeline() {
    end();
}

/**
 * @brief 初始化音频管道
 * @return 初始化成功返回true，失败返回false
 */
bool AudioPipeline::begin() {
    // 如果已经运行，直接返回成功
    if (running_) {
        return true;
    }

    // 初始化环形缓冲区
    if (!tx_ring_.begin() || !rx_ring_.begin()) {
        return false;
    }

    // 分配音频缓冲区
    if (!allocateBuffers()) {
        return false;
    }

    // 初始化扬声器放大器引脚
    if (app::kSpkAmpEnablePin >= 0) {
        pinMode(app::kSpkAmpEnablePin, OUTPUT);
        setSpeakerAmpEnabled(false);
    }

    // 初始化输入和输出I2S
    if (!initInputI2s() || !initOutputI2s()) {
        return false;
    }

    // 停止I2S端口
    i2s_stop(app::kMicPort);
    i2s_stop(app::kSpkPort);

    // 创建音频任务
    running_ = true;
    if (xTaskCreatePinnedToCore(
            &AudioPipeline::audioTaskEntry,
            "AudioPipe",
            app::kAudioTaskStack,
            this,
            app::kAudioTaskPriority,
            &task_handle_,
            app::kAudioTaskCore) != pdPASS) {
        running_ = false;
        LOGE("AUDIO", "Failed to create audio task");
        return false;
    }

    LOGI("AUDIO", "Audio pipeline ready (%u ms frames)", static_cast<unsigned>(app::kAudioFrameMs));
    return true;
}

/**
 * @brief 释放音频管道资源
 */
void AudioPipeline::end() {
    // 停止运行
    running_ = false;

    // 删除音频任务
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }

    // 停止并卸载I2S驱动
    i2s_stop(app::kMicPort);
    i2s_stop(app::kSpkPort);
    i2s_driver_uninstall(app::kMicPort);
    i2s_driver_uninstall(app::kSpkPort);

    // 禁用扬声器放大器
    setSpeakerAmpEnabled(false);
    // 释放缓冲区
    freeBuffers();
    // 释放环形缓冲区
    tx_ring_.end();
    rx_ring_.end();
    // 重置状态
    mode_ = AudioMode::Idle;
}

/**
 * @brief 分配音频缓冲区
 * @return 分配成功返回true，失败返回false
 */
bool AudioPipeline::allocateBuffers() {
    // 分配麦克风原始帧缓冲区
    mic_raw_frame_ = static_cast<int32_t*>(
        allocPsram(app::kInputSamplesPerFrame * sizeof(int32_t)));
    // 分配麦克风PCM帧缓冲区
    mic_pcm_frame_ = static_cast<int16_t*>(
        allocPsram(app::kInputSamplesPerFrame * sizeof(int16_t)));
    // 分配播放帧缓冲区
    playback_frame_ = static_cast<int16_t*>(
        allocPsram(app::kOutputSamplesPerFrame * sizeof(int16_t)));
    // 分配静音帧缓冲区
    silence_frame_ = static_cast<int16_t*>(
        allocPsram(app::kOutputSamplesPerFrame * sizeof(int16_t)));

    // 检查缓冲区分配是否成功
    if (mic_raw_frame_ == nullptr || mic_pcm_frame_ == nullptr ||
        playback_frame_ == nullptr || silence_frame_ == nullptr) {
        LOGE("AUDIO", "Failed to allocate audio buffers");
        return false;
    }

    // 初始化缓冲区为0
    memset(playback_frame_, 0, app::kRxFrameBytes);
    memset(silence_frame_, 0, app::kRxFrameBytes);
    return true;
}

/**
 * @brief 释放音频缓冲区
 */
void AudioPipeline::freeBuffers() {
    // 释放麦克风原始帧缓冲区
    if (mic_raw_frame_ != nullptr) {
        free(mic_raw_frame_);
        mic_raw_frame_ = nullptr;
    }
    // 释放麦克风PCM帧缓冲区
    if (mic_pcm_frame_ != nullptr) {
        free(mic_pcm_frame_);
        mic_pcm_frame_ = nullptr;
    }
    // 释放播放帧缓冲区
    if (playback_frame_ != nullptr) {
        free(playback_frame_);
        playback_frame_ = nullptr;
    }
    // 释放静音帧缓冲区
    if (silence_frame_ != nullptr) {
        free(silence_frame_);
        silence_frame_ = nullptr;
    }
}

/**
 * @brief 初始化输入I2S（麦克风）
 * @return 初始化成功返回true，失败返回false
 */
bool AudioPipeline::initInputI2s() {
    // 配置I2S
    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);  // 主模式，接收
    config.sample_rate = app::kInputSampleRate;  // 采样率
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;  // 每样本位数
    config.channel_format = app::kMicChannelFormat;  // 通道格式
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;  // 通信格式
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;  // 中断分配标志
    config.dma_buf_count = app::kI2sDmaBufCount;  // DMA缓冲区数量
    config.dma_buf_len = app::kI2sDmaBufLen;  // DMA缓冲区长度
    config.use_apll = false;  // 不使用APLL
    config.tx_desc_auto_clear = false;  // 不自动清除TX描述符
    config.fixed_mclk = 0;  // 固定MCLK
    config.mclk_multiple = I2S_MCLK_MULTIPLE_256;  // MCLK倍数
    config.bits_per_chan = I2S_BITS_PER_CHAN_32BIT;  // 每通道位数

    // 安装I2S驱动
    esp_err_t err = i2s_driver_install(app::kMicPort, &config, 0, nullptr);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LOGE("AUDIO", "Mic I2S install failed: %d", err);
        return false;
    }

    // 配置I2S引脚
    i2s_pin_config_t pin_config = {};
    pin_config.bck_io_num = app::kMicSckPin;  // BCK引脚
    pin_config.ws_io_num = app::kMicWsPin;  // WS引脚
    pin_config.data_out_num = I2S_PIN_NO_CHANGE;  // 数据输出引脚不变
    pin_config.data_in_num = app::kMicSdPin;  // 数据输入引脚

    // 设置I2S引脚
    err = i2s_set_pin(app::kMicPort, &pin_config);
    if (err != ESP_OK) {
        LOGE("AUDIO", "Mic I2S pin config failed: %d", err);
        return false;
    }

    // 清零DMA缓冲区
    i2s_zero_dma_buffer(app::kMicPort);
    return true;
}

/**
 * @brief 初始化输出I2S（扬声器）
 * @return 初始化成功返回true，失败返回false
 */
bool AudioPipeline::initOutputI2s() {
    // 配置I2S
    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);  // 主模式，发送
    config.sample_rate = app::kOutputSampleRate;  // 采样率
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;  // 每样本位数
    config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;  // 通道格式
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;  // 通信格式
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;  // 中断分配标志
    config.dma_buf_count = app::kI2sDmaBufCount;  // DMA缓冲区数量
    config.dma_buf_len = app::kI2sDmaBufLen;  // DMA缓冲区长度
    config.use_apll = true;  // 使用APLL
    config.tx_desc_auto_clear = true;  // 自动清除TX描述符
    config.fixed_mclk = 0;  // 固定MCLK
    config.mclk_multiple = I2S_MCLK_MULTIPLE_256;  // MCLK倍数
    config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;  // 每通道位数

    // 安装I2S驱动
    esp_err_t err = i2s_driver_install(app::kSpkPort, &config, 0, nullptr);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LOGE("AUDIO", "Speaker I2S install failed: %d", err);
        return false;
    }

    // 配置I2S引脚
    i2s_pin_config_t pin_config = {};
    pin_config.bck_io_num = app::kSpkBclkPin;  // BCK引脚
    pin_config.ws_io_num = app::kSpkLrcPin;  // WS引脚
    pin_config.data_out_num = app::kSpkDinPin;  // 数据输出引脚
    pin_config.data_in_num = I2S_PIN_NO_CHANGE;  // 数据输入引脚不变

    // 设置I2S引脚
    err = i2s_set_pin(app::kSpkPort, &pin_config);
    if (err != ESP_OK) {
        LOGE("AUDIO", "Speaker I2S pin config failed: %d", err);
        return false;
    }

    // 清零DMA缓冲区
    i2s_zero_dma_buffer(app::kSpkPort);
    return true;
}

/**
 * @brief 音频任务入口函数
 * @param arg 任务参数
 */
void AudioPipeline::audioTaskEntry(void* arg) {
    auto* self = static_cast<AudioPipeline*>(arg);
    self->audioTask();
    vTaskDelete(nullptr);
}

/**
 * @brief 音频任务主函数
 * @note 按照固定的帧周期处理音频数据
 */
void AudioPipeline::audioTask() {
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t frame_period = pdMS_TO_TICKS(app::kAudioFrameMs);

    while (running_) {
        // 等待到下一个帧周期
        vTaskDelayUntil(&last_wake, frame_period);

        // 根据当前模式处理音频
        switch (mode_) {
            case AudioMode::Capturing:
                // 处理捕获帧
                processCaptureFrame();
                break;
            case AudioMode::Prebuffering:
            case AudioMode::Playing:
                // 处理播放帧
                processPlaybackFrame();
                break;
            case AudioMode::Idle:
            default:
                // 空闲状态，不做处理
                break;
        }

        // 更新统计信息
        stats_.tx_buffered_bytes = tx_ring_.size();
        stats_.rx_buffered_bytes = rx_ring_.size();
    }
}

/**
 * @brief 处理捕获帧
 * @note 从麦克风读取数据，进行处理后写入发送环形缓冲区
 */
void AudioPipeline::processCaptureFrame() {
    size_t bytes_read = 0;
    // 从I2S读取麦克风数据
    const esp_err_t err = i2s_read(
        app::kMicPort,
        mic_raw_frame_,
        app::kInputSamplesPerFrame * sizeof(int32_t),
        &bytes_read,
        pdMS_TO_TICKS(app::kAudioFrameMs));

    if (err != ESP_OK) {
        LOGW("AUDIO", "Mic read failed: %d", err);
        return;
    }

    const size_t samples_read = bytes_read / sizeof(int32_t);
    if (samples_read == 0) {
        return;
    }

    // 处理麦克风数据：右移16位，应用增益，限幅
    for (size_t i = 0; i < samples_read; ++i) {
        int32_t sample = mic_raw_frame_[i] >> 16;
        sample = (sample * app::kMicGainPercent) / 100;
        if (sample > INT16_MAX) {
            sample = INT16_MAX;
        } else if (sample < INT16_MIN) {
            sample = INT16_MIN;
        }
        mic_pcm_frame_[i] = static_cast<int16_t>(sample);
    }

    // 填充剩余样本为0
    if (samples_read < app::kInputSamplesPerFrame) {
        memset(
            mic_pcm_frame_ + samples_read,
            0,
            (app::kInputSamplesPerFrame - samples_read) * sizeof(int16_t));
    }

    // 计算RMS值，用于VAD（语音活动检测）
    const int rms = calculateRms(mic_pcm_frame_, app::kInputSamplesPerFrame);
    stats_.last_mic_rms = rms;

    // 检查是否在预热期
    const bool warmup_active =
        (millis() - capture_started_ms_) < app::kCaptureVadWarmupMs;
    // 检测语音活动
    const bool detected =
        (rms >= app::kVadStartThreshold) ||
        (voice_active_ && rms >= app::kVadContinueThreshold);

    // 处理VAD逻辑
    if (warmup_active) {
        // 预热期，重置计数器
        speech_frames_ = 0;
        silence_frames_ = 0;
    } else if (detected) {
        // 检测到语音
        silence_frames_ = 0;
        if (!voice_active_) {
            // 开始检测到语音
            if (speech_frames_ < 0xFF) {
                ++speech_frames_;
            }
            if (speech_frames_ >= app::kVadStartFrames) {
                // 确认检测到语音
                voice_active_ = true;
                speech_frames_ = 0;
                if (speech_callback_) {
                    speech_callback_(true);
                }
            }
        }
    } else if (voice_active_) {
        // 检测到静音
        ++silence_frames_;
        if (silence_frames_ >= app::kVadSilenceFrames) {
            // 确认检测到静音
            voice_active_ = false;
            speech_frames_ = 0;
            silence_frames_ = 0;
            if (speech_callback_) {
                speech_callback_(false);
            }
        }
    } else {
        // 持续静音
        speech_frames_ = 0;
    }

    // 将处理后的数据写入发送环形缓冲区
    const size_t written = tx_ring_.write(
        reinterpret_cast<uint8_t*>(mic_pcm_frame_),
        app::kTxFrameBytes);
    if (written < app::kTxFrameBytes) {
        ++stats_.tx_overflows;
    } else {
        ++stats_.captured_frames;
    }

    // 定期输出音频统计信息
    if ((millis() - last_audio_log_ms_) >= app::kAudioStatsIntervalMs) {
        LOGD(
            "AUDIO",
            "mic rms=%d tx=%.1f%% rx=%.1f%%",
            rms,
            tx_ring_.fillPercent(),
            rx_ring_.fillPercent());
        last_audio_log_ms_ = millis();
    }
}

/**
 * @brief 处理播放帧
 * @note 从接收环形缓冲区读取数据，进行处理后通过I2S输出到扬声器
 */
void AudioPipeline::processPlaybackFrame() {
    const size_t buffered = rx_ring_.size();

    // 处理预缓冲模式
    if (mode_ == AudioMode::Prebuffering) {
        // 检查是否准备就绪
        const bool ready_with_full_buffer = buffered >= app::kPlaybackPrebufferBytes;
        const bool ready_with_completed_stream =
            playback_stream_ended_ && buffered >= app::kRxFrameBytes;
        const bool ready_after_grace =
            !require_full_prebuffer_ &&
            buffered >= app::kPlaybackResumeBytes &&
            (millis() - playback_prebuffer_started_ms_) >= app::kPlaybackStartGraceMs;

        if (ready_with_full_buffer ||
            ready_with_completed_stream ||
            ready_after_grace) {
            // 切换到播放模式
            mode_ = AudioMode::Playing;
            require_full_prebuffer_ = false;
            playback_fade_samples_remaining_ = 64;
            LOGI("AUDIO", "Playback start with %u bytes buffered", static_cast<unsigned>(buffered));
        } else {
            return;
        }
    }

    // 从接收环形缓冲区读取数据
    size_t bytes_read = rx_ring_.read(
        reinterpret_cast<uint8_t*>(playback_frame_),
        app::kRxFrameBytes);
    if (bytes_read == 0) {
        // 缓冲区为空
        ++stats_.rx_underruns;
        if (playback_stream_ended_) {
            underrun_frames_ = 0;
            return;
        }

        // 增加欠载计数
        if (underrun_frames_ < 0xFF) {
            ++underrun_frames_;
        }

        // 输出警告
        if (underrun_frames_ == app::kPlaybackUnderrunToleranceFrames) {
            LOGW("AUDIO", "Playback jitter gap, holding output");
        }

        // 构建conceal帧或静音帧
        if (underrun_frames_ <= app::kPlaybackConcealFrames) {
            buildConcealFrame(underrun_frames_);
        } else {
            memset(silence_frame_, 0, app::kRxFrameBytes);
        }

        // 输出conceal帧
        const int16_t* conceal_frame = silence_frame_;
        size_t bytes_written = 0;
        const esp_err_t err = i2s_write(
            app::kSpkPort,
            conceal_frame,
            app::kRxFrameBytes,
            &bytes_written,
            pdMS_TO_TICKS(app::kAudioFrameMs));
        if (err != ESP_OK) {
            LOGW("AUDIO", "Speaker conceal fill failed: %d", err);
        }
        return;
    }
    // 从欠载中恢复
    const bool recovering_from_gap = underrun_frames_ > 0;
    underrun_frames_ = 0;

    // 填充剩余数据为0
    if (bytes_read < app::kRxFrameBytes) {
        memset(
            reinterpret_cast<uint8_t*>(playback_frame_) + bytes_read,
            0,
            app::kRxFrameBytes - bytes_read);
    }

    // 应用音量
    applyVolume(playback_frame_, app::kOutputSamplesPerFrame);
    // 处理淡入
    if (recovering_from_gap && playback_fade_samples_remaining_ == 0) {
        playback_fade_samples_remaining_ = 64;
    }
    if (playback_fade_samples_remaining_ > 0) {
        const size_t fade_samples =
            playback_fade_samples_remaining_ < app::kOutputSamplesPerFrame
                ? playback_fade_samples_remaining_
                : app::kOutputSamplesPerFrame;
        for (size_t i = 0; i < fade_samples; ++i) {
            const int32_t numerator = static_cast<int32_t>(i + 1);
            const int32_t denominator = static_cast<int32_t>(fade_samples);
            playback_frame_[i] = static_cast<int16_t>(
                (static_cast<int32_t>(playback_frame_[i]) * numerator) / denominator);
        }
        playback_fade_samples_remaining_ -= fade_samples;
    }

    // 输出到扬声器
    size_t bytes_written = 0;
    const esp_err_t err = i2s_write(
        app::kSpkPort,
        playback_frame_,
        app::kRxFrameBytes,
        &bytes_written,
        pdMS_TO_TICKS(app::kAudioFrameMs));

    if (err != ESP_OK) {
        LOGW("AUDIO", "Speaker write failed: %d", err);
        return;
    }

    // 增加播放帧数统计
    ++stats_.played_frames;
}

/**
 * @brief 构建conceal帧
 * @param conceal_index conceal索引
 * @note 用于在音频欠载时生成平滑的过渡帧
 */
void AudioPipeline::buildConcealFrame(uint8_t conceal_index) {
    // 复制上一帧数据到静音帧
    memcpy(silence_frame_, playback_frame_, app::kRxFrameBytes);

    // 计算缩放因子
    const int32_t frame_scale_den = static_cast<int32_t>(app::kPlaybackConcealFrames + 1);
    const int32_t frame_scale_num = static_cast<int32_t>(
        max<int>(0, frame_scale_den - static_cast<int32_t>(conceal_index)));

    // 对每个样本应用缩放和淡出
    for (size_t i = 0; i < app::kOutputSamplesPerFrame; ++i) {
        const int32_t tail_num =
            static_cast<int32_t>(app::kOutputSamplesPerFrame - i);
        int32_t sample = silence_frame_[i];
        // 应用帧缩放
        sample = (sample * frame_scale_num) / frame_scale_den;
        // 应用淡出
        sample = (sample * tail_num) / static_cast<int32_t>(app::kOutputSamplesPerFrame);
        silence_frame_[i] = static_cast<int16_t>(sample);
    }
}

/**
 * @brief 计算RMS（均方根）值
 * @param samples 样本数据
 * @param count 样本数量
 * @return RMS值
 * @note 用于语音活动检测（VAD）
 */
int AudioPipeline::calculateRms(const int16_t* samples, size_t count) const {
    if (samples == nullptr || count == 0) {
        return 0;
    }

    // 计算平方和
    int64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        const int32_t sample = samples[i];
        sum += static_cast<int64_t>(sample) * sample;
    }

    // 计算均方根
    return static_cast<int>(sqrt(static_cast<double>(sum) / static_cast<double>(count)));
}

/**
 * @brief 应用音量
 * @param samples 样本数据
 * @param count 样本数量
 * @note 根据当前音量百分比调整音频样本
 */
void AudioPipeline::applyVolume(int16_t* samples, size_t count) {
    const int volume = speaker_volume_percent_;
    if (volume == 100) {
        // 音量为100%，无需调整
        return;
    }

    // 对每个样本应用音量调整
    for (size_t i = 0; i < count; ++i) {
        int32_t sample = (static_cast<int32_t>(samples[i]) * volume) / 100;
        // 限幅
        if (sample > INT16_MAX) {
            sample = INT16_MAX;
        } else if (sample < INT16_MIN) {
            sample = INT16_MIN;
        }
        samples[i] = static_cast<int16_t>(sample);
    }
}

/**
 * @brief 设置扬声器音量百分比
 * @param volume_percent 音量百分比
 * @note 音量范围被限制在最小和最大值之间
 */
void AudioPipeline::setSpeakerVolumePercent(uint8_t volume_percent) {
    speaker_volume_percent_ = constrain(
        volume_percent,
        app::kSpeakerVolumeMinPercent,
        app::kSpeakerVolumeMaxPercent);
}

/**
 * @brief 设置扬声器放大器使能状态
 * @param enabled 是否使能
 * @note 仅当扬声器放大器引脚有效时才执行操作
 */
void AudioPipeline::setSpeakerAmpEnabled(bool enabled) {
    if (app::kSpkAmpEnablePin < 0) {
        return;
    }
    digitalWrite(
        app::kSpkAmpEnablePin,
        enabled ? app::kSpkAmpEnableLevel : !app::kSpkAmpEnableLevel);
}

/**
 * @brief 开始捕获音频
 * @note 初始化捕获状态，启动麦克风I2S，停止扬声器
 */
void AudioPipeline::startCapture() {
    // 清空缓冲区
    clearCaptureBuffer();
    clearPlaybackBuffer();
    // 重置状态
    voice_active_ = false;
    speech_frames_ = 0;
    silence_frames_ = 0;
    playback_stream_ended_ = false;
    playback_fade_samples_remaining_ = 0;
    // 记录捕获开始时间
    capture_started_ms_ = millis();
    // 设置模式为捕获
    mode_ = AudioMode::Capturing;
    // 清零麦克风DMA缓冲区
    i2s_zero_dma_buffer(app::kMicPort);
    // 启动麦克风I2S
    i2s_start(app::kMicPort);
    // 停止扬声器I2S
    i2s_stop(app::kSpkPort);
    // 禁用扬声器放大器
    setSpeakerAmpEnabled(false);
}

/**
 * @brief 停止捕获音频
 * @note 停止麦克风I2S，重置状态，清空捕获缓冲区
 */
void AudioPipeline::stopCapture() {
    // 停止麦克风I2S
    i2s_stop(app::kMicPort);
    // 重置状态
    voice_active_ = false;
    speech_frames_ = 0;
    silence_frames_ = 0;
    // 清空捕获缓冲区
    clearCaptureBuffer();
    // 如果当前是捕获模式，切换到空闲模式
    if (mode_ == AudioMode::Capturing) {
        mode_ = AudioMode::Idle;
    }
}

/**
 * @brief 开始播放音频
 * @note 初始化播放状态，启动扬声器I2S，停止麦克风
 */
void AudioPipeline::startPlayback() {
    // 如果已经在播放或预缓冲模式，直接返回
    if (mode_ == AudioMode::Playing || mode_ == AudioMode::Prebuffering) {
        return;
    }
    // 停止麦克风I2S
    i2s_stop(app::kMicPort);
    // 清零扬声器DMA缓冲区
    i2s_zero_dma_buffer(app::kSpkPort);
    // 启用扬声器放大器
    setSpeakerAmpEnabled(true);
    // 启动扬声器I2S
    i2s_start(app::kSpkPort);
    // 重置状态
    require_full_prebuffer_ = false;
    playback_stream_ended_ = false;
    underrun_frames_ = 0;
    playback_fade_samples_remaining_ = 0;
    // 记录预缓冲开始时间
    playback_prebuffer_started_ms_ = millis();
    // 设置模式为预缓冲
    mode_ = AudioMode::Prebuffering;
}

/**
 * @brief 停止播放音频
 * @param clear_buffer 是否清空缓冲区
 * @note 停止扬声器I2S，禁用扬声器放大器，重置状态
 */
void AudioPipeline::stopPlayback(bool clear_buffer) {
    // 停止扬声器I2S
    i2s_stop(app::kSpkPort);
    // 禁用扬声器放大器
    setSpeakerAmpEnabled(false);
    // 清空播放缓冲区
    if (clear_buffer) {
        clearPlaybackBuffer();
    }
    // 重置状态
    require_full_prebuffer_ = false;
    playback_stream_ended_ = false;
    underrun_frames_ = 0;
    playback_fade_samples_remaining_ = 0;
    // 如果当前是播放或预缓冲模式，切换到空闲模式
    if (mode_ == AudioMode::Playing || mode_ == AudioMode::Prebuffering) {
        mode_ = AudioMode::Idle;
    }
}

/**
 * @brief 标记播放流结束
 * @note 设置播放流结束标志，用于处理播放完成的情况
 */
void AudioPipeline::markPlaybackStreamEnded() {
    playback_stream_ended_ = true;
}

/**
 * @brief 清空捕获缓冲区
 * @note 清空发送环形缓冲区并重置相关统计信息
 */
void AudioPipeline::clearCaptureBuffer() {
    tx_ring_.clear();
    stats_.tx_buffered_bytes = 0;
}

/**
 * @brief 清空播放缓冲区
 * @note 清空接收环形缓冲区并重置相关状态和统计信息
 */
void AudioPipeline::clearPlaybackBuffer() {
    rx_ring_.clear();
    stats_.rx_buffered_bytes = 0;
    playback_stream_ended_ = false;
    playback_fade_samples_remaining_ = 0;
}

/**
 * @brief 读取捕获的音频数据
 * @param data 数据缓冲区
 * @param max_len 最大读取长度
 * @return 实际读取的长度
 * @note 从发送环形缓冲区读取数据
 */
size_t AudioPipeline::readCaptureAudio(uint8_t* data, size_t max_len) {
    return tx_ring_.read(data, max_len);
}

/**
 * @brief 获取捕获缓冲区已用字节数
 * @return 已用字节数
 * @note 返回发送环形缓冲区的大小
 */
size_t AudioPipeline::captureBufferedBytes() {
    return tx_ring_.size();
}

/**
 * @brief 推送播放音频数据
 * @param data 音频数据
 * @param len 数据长度
 * @return 推送成功返回true，失败返回false
 * @note 将音频数据写入接收环形缓冲区，并在需要时启动播放
 */
bool AudioPipeline::pushPlaybackAudio(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0) {
        return false;
    }

    // 写入接收环形缓冲区
    const size_t written = rx_ring_.write(data, len);
    if (written < len) {
        // 缓冲区溢出
        ++stats_.rx_overflows;
        LOGW("AUDIO", "Playback buffer overflow: %u/%u", static_cast<unsigned>(written), static_cast<unsigned>(len));
    }

    // 重置播放流结束标志
    playback_stream_ended_ = false;

    // 如果当前是空闲或捕获模式，切换到播放模式
    if (mode_ == AudioMode::Idle || mode_ == AudioMode::Capturing) {
        stopCapture();
        startPlayback();
    }
    // 返回是否完全写入
    return written == len;
}

/**
 * @brief 获取播放缓冲区已用字节数
 * @return 已用字节数
 * @note 返回接收环形缓冲区的大小
 */
size_t AudioPipeline::playbackBufferedBytes() {
    return rx_ring_.size();
}

/**
 * @brief 获取音频统计信息
 * @return 音频统计信息
 * @note 更新缓冲区状态并返回完整的统计信息
 */
AudioStats AudioPipeline::stats() {
    // 更新缓冲区状态
    stats_.tx_buffered_bytes = tx_ring_.size();
    stats_.rx_buffered_bytes = rx_ring_.size();
    // 返回统计信息
    return stats_;
}
