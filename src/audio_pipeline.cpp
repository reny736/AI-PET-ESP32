#include "audio_pipeline.h"

#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#include "logger.h"

namespace {

void* allocPsram(size_t size) {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == nullptr) {
        ptr = malloc(size);
    }
    return ptr;
}

}  // namespace

AudioPipeline::AudioPipeline()
    : tx_ring_(app::kTxRingBufferBytes),
      rx_ring_(app::kRxRingBufferBytes),
      task_handle_(nullptr),
      mode_(AudioMode::Idle),
      mic_raw_frame_(nullptr),
      mic_pcm_frame_(nullptr),
      playback_frame_(nullptr),
      silence_frame_(nullptr),
      running_(false),
      voice_active_(false),
      require_full_prebuffer_(false),
      playback_stream_ended_(false),
      speaker_volume_percent_(app::kSpeakerVolumeDefaultPercent),
      speech_frames_(0),
      silence_frames_(0),
      underrun_frames_(0),
      playback_fade_samples_remaining_(0),
      capture_started_ms_(0),
      playback_prebuffer_started_ms_(0),
      last_audio_log_ms_(0) {
}

AudioPipeline::~AudioPipeline() {
    end();
}

bool AudioPipeline::begin() {
    if (running_) {
        return true;
    }

    if (!tx_ring_.begin() || !rx_ring_.begin()) {
        return false;
    }

    if (!allocateBuffers()) {
        return false;
    }

    if (app::kSpkAmpEnablePin >= 0) {
        pinMode(app::kSpkAmpEnablePin, OUTPUT);
        setSpeakerAmpEnabled(false);
    }

    if (!initInputI2s() || !initOutputI2s()) {
        return false;
    }

    i2s_stop(app::kMicPort);
    i2s_stop(app::kSpkPort);

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

void AudioPipeline::end() {
    running_ = false;

    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }

    i2s_stop(app::kMicPort);
    i2s_stop(app::kSpkPort);
    i2s_driver_uninstall(app::kMicPort);
    i2s_driver_uninstall(app::kSpkPort);

    setSpeakerAmpEnabled(false);
    freeBuffers();
    tx_ring_.end();
    rx_ring_.end();
    mode_ = AudioMode::Idle;
}

bool AudioPipeline::allocateBuffers() {
    mic_raw_frame_ = static_cast<int32_t*>(
        allocPsram(app::kInputSamplesPerFrame * sizeof(int32_t)));
    mic_pcm_frame_ = static_cast<int16_t*>(
        allocPsram(app::kInputSamplesPerFrame * sizeof(int16_t)));
    playback_frame_ = static_cast<int16_t*>(
        allocPsram(app::kOutputSamplesPerFrame * sizeof(int16_t)));
    silence_frame_ = static_cast<int16_t*>(
        allocPsram(app::kOutputSamplesPerFrame * sizeof(int16_t)));

    if (mic_raw_frame_ == nullptr || mic_pcm_frame_ == nullptr ||
        playback_frame_ == nullptr || silence_frame_ == nullptr) {
        LOGE("AUDIO", "Failed to allocate audio buffers");
        return false;
    }

    memset(playback_frame_, 0, app::kRxFrameBytes);
    memset(silence_frame_, 0, app::kRxFrameBytes);
    return true;
}

void AudioPipeline::freeBuffers() {
    if (mic_raw_frame_ != nullptr) {
        free(mic_raw_frame_);
        mic_raw_frame_ = nullptr;
    }
    if (mic_pcm_frame_ != nullptr) {
        free(mic_pcm_frame_);
        mic_pcm_frame_ = nullptr;
    }
    if (playback_frame_ != nullptr) {
        free(playback_frame_);
        playback_frame_ = nullptr;
    }
    if (silence_frame_ != nullptr) {
        free(silence_frame_);
        silence_frame_ = nullptr;
    }
}

bool AudioPipeline::initInputI2s() {
    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
    config.sample_rate = app::kInputSampleRate;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    config.channel_format = app::kMicChannelFormat;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = app::kI2sDmaBufCount;
    config.dma_buf_len = app::kI2sDmaBufLen;
    config.use_apll = false;
    config.tx_desc_auto_clear = false;
    config.fixed_mclk = 0;
    config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    config.bits_per_chan = I2S_BITS_PER_CHAN_32BIT;

    esp_err_t err = i2s_driver_install(app::kMicPort, &config, 0, nullptr);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LOGE("AUDIO", "Mic I2S install failed: %d", err);
        return false;
    }

    i2s_pin_config_t pin_config = {};
    pin_config.bck_io_num = app::kMicSckPin;
    pin_config.ws_io_num = app::kMicWsPin;
    pin_config.data_out_num = I2S_PIN_NO_CHANGE;
    pin_config.data_in_num = app::kMicSdPin;

    err = i2s_set_pin(app::kMicPort, &pin_config);
    if (err != ESP_OK) {
        LOGE("AUDIO", "Mic I2S pin config failed: %d", err);
        return false;
    }

    i2s_zero_dma_buffer(app::kMicPort);
    return true;
}

bool AudioPipeline::initOutputI2s() {
    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = app::kOutputSampleRate;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = app::kI2sDmaBufCount;
    config.dma_buf_len = app::kI2sDmaBufLen;
    config.use_apll = true;
    config.tx_desc_auto_clear = true;
    config.fixed_mclk = 0;
    config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;

    esp_err_t err = i2s_driver_install(app::kSpkPort, &config, 0, nullptr);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LOGE("AUDIO", "Speaker I2S install failed: %d", err);
        return false;
    }

    i2s_pin_config_t pin_config = {};
    pin_config.bck_io_num = app::kSpkBclkPin;
    pin_config.ws_io_num = app::kSpkLrcPin;
    pin_config.data_out_num = app::kSpkDinPin;
    pin_config.data_in_num = I2S_PIN_NO_CHANGE;

    err = i2s_set_pin(app::kSpkPort, &pin_config);
    if (err != ESP_OK) {
        LOGE("AUDIO", "Speaker I2S pin config failed: %d", err);
        return false;
    }

    i2s_zero_dma_buffer(app::kSpkPort);
    return true;
}

void AudioPipeline::audioTaskEntry(void* arg) {
    auto* self = static_cast<AudioPipeline*>(arg);
    self->audioTask();
    vTaskDelete(nullptr);
}

void AudioPipeline::audioTask() {
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t frame_period = pdMS_TO_TICKS(app::kAudioFrameMs);

    while (running_) {
        vTaskDelayUntil(&last_wake, frame_period);

        switch (mode_) {
            case AudioMode::Capturing:
                processCaptureFrame();
                break;
            case AudioMode::Prebuffering:
            case AudioMode::Playing:
                processPlaybackFrame();
                break;
            case AudioMode::Idle:
            default:
                break;
        }

        stats_.tx_buffered_bytes = tx_ring_.size();
        stats_.rx_buffered_bytes = rx_ring_.size();
    }
}

void AudioPipeline::processCaptureFrame() {
    size_t bytes_read = 0;
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

    if (samples_read < app::kInputSamplesPerFrame) {
        memset(
            mic_pcm_frame_ + samples_read,
            0,
            (app::kInputSamplesPerFrame - samples_read) * sizeof(int16_t));
    }

    const int rms = calculateRms(mic_pcm_frame_, app::kInputSamplesPerFrame);
    stats_.last_mic_rms = rms;

    const bool warmup_active =
        (millis() - capture_started_ms_) < app::kCaptureVadWarmupMs;
    const bool detected =
        (rms >= app::kVadStartThreshold) ||
        (voice_active_ && rms >= app::kVadContinueThreshold);

    if (warmup_active) {
        speech_frames_ = 0;
        silence_frames_ = 0;
    } else if (detected) {
        silence_frames_ = 0;
        if (!voice_active_) {
            if (speech_frames_ < 0xFF) {
                ++speech_frames_;
            }
            if (speech_frames_ >= app::kVadStartFrames) {
                voice_active_ = true;
                speech_frames_ = 0;
                if (speech_callback_) {
                    speech_callback_(true);
                }
            }
        }
    } else if (voice_active_) {
        ++silence_frames_;
        if (silence_frames_ >= app::kVadSilenceFrames) {
            voice_active_ = false;
            speech_frames_ = 0;
            silence_frames_ = 0;
            if (speech_callback_) {
                speech_callback_(false);
            }
        }
    } else {
        speech_frames_ = 0;
    }

    const size_t written = tx_ring_.write(
        reinterpret_cast<uint8_t*>(mic_pcm_frame_),
        app::kTxFrameBytes);
    if (written < app::kTxFrameBytes) {
        ++stats_.tx_overflows;
    } else {
        ++stats_.captured_frames;
    }

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

void AudioPipeline::processPlaybackFrame() {
    const size_t buffered = rx_ring_.size();

    if (mode_ == AudioMode::Prebuffering) {
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
            mode_ = AudioMode::Playing;
            require_full_prebuffer_ = false;
            playback_fade_samples_remaining_ = 64;
            LOGI("AUDIO", "Playback start with %u bytes buffered", static_cast<unsigned>(buffered));
        } else {
            return;
        }
    }

    size_t bytes_read = rx_ring_.read(
        reinterpret_cast<uint8_t*>(playback_frame_),
        app::kRxFrameBytes);
    if (bytes_read == 0) {
        ++stats_.rx_underruns;
        if (playback_stream_ended_) {
            underrun_frames_ = 0;
            return;
        }

        if (underrun_frames_ < 0xFF) {
            ++underrun_frames_;
        }

        if (underrun_frames_ == app::kPlaybackUnderrunToleranceFrames) {
            LOGW("AUDIO", "Playback jitter gap, holding output");
        }

        if (underrun_frames_ <= app::kPlaybackConcealFrames) {
            buildConcealFrame(underrun_frames_);
        } else {
            memset(silence_frame_, 0, app::kRxFrameBytes);
        }

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
    const bool recovering_from_gap = underrun_frames_ > 0;
    underrun_frames_ = 0;

    if (bytes_read < app::kRxFrameBytes) {
        memset(
            reinterpret_cast<uint8_t*>(playback_frame_) + bytes_read,
            0,
            app::kRxFrameBytes - bytes_read);
    }

    applyVolume(playback_frame_, app::kOutputSamplesPerFrame);
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

    ++stats_.played_frames;
}

void AudioPipeline::buildConcealFrame(uint8_t conceal_index) {
    memcpy(silence_frame_, playback_frame_, app::kRxFrameBytes);

    const int32_t frame_scale_den = static_cast<int32_t>(app::kPlaybackConcealFrames + 1);
    const int32_t frame_scale_num = static_cast<int32_t>(
        max<int>(0, frame_scale_den - static_cast<int32_t>(conceal_index)));

    for (size_t i = 0; i < app::kOutputSamplesPerFrame; ++i) {
        const int32_t tail_num =
            static_cast<int32_t>(app::kOutputSamplesPerFrame - i);
        int32_t sample = silence_frame_[i];
        sample = (sample * frame_scale_num) / frame_scale_den;
        sample = (sample * tail_num) / static_cast<int32_t>(app::kOutputSamplesPerFrame);
        silence_frame_[i] = static_cast<int16_t>(sample);
    }
}

int AudioPipeline::calculateRms(const int16_t* samples, size_t count) const {
    if (samples == nullptr || count == 0) {
        return 0;
    }

    int64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        const int32_t sample = samples[i];
        sum += static_cast<int64_t>(sample) * sample;
    }

    return static_cast<int>(sqrt(static_cast<double>(sum) / static_cast<double>(count)));
}

void AudioPipeline::applyVolume(int16_t* samples, size_t count) {
    const int volume = speaker_volume_percent_;
    if (volume == 100) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        int32_t sample = (static_cast<int32_t>(samples[i]) * volume) / 100;
        if (sample > INT16_MAX) {
            sample = INT16_MAX;
        } else if (sample < INT16_MIN) {
            sample = INT16_MIN;
        }
        samples[i] = static_cast<int16_t>(sample);
    }
}

void AudioPipeline::setSpeakerVolumePercent(uint8_t volume_percent) {
    speaker_volume_percent_ = constrain(
        volume_percent,
        app::kSpeakerVolumeMinPercent,
        app::kSpeakerVolumeMaxPercent);
}

void AudioPipeline::setSpeakerAmpEnabled(bool enabled) {
    if (app::kSpkAmpEnablePin < 0) {
        return;
    }
    digitalWrite(
        app::kSpkAmpEnablePin,
        enabled ? app::kSpkAmpEnableLevel : !app::kSpkAmpEnableLevel);
}

void AudioPipeline::startCapture() {
    clearCaptureBuffer();
    clearPlaybackBuffer();
    voice_active_ = false;
    speech_frames_ = 0;
    silence_frames_ = 0;
    playback_stream_ended_ = false;
    playback_fade_samples_remaining_ = 0;
    capture_started_ms_ = millis();
    mode_ = AudioMode::Capturing;
    i2s_zero_dma_buffer(app::kMicPort);
    i2s_start(app::kMicPort);
    i2s_stop(app::kSpkPort);
    setSpeakerAmpEnabled(false);
}

void AudioPipeline::stopCapture() {
    i2s_stop(app::kMicPort);
    voice_active_ = false;
    speech_frames_ = 0;
    silence_frames_ = 0;
    clearCaptureBuffer();
    if (mode_ == AudioMode::Capturing) {
        mode_ = AudioMode::Idle;
    }
}

void AudioPipeline::startPlayback() {
    if (mode_ == AudioMode::Playing || mode_ == AudioMode::Prebuffering) {
        return;
    }
    i2s_stop(app::kMicPort);
    i2s_zero_dma_buffer(app::kSpkPort);
    setSpeakerAmpEnabled(true);
    i2s_start(app::kSpkPort);
    require_full_prebuffer_ = false;
    playback_stream_ended_ = false;
    underrun_frames_ = 0;
    playback_fade_samples_remaining_ = 0;
    playback_prebuffer_started_ms_ = millis();
    mode_ = AudioMode::Prebuffering;
}

void AudioPipeline::stopPlayback(bool clear_buffer) {
    i2s_stop(app::kSpkPort);
    setSpeakerAmpEnabled(false);
    if (clear_buffer) {
        clearPlaybackBuffer();
    }
    require_full_prebuffer_ = false;
    playback_stream_ended_ = false;
    underrun_frames_ = 0;
    playback_fade_samples_remaining_ = 0;
    if (mode_ == AudioMode::Playing || mode_ == AudioMode::Prebuffering) {
        mode_ = AudioMode::Idle;
    }
}

void AudioPipeline::markPlaybackStreamEnded() {
    playback_stream_ended_ = true;
}

void AudioPipeline::clearCaptureBuffer() {
    tx_ring_.clear();
    stats_.tx_buffered_bytes = 0;
}

void AudioPipeline::clearPlaybackBuffer() {
    rx_ring_.clear();
    stats_.rx_buffered_bytes = 0;
    playback_stream_ended_ = false;
    playback_fade_samples_remaining_ = 0;
}

size_t AudioPipeline::readCaptureAudio(uint8_t* data, size_t max_len) {
    return tx_ring_.read(data, max_len);
}

size_t AudioPipeline::captureBufferedBytes() {
    return tx_ring_.size();
}

bool AudioPipeline::pushPlaybackAudio(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0) {
        return false;
    }

    const size_t written = rx_ring_.write(data, len);
    if (written < len) {
        ++stats_.rx_overflows;
        LOGW("AUDIO", "Playback buffer overflow: %u/%u", static_cast<unsigned>(written), static_cast<unsigned>(len));
    }

    playback_stream_ended_ = false;

    if (mode_ == AudioMode::Idle || mode_ == AudioMode::Capturing) {
        stopCapture();
        startPlayback();
    }
    return written == len;
}

size_t AudioPipeline::playbackBufferedBytes() {
    return rx_ring_.size();
}

AudioStats AudioPipeline::stats() {
    stats_.tx_buffered_bytes = tx_ring_.size();
    stats_.rx_buffered_bytes = rx_ring_.size();
    return stats_;
}
