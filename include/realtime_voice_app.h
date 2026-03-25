#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

#include "audio_pipeline.h"
#include "doubao_protocol.h"
#include "doubao_ws_client.h"
#include "status_led.h"

enum class AppState {
    Booting,
    WifiConnecting,
    Standby,
    ApiConnecting,
    SessionStarting,
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
    bool sendStartConnection();
    bool sendStartSession();
    bool sendSayHello();
    void startListening();
    void stopListeningForThinking();
    void serviceSerialCommands();
    void serviceAuxSerial();
    void handleSerialCommand(const String& command);
    void requestApiActivation();
    void deactivateApiConnection(const char* reason);
    void loadSpeakerVolumePreference();
    void saveSpeakerVolumePreference(uint8_t volume_percent);
    void applySpeakerVolume(int volume_percent, bool persist, const char* source);
    void printSerialHelp();
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
    String deferred_reconnect_reason_;

    uint32_t reconnect_at_ms_;
    uint32_t last_audio_rx_ms_;
    uint32_t last_tts_ended_ms_;
    uint32_t last_monitor_ms_;
    uint32_t listening_started_ms_;
    uint32_t thinking_started_ms_;
    String serial_command_buffer_;
};
