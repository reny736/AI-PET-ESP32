#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>

#include "app_config.h"
#include "ring_buffer.h"

enum class AudioMode {
    Idle,
    Capturing,
    Prebuffering,
    Playing
};

struct AudioStats {
    uint32_t captured_frames = 0;
    uint32_t played_frames = 0;
    uint32_t tx_overflows = 0;
    uint32_t rx_overflows = 0;
    uint32_t rx_underruns = 0;
    int last_mic_rms = 0;
    size_t tx_buffered_bytes = 0;
    size_t rx_buffered_bytes = 0;
};

class AudioPipeline {
public:
    using SpeechCallback = std::function<void(bool)>;

    AudioPipeline();
    ~AudioPipeline();

    bool begin();
    void end();

    void setSpeechCallback(SpeechCallback callback) { speech_callback_ = callback; }

    void startCapture();
    void stopCapture();
    void startPlayback();
    void stopPlayback(bool clear_buffer = true);
    void markPlaybackStreamEnded();
    void setSpeakerVolumePercent(uint8_t volume_percent);
    uint8_t speakerVolumePercent() const { return speaker_volume_percent_; }

    void clearCaptureBuffer();
    void clearPlaybackBuffer();

    size_t readCaptureAudio(uint8_t* data, size_t max_len);
    size_t captureBufferedBytes();

    bool pushPlaybackAudio(const uint8_t* data, size_t len);
    size_t playbackBufferedBytes();

    AudioMode mode() const { return mode_; }
    bool isCapturing() const { return mode_ == AudioMode::Capturing; }
    bool isPlaying() const {
        return mode_ == AudioMode::Playing || mode_ == AudioMode::Prebuffering;
    }

    AudioStats stats();

private:
    static void audioTaskEntry(void* arg);
    void audioTask();

    bool initInputI2s();
    bool initOutputI2s();
    bool allocateBuffers();
    void freeBuffers();

    void processCaptureFrame();
    void processPlaybackFrame();
    void buildConcealFrame(uint8_t conceal_index);
    int calculateRms(const int16_t* samples, size_t count) const;
    void applyVolume(int16_t* samples, size_t count);
    void setSpeakerAmpEnabled(bool enabled);

    RingBuffer tx_ring_;
    RingBuffer rx_ring_;

    TaskHandle_t task_handle_;
    AudioMode mode_;
    SpeechCallback speech_callback_;
    AudioStats stats_;

    int32_t* mic_raw_frame_;
    int16_t* mic_pcm_frame_;
    int16_t* playback_frame_;
    int16_t* silence_frame_;

    bool running_;
    bool voice_active_;
    bool require_full_prebuffer_;
    bool playback_stream_ended_;
    volatile uint8_t speaker_volume_percent_;
    uint8_t speech_frames_;
    uint8_t silence_frames_;
    uint8_t underrun_frames_;
    uint16_t playback_fade_samples_remaining_;
    uint32_t capture_started_ms_;
    uint32_t playback_prebuffer_started_ms_;
    uint32_t last_audio_log_ms_;
};
