#include "realtime_voice_app.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_random.h>
#include <stdlib.h>

#include "app_config.h"
#include "logger.h"

namespace {

String makeUuid() {
    char buffer[48];
    const uint32_t r1 = esp_random();
    const uint32_t r2 = esp_random();
    const uint32_t r3 = esp_random();
    const uint32_t r4 = esp_random();
    snprintf(
        buffer,
        sizeof(buffer),
        "%08x-%04x-%04x-%04x-%08x%04x",
        r1,
        (r2 >> 16) & 0xFFFF,
        r2 & 0xFFFF,
        (r3 >> 16) & 0xFFFF,
        (r3 & 0xFFFF) | ((r4 & 0xFFFF) << 16),
        r4 >> 16);
    return String(buffer);
}

String summarizePayload(const String& payload, size_t max_len = 120) {
    if (payload.length() <= max_len) {
        return payload;
    }
    return payload.substring(0, max_len) + "...";
}

bool isRecoverableDialogError(const String& payload) {
    return payload.indexOf("AudioASRIdleTimeoutError") >= 0 ||
           payload.indexOf("ClientLackDataError") >= 0 ||
           payload.indexOf("\"status_code\":\"52000009\"") >= 0 ||
           payload.indexOf("\"status_code\":\"52000030\"") >= 0;
}

bool parseIntStrict(const String& text, int& value) {
    if (text.length() == 0) {
        return false;
    }

    char* end = nullptr;
    const long parsed = strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

const char* stateName(AppState state) {
    switch (state) {
        case AppState::Booting:
            return "booting";
        case AppState::WifiConnecting:
            return "wifi";
        case AppState::Standby:
            return "standby";
        case AppState::ApiConnecting:
            return "api";
        case AppState::SessionStarting:
            return "session";
        case AppState::Listening:
            return "listening";
        case AppState::Thinking:
            return "thinking";
        case AppState::Speaking:
            return "speaking";
        case AppState::Error:
            return "error";
        default:
            return "unknown";
    }
}

}  // namespace

RealtimeVoiceApp::RealtimeVoiceApp()
    : led_(app::kRgbLedPin),
      aux_serial_(1),
      state_(AppState::Booting),
      session_ready_(false),
      tts_ended_(false),
      reconnect_scheduled_(false),
      reconnect_after_playback_(false),
      audio_uplink_enabled_(false),
      api_activation_requested_(false),
      reconnect_at_ms_(0),
      last_audio_rx_ms_(0),
      last_tts_ended_ms_(0),
      last_monitor_ms_(0),
      listening_started_ms_(0),
      thinking_started_ms_(0) {
    session_config_.bot_name = app::kBotName;
    session_config_.system_role = app::kSystemRole;
    session_config_.speaking_style = app::kSpeakingStyle;
    session_config_.speaker = app::kSpeaker;
    session_config_.location_city = app::kLocationCity;
    session_config_.model = app::kDialogModel;
    session_config_.recv_timeout_seconds = app::kRecvTimeoutSeconds;
    session_config_.strict_audit = app::kStrictAudit;
}

bool RealtimeVoiceApp::begin() {
    Serial.begin(115200);
    const uint32_t serial_wait_started_ms = millis();
    while (!Serial && (millis() - serial_wait_started_ms) < 1500) {
        delay(10);
    }
    delay(100);

    LOGI("APP", "ESP32-S3 Doubao realtime voice");
    LOGI(
        "APP",
        "heap=%u psram=%u",
        static_cast<unsigned>(ESP.getFreeHeap()),
        static_cast<unsigned>(ESP.getFreePsram()));
    LOGI("APP", "build=%s %s", __DATE__, __TIME__);

    led_.begin();
    setState(AppState::Booting);

    if (!beginAuxSerial()) {
        LOGE("APP", "Aux serial init failed");
        return false;
    }

    if (!audio_.begin()) {
        LOGE("APP", "Audio pipeline init failed");
        return false;
    }
    loadSpeakerVolumePreference();
    printSerialHelp();

    audio_.setSpeechCallback([this](bool speaking) { handleSpeechStateChanged(speaking); });
    ws_client_.setMessageCallback(
        [this](const uint8_t* data, size_t len) { handleSocketMessage(data, len); });
    ws_client_.setDisconnectCallback([this]() { handleSocketDisconnected(); });

    if (!connectWiFi()) {
        scheduleReconnect("initial WiFi connect failed");
        return true;
    }

    setState(AppState::Standby);
    LOGI(
        "APP",
        "WiFi ready, waiting aux serial trigger 0x%02X on UART1 RX=%d TX=%d",
        static_cast<unsigned>(app::kApiTriggerByte),
        app::kAuxSerialRxPin,
        app::kAuxSerialTxPin);
    return true;
}

void RealtimeVoiceApp::loop() {
    ws_client_.loop();
    led_.update();

    serviceSerialCommands();
    serviceAuxSerial();
    serviceAudioUplink();
    serviceConversationState();
    reconnectIfNeeded();
    printMonitor();

    delay(1);
}

bool RealtimeVoiceApp::connectWiFi() {
    setState(AppState::WifiConnecting);

    if (WiFi.status() == WL_CONNECTED) {
        LOGI("WIFI", "Already connected: %s", WiFi.localIP().toString().c_str());
        return true;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(app::kWifiSsid, app::kWifiPassword);

    LOGI("WIFI", "Connecting to %s", app::kWifiSsid);
    const uint32_t start_ms = millis();
    while (WiFi.status() != WL_CONNECTED &&
           (millis() - start_ms) < app::kWifiConnectTimeoutMs) {
        delay(250);
    }

    if (WiFi.status() != WL_CONNECTED) {
        LOGE("WIFI", "Connection timeout");
        return false;
    }

    LOGI(
        "WIFI",
        "Connected ip=%s rssi=%d",
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI());
    return true;
}

bool RealtimeVoiceApp::beginAuxSerial() {
    if (app::kAuxSerialRxPin == app::kAuxSerialTxPin ||
        app::kAuxSerialRxPin < 0 ||
        app::kAuxSerialTxPin < 0) {
        LOGE("APP", "Aux serial pin config invalid");
        return false;
    }

    const int used_pins[] = {
        app::kMicWsPin,
        app::kMicSdPin,
        app::kMicSckPin,
        app::kSpkDinPin,
        app::kSpkBclkPin,
        app::kSpkLrcPin,
        app::kSpkAmpEnablePin,
        app::kRgbLedPin,
    };

    for (const int pin : used_pins) {
        if (app::kAuxSerialRxPin == pin || app::kAuxSerialTxPin == pin) {
            LOGE(
                "APP",
                "Aux serial pin conflict detected RX=%d TX=%d conflict=%d",
                app::kAuxSerialRxPin,
                app::kAuxSerialTxPin,
                pin);
            return false;
        }
    }

    aux_serial_.begin(
        app::kAuxSerialBaud,
        SERIAL_8N1,
        app::kAuxSerialRxPin,
        app::kAuxSerialTxPin);
    LOGI(
        "APP",
        "Aux serial ready baud=%u RX=%d TX=%d",
        static_cast<unsigned>(app::kAuxSerialBaud),
        app::kAuxSerialRxPin,
        app::kAuxSerialTxPin);
    return true;
}

bool RealtimeVoiceApp::connectDoubao() {
    setState(AppState::ApiConnecting);
    session_ready_ = false;
    tts_ended_ = false;
    reconnect_after_playback_ = false;
    audio_uplink_enabled_ = false;
    deferred_reconnect_reason_ = "";
    session_id_ = "";
    last_audio_rx_ms_ = 0;
    last_tts_ended_ms_ = 0;

    ws_client_.begin(app::kWsAppId, app::kWsAccessKey);
    if (!ws_client_.connect()) {
        LOGE("APP", "WebSocket connect failed");
        return false;
    }

    return sendStartConnection();
}

bool RealtimeVoiceApp::sendStartConnection() {
    uint8_t* frame = nullptr;
    size_t frame_len = 0;
    if (!doubao::Protocol::buildStartConnectionFrame(frame, frame_len)) {
        return false;
    }

    const bool ok = ws_client_.sendBinary(frame, frame_len);
    free(frame);
    if (!ok) {
        LOGE("APP", "StartConnection send failed");
        return false;
    }

    LOGI("APP", "StartConnection sent");
    setState(AppState::ApiConnecting);
    return true;
}

bool RealtimeVoiceApp::sendStartSession() {
    session_id_ = makeUuid();

    uint8_t* frame = nullptr;
    size_t frame_len = 0;
    if (!doubao::Protocol::buildStartSessionFrame(session_id_, session_config_, frame, frame_len)) {
        return false;
    }

    const bool ok = ws_client_.sendBinary(frame, frame_len);
    free(frame);
    if (!ok) {
        LOGE("APP", "StartSession send failed");
        return false;
    }

    LOGI("APP", "StartSession sent session_id=%s", session_id_.c_str());
    setState(AppState::SessionStarting);
    return true;
}

bool RealtimeVoiceApp::sendSayHello() {
    uint8_t* frame = nullptr;
    size_t frame_len = 0;
    if (!doubao::Protocol::buildSayHelloFrame(
            session_id_,
            String(app::kHelloText),
            frame,
            frame_len)) {
        return false;
    }

    const bool ok = ws_client_.sendBinary(frame, frame_len);
    free(frame);
    if (!ok) {
        LOGE("APP", "SayHello send failed");
        return false;
    }

    LOGI("APP", "SayHello sent");
    audio_uplink_enabled_ = false;
    setState(AppState::Thinking);
    thinking_started_ms_ = millis();
    return true;
}

void RealtimeVoiceApp::startListening() {
    audio_.stopPlayback(true);
    audio_.startCapture();
    audio_uplink_enabled_ = true;
    tts_ended_ = false;
    listening_started_ms_ = millis();
    setState(AppState::Listening);
    LOGI("APP", "Listening started");
}

void RealtimeVoiceApp::stopListeningForThinking() {
    audio_uplink_enabled_ = true;
    thinking_started_ms_ = millis();
    setState(AppState::Thinking);
    LOGI(
        "APP",
        "Speech end detected, keep streaming until server finalizes ASR, buffered=%uB",
        static_cast<unsigned>(audio_.captureBufferedBytes()));
}

void RealtimeVoiceApp::serviceSerialCommands() {
    while (Serial.available() > 0) {
        const int ch = Serial.read();
        if (ch < 0) {
            break;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            serial_command_buffer_.trim();
            if (serial_command_buffer_.length() > 0) {
                handleSerialCommand(serial_command_buffer_);
                serial_command_buffer_ = "";
            }
            continue;
        }
        if (serial_command_buffer_.length() < 64) {
            serial_command_buffer_ += static_cast<char>(ch);
        }
    }
}

void RealtimeVoiceApp::serviceAuxSerial() {
    while (aux_serial_.available() > 0) {
        const int value = aux_serial_.read();
        if (value < 0) {
            break;
        }

        const uint8_t byte_value = static_cast<uint8_t>(value);
        if (byte_value == app::kApiTriggerByte) {
            requestApiActivation();
            continue;
        }

        aux_serial_.write(&byte_value, 1);
        if (api_activation_requested_ ||
            reconnect_after_playback_ ||
            ws_client_.isConnected()) {
            deactivateApiConnection("UART1 data byte received");
        }
    }
}

void RealtimeVoiceApp::handleSerialCommand(const String& command) {
    String normalized = command;
    normalized.trim();
    normalized.toLowerCase();

    if (normalized == "help" || normalized == "?") {
        printSerialHelp();
        return;
    }

    if (normalized == "mute") {
        applySpeakerVolume(0, true, "serial");
        return;
    }

    if (normalized == "vol" || normalized == "volume") {
        LOGI("APP", "Speaker volume=%u%%", static_cast<unsigned>(audio_.speakerVolumePercent()));
        return;
    }

    if (normalized == "vol +" || normalized == "volume +" ||
        normalized == "vol up" || normalized == "volume up") {
        applySpeakerVolume(
            audio_.speakerVolumePercent() + app::kSpeakerVolumeStepPercent,
            true,
            "serial");
        return;
    }

    if (normalized == "vol -" || normalized == "volume -" ||
        normalized == "vol down" || normalized == "volume down") {
        const int next_volume =
            static_cast<int>(audio_.speakerVolumePercent()) - app::kSpeakerVolumeStepPercent;
        applySpeakerVolume(next_volume, true, "serial");
        return;
    }

    String argument;
    if (normalized.startsWith("vol ")) {
        argument = command.substring(4);
    } else if (normalized.startsWith("volume ")) {
        argument = command.substring(7);
    }

    argument.trim();
    if (argument.length() > 0) {
        int requested_volume = 0;
        if (!parseIntStrict(argument, requested_volume)) {
            LOGW("APP", "Invalid volume command: %s", command.c_str());
            printSerialHelp();
            return;
        }

        applySpeakerVolume(requested_volume, true, "serial");
        return;
    }

    LOGW("APP", "Unknown serial command: %s", command.c_str());
    printSerialHelp();
}

void RealtimeVoiceApp::requestApiActivation() {
    if (api_activation_requested_) {
        return;
    }

    api_activation_requested_ = true;
    LOGI("APP", "Aux serial trigger received, enabling API connect");

    if (WiFi.status() != WL_CONNECTED) {
        scheduleReconnect("aux trigger wifi reconnect");
        return;
    }

    if (!ws_client_.isConnected() && !connectDoubao()) {
        scheduleReconnect("aux trigger API connect failed");
    }
}

void RealtimeVoiceApp::deactivateApiConnection(const char* reason) {
    const bool had_api_activity =
        api_activation_requested_ ||
        reconnect_after_playback_ ||
        ws_client_.isConnected();

    api_activation_requested_ = false;
    reconnect_scheduled_ = false;
    reconnect_after_playback_ = false;
    reconnect_at_ms_ = 0;
    session_ready_ = false;
    tts_ended_ = false;
    audio_uplink_enabled_ = false;
    deferred_reconnect_reason_ = "";
    session_id_ = "";
    last_audio_rx_ms_ = 0;
    last_tts_ended_ms_ = 0;

    audio_.stopCapture();
    audio_.stopPlayback(true);
    ws_client_.disconnect();

    if (WiFi.status() == WL_CONNECTED) {
        setState(AppState::Standby);
    } else {
        setState(AppState::Error);
    }

    if (had_api_activity) {
        LOGI(
            "APP",
            "API disconnected by UART1 request: %s, waiting trigger 0x%02X",
            reason != nullptr ? reason : "manual",
            static_cast<unsigned>(app::kApiTriggerByte));
    }
}

void RealtimeVoiceApp::loadSpeakerVolumePreference() {
    Preferences prefs;
    uint8_t stored_volume = app::kSpeakerVolumeDefaultPercent;
    if (prefs.begin(app::kVolumePrefsNamespace, true)) {
        stored_volume =
            prefs.getUChar(app::kVolumePrefsKey, app::kSpeakerVolumeDefaultPercent);
        prefs.end();
    } else {
        LOGW("APP", "Volume preferences open failed, using default");
    }

    applySpeakerVolume(stored_volume, false, "boot");
}

void RealtimeVoiceApp::saveSpeakerVolumePreference(uint8_t volume_percent) {
    Preferences prefs;
    if (!prefs.begin(app::kVolumePrefsNamespace, false)) {
        LOGW("APP", "Volume preferences save failed");
        return;
    }
    prefs.putUChar(app::kVolumePrefsKey, volume_percent);
    prefs.end();
}

void RealtimeVoiceApp::applySpeakerVolume(int volume_percent, bool persist, const char* source) {
    const uint8_t clamped = static_cast<uint8_t>(constrain(
        volume_percent,
        app::kSpeakerVolumeMinPercent,
        app::kSpeakerVolumeMaxPercent));
    const uint8_t previous = audio_.speakerVolumePercent();
    audio_.setSpeakerVolumePercent(clamped);

    if (persist) {
        saveSpeakerVolumePreference(clamped);
    }

    if (previous != clamped || source != nullptr) {
        LOGI(
            "APP",
            "Speaker volume set to %u%% (%s)",
            static_cast<unsigned>(clamped),
            source != nullptr ? source : "runtime");
        if (clamped > 100) {
            LOGW("APP", "Volume above 100%% may introduce clipping noise");
        }
    }
}

void RealtimeVoiceApp::printSerialHelp() {
    LOGI(
        "APP",
        "Serial cmds: `vol`, `vol 80`, `vol +`, `vol -`, `mute`, `help`");
}

void RealtimeVoiceApp::setState(AppState state) {
    state_ = state;

    switch (state_) {
        case AppState::Booting:
            led_.setState(LedState::Booting);
            break;
        case AppState::WifiConnecting:
            led_.setState(LedState::WifiConnecting);
            break;
        case AppState::Standby:
            led_.setState(LedState::Standby);
            break;
        case AppState::ApiConnecting:
        case AppState::SessionStarting:
            led_.setState(LedState::ApiConnecting);
            break;
        case AppState::Listening:
            led_.setState(LedState::Listening);
            break;
        case AppState::Thinking:
            led_.setState(LedState::Thinking);
            break;
        case AppState::Speaking:
            led_.setState(LedState::Speaking);
            break;
        case AppState::Error:
            led_.setState(LedState::Error);
            break;
        default:
            led_.setState(LedState::Off);
            break;
    }
}

void RealtimeVoiceApp::scheduleReconnect(const char* reason, bool allow_playback_drain) {
    if (allow_playback_drain &&
        state_ == AppState::Speaking &&
        audio_.playbackBufferedBytes() > 0) {
        session_ready_ = false;
        tts_ended_ = true;
        last_tts_ended_ms_ = millis();
        reconnect_after_playback_ = true;
        deferred_reconnect_reason_ = reason;
        audio_.markPlaybackStreamEnded();
        LOGW("APP", "Deferring reconnect until playback completes: %s", reason);
        return;
    }

    LOGW("APP", "Scheduling reconnect: %s", reason);
    session_ready_ = false;
    tts_ended_ = false;
    reconnect_after_playback_ = false;
    audio_uplink_enabled_ = false;
    deferred_reconnect_reason_ = "";
    audio_.stopCapture();
    audio_.stopPlayback(true);
    ws_client_.disconnect();
    setState(AppState::Error);
    reconnect_scheduled_ = true;
    reconnect_at_ms_ = millis() + app::kReconnectDelayMs;
}

void RealtimeVoiceApp::reconnectIfNeeded() {
    if (!reconnect_scheduled_) {
        return;
    }
    if (millis() < reconnect_at_ms_) {
        return;
    }

    reconnect_scheduled_ = false;
    LOGI("APP", "Reconnecting...");

    const bool wifi_ready =
        (WiFi.status() == WL_CONNECTED) || connectWiFi();
    if (!wifi_ready) {
        scheduleReconnect("WiFi reconnect failed");
        return;
    }

    if (!api_activation_requested_) {
        setState(AppState::Standby);
        LOGI(
            "APP",
            "WiFi restored, waiting aux serial trigger 0x%02X",
            static_cast<unsigned>(app::kApiTriggerByte));
        return;
    }

    if (!connectDoubao()) {
        scheduleReconnect("API reconnect failed");
    }
}

void RealtimeVoiceApp::serviceAudioUplink() {
    if (!session_ready_ ||
        !audio_uplink_enabled_ ||
        (state_ != AppState::Listening && state_ != AppState::Thinking)) {
        return;
    }

    uint8_t packet[app::kTxFrameBytes * app::kAudioFramesPerPacket];
    uint8_t ws_frame[app::kAudioWsFrameBufferBytes];
    uint8_t packets_sent = 0;
    while (audio_.captureBufferedBytes() >= app::kTxFrameBytes &&
           packets_sent < app::kAudioPacketsPerLoop) {
        const size_t available_frames =
            audio_.captureBufferedBytes() / app::kTxFrameBytes;
        const size_t frames_to_bundle =
            available_frames >= app::kAudioFramesPerPacket ? app::kAudioFramesPerPacket : 1;
        const size_t bytes_to_read = frames_to_bundle * app::kTxFrameBytes;

        const size_t audio_bytes = audio_.readCaptureAudio(packet, bytes_to_read);
        if (audio_bytes == 0) {
            break;
        }

        size_t ws_frame_len = 0;
        if (!doubao::Protocol::buildAudioFrameInto(
                session_id_,
                packet,
                audio_bytes,
                ws_frame,
                sizeof(ws_frame),
                ws_frame_len)) {
            scheduleReconnect("audio frame build failed");
            return;
        }

        const bool ok = ws_client_.sendBinary(ws_frame, ws_frame_len);
        if (!ok) {
            scheduleReconnect("audio send failed");
            return;
        }

        ++packets_sent;
    }

    if (state_ == AppState::Thinking &&
        packets_sent > 0 &&
        audio_.captureBufferedBytes() == 0) {
        LOGI("APP", "Final speech audio flushed to server");
    }
}

void RealtimeVoiceApp::serviceConversationState() {
    if (state_ == AppState::Speaking) {
        if (tts_ended_ &&
            audio_.playbackBufferedBytes() == 0 &&
            (millis() - last_audio_rx_ms_) > app::kPlaybackDrainGraceMs) {
            if (reconnect_after_playback_) {
                const String reason = deferred_reconnect_reason_;
                reconnect_after_playback_ = false;
                deferred_reconnect_reason_ = "";
                scheduleReconnect(reason.c_str());
                return;
            }
            LOGI("APP", "Playback complete, switching back to listening");
            startListening();
        }
    } else if (state_ == AppState::Thinking) {
        if (tts_ended_ &&
            (millis() - last_tts_ended_ms_) > app::kPlaybackDrainGraceMs &&
            audio_.playbackBufferedBytes() == 0) {
            startListening();
        }
    }
}

void RealtimeVoiceApp::printMonitor() {
    if ((millis() - last_monitor_ms_) < app::kMonitorIntervalMs) {
        return;
    }
    last_monitor_ms_ = millis();

    const AudioStats stats = audio_.stats();
    LOGD(
        "MON",
        "state=%s wifi=%d vol=%u%% tx=%uB rx=%uB rms=%d heap=%u",
        stateName(state_),
        WiFi.RSSI(),
        static_cast<unsigned>(audio_.speakerVolumePercent()),
        static_cast<unsigned>(stats.tx_buffered_bytes),
        static_cast<unsigned>(stats.rx_buffered_bytes),
        stats.last_mic_rms,
        static_cast<unsigned>(ESP.getFreeHeap()));
}

void RealtimeVoiceApp::handleSpeechStateChanged(bool speaking) {
    if (state_ != AppState::Listening) {
        return;
    }

    if (speaking) {
        LOGI("APP", "Speech started");
        return;
    }

    const uint32_t elapsed = millis() - listening_started_ms_;
    LOGI("APP", "Speech ended after %u ms", static_cast<unsigned>(elapsed));
    stopListeningForThinking();
}

void RealtimeVoiceApp::handleSocketMessage(const uint8_t* data, size_t len) {
    doubao::ServerMessage message;
    if (!doubao::Protocol::parseServerMessage(data, len, message)) {
        LOGW("APP", "Failed to parse server frame");
        logutil::hexDump("WSS", data, len);
        return;
    }

    if (message.session_id.length() > 0 &&
        session_id_.length() > 0 &&
        message.session_id != session_id_) {
        LOGW(
            "APP",
            "Ignoring stale session event: msg_session=%s current_session=%s",
            message.session_id.c_str(),
            session_id_.c_str());
        doubao::Protocol::freeServerMessage(message);
        return;
    }

    if (message.is_error) {
        LOGE("APP", "Server error %u %s", static_cast<unsigned>(message.error_code), message.payload_json.c_str());
        doubao::Protocol::freeServerMessage(message);
        scheduleReconnect("server error");
        return;
    }

    if (message.message_type == doubao::kServerAudioResponse) {
        handleServerAudio(message);
    } else if (message.message_type == doubao::kServerFullResponse && message.has_event) {
        handleServerEvent(message);
    }

    doubao::Protocol::freeServerMessage(message);
}

void RealtimeVoiceApp::handleSocketDisconnected() {
    if (reconnect_scheduled_ || reconnect_after_playback_) {
        return;
    }
    if (!api_activation_requested_) {
        if (WiFi.status() == WL_CONNECTED) {
            setState(AppState::Standby);
        }
        return;
    }
    scheduleReconnect("socket disconnected", true);
}

void RealtimeVoiceApp::handleServerEvent(const doubao::ServerMessage& message) {
    LOGD("APP", "Event %u", static_cast<unsigned>(message.event));

    switch (message.event) {
        case doubao::kConnectionStarted:
            LOGI("APP", "Connection started");
            if (!sendStartSession()) {
                scheduleReconnect("start session failed");
            }
            break;

        case doubao::kSessionStarted:
            session_ready_ = true;
            LOGI("APP", "Session started: %s", session_id_.c_str());
            if (app::kAutoSayHello) {
                if (!sendSayHello()) {
                    scheduleReconnect("say hello failed");
                }
            } else {
                startListening();
            }
            break;

        case doubao::kTtsSentenceStart:
            if (state_ == AppState::Thinking) {
                audio_uplink_enabled_ = false;
                audio_.clearCaptureBuffer();
            }
            if (state_ == AppState::Thinking && audio_.isCapturing()) {
                audio_.stopCapture();
                LOGI("APP", "Capture stopped, server started TTS");
            }
            LOGI("APP", "TTS sentence start");
            break;

        case doubao::kTtsSentenceEnd:
            LOGD("APP", "TTS sentence end");
            break;

        case doubao::kTtsEnded:
            tts_ended_ = true;
            last_tts_ended_ms_ = millis();
            audio_.markPlaybackStreamEnded();
            LOGI("APP", "TTS ended");
            break;

        case doubao::kAsrInfo:
            LOGD("APP", "ASR info %s", summarizePayload(message.payload_json).c_str());
            break;
        case doubao::kAsrResponse:
            LOGD("APP", "ASR response %s", summarizePayload(message.payload_json).c_str());
            break;
        case doubao::kAsrEnded:
            if (state_ == AppState::Thinking) {
                audio_uplink_enabled_ = false;
                audio_.clearCaptureBuffer();
            }
            if (state_ == AppState::Thinking && audio_.isCapturing()) {
                audio_.stopCapture();
                LOGI("APP", "Capture stopped, ASR finalized");
            }
            LOGI("APP", "ASR ended %s", summarizePayload(message.payload_json).c_str());
            break;
        case doubao::kUsageResponse:
            if (message.payload_json.length() > 0) {
                LOGI(
                    "APP",
                    "Usage %s",
                    summarizePayload(message.payload_json).c_str());
            }
            break;
        case doubao::kChatResponse:
            LOGD("APP", "Chat response %s", summarizePayload(message.payload_json).c_str());
            break;
        case doubao::kChatEnded:
            LOGI("APP", "Chat ended %s", summarizePayload(message.payload_json).c_str());
            break;

        case doubao::kDialogError:
            if (isRecoverableDialogError(message.payload_json)) {
                LOGW(
                    "APP",
                    "Recoverable dialog error: event=%u payload=%s",
                    static_cast<unsigned>(message.event),
                    summarizePayload(message.payload_json).c_str());
                if (state_ != AppState::Speaking) {
                    startListening();
                }
                break;
            }
            LOGW(
                "APP",
                "Conversation event failed: event=%u payload=%s",
                static_cast<unsigned>(message.event),
                summarizePayload(message.payload_json).c_str());
            scheduleReconnect("conversation event failed");
            break;
        case doubao::kConnectionFailed:
        case doubao::kConnectionFinished:
        case doubao::kSessionFailed:
        case doubao::kSessionFinished:
            LOGW(
                "APP",
                "Conversation event failed: event=%u payload=%s",
                static_cast<unsigned>(message.event),
                summarizePayload(message.payload_json).c_str());
            scheduleReconnect("conversation event failed");
            break;

        default:
            break;
    }
}

void RealtimeVoiceApp::handleServerAudio(const doubao::ServerMessage& message) {
    if (message.payload == nullptr || message.payload_length == 0) {
        return;
    }

    if (audio_.isCapturing()) {
        audio_.stopCapture();
    }
    audio_uplink_enabled_ = false;
    audio_.clearCaptureBuffer();

    audio_.pushPlaybackAudio(message.payload, message.payload_length);
    last_audio_rx_ms_ = millis();
    tts_ended_ = false;

    if (state_ != AppState::Speaking) {
        setState(AppState::Speaking);
        const uint32_t latency = millis() - thinking_started_ms_;
        LOGI("APP", "First audio packet in %u ms", static_cast<unsigned>(latency));
    }
}
