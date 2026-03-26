#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

#include "audio_pipeline.h"
#include "doubao_protocol.h"
#include "doubao_ws_client.h"
#include "ota_manager.h"
#include "status_led.h"

enum class AppState {
    Booting,
    WifiConnecting,
    Standby,
    ApiConnecting,
    SessionStarting,
    Ota,
    Listening,
    Thinking,
    Speaking,
    Error
};

class RealtimeVoiceApp {
public:
    RealtimeVoiceApp();

    bool begin();
    void loop();

private:
    bool connectWiFi();
    bool connectDoubao();
    bool beginAuxSerial();
    void beginOtaButtons();
    bool sendStartConnection();
    bool sendStartSession();
    bool sendSayHello();
    void startListening();
    void stopListeningForThinking();
    void serviceSerialCommands();
    void serviceAuxSerial();
    void serviceOtaButtons();
    void serviceOtaMode();
    void handleSerialCommand(const String& command);
    void requestApiActivation();
    void deactivateApiConnection(const char* reason);
    void loadSpeakerVolumePreference();
    void saveSpeakerVolumePreference(uint8_t volume_percent);
    void applySpeakerVolume(int volume_percent, bool persist, const char* source);
    void printSerialHelp();
    void printOtaHelp();
    void handleOtaCommand(const String& command);
    bool enterOtaMode(OtaTarget target, const char* reason);
    void exitOtaMode(const char* reason);
    bool prepareForOta(const char* reason);
    bool runOtaUpdate(
        OtaTarget target,
        const String& url,
        const String& expected_sha256,
        const String& resolved_version = "");
    bool readOtaButton(int pin) const;
    bool detectButtonPress(
        int pin,
        bool enabled,
        bool& last_raw,
        bool& stable_pressed,
        uint32_t& last_change_ms);
    void sendStm32StopCommand();
    void setState(AppState state);
    void scheduleReconnect(const char* reason, bool allow_playback_drain = false);
    void reconnectIfNeeded();
    void serviceAudioUplink();
    void serviceConversationState();
    void printMonitor();

    void handleSpeechStateChanged(bool speaking);
    void handleSocketMessage(const uint8_t* data, size_t len);
    void handleSocketDisconnected();
    void handleServerEvent(const doubao::ServerMessage& message);
    void handleServerAudio(const doubao::ServerMessage& message);

    StatusLed led_;
    AudioPipeline audio_;
    DoubaoWsClient ws_client_;
    OtaManager ota_;
    HardwareSerial aux_serial_;
    doubao::SessionConfig session_config_;

    AppState state_;
    String session_id_;

    bool session_ready_;
    bool tts_ended_;
    bool reconnect_scheduled_;
    bool reconnect_after_playback_;
    bool audio_uplink_enabled_;
    bool api_activation_requested_;
    bool ota_mode_request_seen_;
    bool ota_esp32_button_enabled_;
    bool ota_stm32_button_enabled_;
    bool ota_esp32_button_raw_;
    bool ota_stm32_button_raw_;
    bool ota_esp32_button_pressed_;
    bool ota_stm32_button_pressed_;
    String deferred_reconnect_reason_;

    uint32_t reconnect_at_ms_;
    uint32_t last_audio_rx_ms_;
    uint32_t last_tts_ended_ms_;
    uint32_t last_monitor_ms_;
    uint32_t listening_started_ms_;
    uint32_t thinking_started_ms_;
    uint32_t ota_mode_started_ms_;
    uint32_t ota_last_attempt_ms_;
    uint32_t ota_esp32_button_changed_ms_;
    uint32_t ota_stm32_button_changed_ms_;
    String serial_command_buffer_;
};
