#include "realtime_voice_app.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
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

constexpr char kOtaPrefsNamespace[] = "ota";
constexpr char kOtaEsp32VersionKey[] = "esp32_ver";
constexpr char kOtaStm32VersionKey[] = "stm32_ver";
constexpr int kInternalMemoryPins[] = {35, 36, 37};

String takeToken(String& text) {
    text.trim();
    if (text.length() == 0) {
        return "";
    }

    const int space_index = text.indexOf(' ');
    if (space_index < 0) {
        String token = text;
        text = "";
        token.trim();
        return token;
    }

    String token = text.substring(0, space_index);
    text = text.substring(space_index + 1);
    token.trim();
    text.trim();
    return token;
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
        case AppState::Ota:
            return "ota";
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

const char* defaultOtaUrl(OtaTarget target) {
    switch (target) {
        case OtaTarget::Esp32Self:
            return app::kOtaEsp32DefaultUrl;
        case OtaTarget::Stm32:
            return app::kOtaStm32DefaultUrl;
        default:
            return "";
    }
}

const char* defaultOtaSha256(OtaTarget target) {
    switch (target) {
        case OtaTarget::Esp32Self:
            return app::kOtaEsp32DefaultSha256;
        case OtaTarget::Stm32:
            return app::kOtaStm32DefaultSha256;
        default:
            return "";
    }
}

const char* defaultOtaManifestUrl(OtaTarget target) {
    switch (target) {
        case OtaTarget::Esp32Self:
            return app::kOtaEsp32ManifestUrl;
        case OtaTarget::Stm32:
            return app::kOtaStm32ManifestUrl;
        default:
            return "";
    }
}

const char* compiledOtaVersion(OtaTarget target) {
    switch (target) {
        case OtaTarget::Esp32Self:
            return app::kOtaEsp32CurrentVersion;
        case OtaTarget::Stm32:
            return app::kOtaStm32CurrentVersion;
        default:
            return "";
    }
}

const char* otaVersionKey(OtaTarget target) {
    switch (target) {
        case OtaTarget::Esp32Self:
            return kOtaEsp32VersionKey;
        case OtaTarget::Stm32:
            return kOtaStm32VersionKey;
        default:
            return "";
    }
}

String trimCopy(String value) {
    value.trim();
    return value;
}

String currentOtaVersion(OtaTarget target) {
    Preferences prefs;
    if (prefs.begin(kOtaPrefsNamespace, false)) {
        String stored = "";
        if (prefs.isKey(otaVersionKey(target))) {
            stored = trimCopy(prefs.getString(otaVersionKey(target), ""));
        }
        prefs.end();
        if (stored.length() > 0) {
            return stored;
        }
    }

    return trimCopy(String(compiledOtaVersion(target)));
}

bool isBoardReservedPin(int pin) {
    for (const int reserved_pin : kInternalMemoryPins) {
        if (pin == reserved_pin) {
            return true;
        }
    }
    return false;
}

bool saveInstalledOtaVersion(OtaTarget target, const String& version) {
    const String normalized = trimCopy(version);
    if (normalized.length() == 0) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(kOtaPrefsNamespace, false)) {
        return false;
    }

    const bool ok = prefs.putString(otaVersionKey(target), normalized) == normalized.length();
    prefs.end();
    return ok;
}

String makeAbsoluteOtaUrl(const String& value) {
    if (value.startsWith("http://") || value.startsWith("https://")) {
        return value;
    }

    if (value.length() == 0) {
        return "";
    }

    if (value.startsWith("/")) {
        return String(app::kOtaServerBaseUrl) + value;
    }

    return String(app::kOtaServerBaseUrl) + "/" + value;
}

struct OtaRequestResolution {
    String url;
    String sha256;
    String version;
    String md5;
    String update_log;
    bool manifest_used = false;
    bool already_latest = false;
};

bool fetchManifestResolution(
    OtaTarget target,
    OtaRequestResolution& resolution,
    String& error) {
    const char* manifest_url = defaultOtaManifestUrl(target);
    if (manifest_url == nullptr || manifest_url[0] == '\0') {
        error = "manifest disabled";
        return false;
    }

    WiFiClient client;
    HTTPClient http;
    if (!http.begin(client, manifest_url)) {
        error = "manifest connect failed";
        return false;
    }

    http.setTimeout(app::kOtaHttpTimeoutMs);
    const int http_code = http.GET();
    if (http_code != HTTP_CODE_OK) {
        error = String("manifest GET failed: ") + http_code;
        http.end();
        return false;
    }

    const String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(app::kOtaManifestJsonBytes);
    const DeserializationError json_error = deserializeJson(doc, payload);
    if (json_error) {
        error = String("manifest JSON parse failed: ") + json_error.c_str();
        return false;
    }

    JsonObjectConst root = doc.as<JsonObjectConst>();
    if (root.isNull()) {
        error = "manifest root is not object";
        return false;
    }

    JsonObjectConst manifest = root;
    const char* target_key = target == OtaTarget::Esp32Self ? "esp32" : "stm32";
    if (root[target_key].is<JsonObjectConst>()) {
        manifest = root[target_key].as<JsonObjectConst>();
    }

    resolution.version = trimCopy(String(manifest["version"] | ""));
    resolution.url = trimCopy(String(manifest["firmware_url"] | ""));
    if (resolution.url.length() == 0) {
        resolution.url = trimCopy(String(manifest["url"] | ""));
    }
    if (resolution.url.length() == 0) {
        resolution.url = trimCopy(String(manifest["firmware_name"] | ""));
    }
    resolution.url = makeAbsoluteOtaUrl(resolution.url);
    resolution.sha256 = trimCopy(String(manifest["sha256"] | ""));
    if (resolution.sha256.length() == 0) {
        resolution.sha256 = trimCopy(String(manifest["sha256sum"] | ""));
    }
    resolution.md5 = trimCopy(String(manifest["md5"] | ""));
    resolution.update_log = trimCopy(String(manifest["update_log"] | ""));
    resolution.manifest_used = true;

    if (resolution.url.length() == 0) {
        error = "manifest missing firmware_url";
        return false;
    }

    const String current_version = currentOtaVersion(target);
    if (current_version.length() > 0 &&
        resolution.version.length() > 0 &&
        resolution.version.equalsIgnoreCase(current_version)) {
        resolution.already_latest = true;
    }

    return true;
}

bool resolveConfiguredOtaRequest(
    OtaTarget target,
    OtaRequestResolution& resolution,
    String& detail) {
    resolution = OtaRequestResolution();
    resolution.url = String(defaultOtaUrl(target));
    resolution.sha256 = String(defaultOtaSha256(target));

    OtaRequestResolution manifest_resolution;
    String manifest_error;
    if (fetchManifestResolution(target, manifest_resolution, manifest_error)) {
        resolution = manifest_resolution;
        detail = String("manifest=") + defaultOtaManifestUrl(target);
        return true;
    }

    if (resolution.url.length() > 0) {
        detail = manifest_error.length() > 0
                     ? String("manifest unavailable, fallback direct: ") + manifest_error
                     : "direct package";
        return true;
    }

    detail = manifest_error.length() > 0 ? manifest_error : "OTA package is not configured";
    return false;
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
      ota_mode_request_seen_(false),
      ota_esp32_button_enabled_(false),
      ota_stm32_button_enabled_(false),
      ota_esp32_button_raw_(false),
      ota_stm32_button_raw_(false),
      ota_esp32_button_pressed_(false),
      ota_stm32_button_pressed_(false),
      reconnect_at_ms_(0),
      last_audio_rx_ms_(0),
      last_tts_ended_ms_(0),
      last_monitor_ms_(0),
      listening_started_ms_(0),
      thinking_started_ms_(0),
      ota_mode_started_ms_(0),
      ota_last_attempt_ms_(0),
      ota_esp32_button_changed_ms_(0),
      ota_stm32_button_changed_ms_(0) {
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
    const String installed_esp32_version = currentOtaVersion(OtaTarget::Esp32Self);
    const String installed_stm32_version = currentOtaVersion(OtaTarget::Stm32);
    LOGI("APP", "firmware=%s build=%s %s", installed_esp32_version.c_str(), __DATE__, __TIME__);
    LOGI(
        "APP",
        "versions esp32=%s stm32=%s",
        installed_esp32_version.c_str(),
        installed_stm32_version.c_str());

    led_.begin();
    setState(AppState::Booting);

    if (!beginAuxSerial()) {
        LOGE("APP", "Aux serial init failed");
        return false;
    }
    beginOtaButtons();

    if (!audio_.begin()) {
        LOGE("APP", "Audio pipeline init failed");
        return false;
    }
    ota_.begin(aux_serial_);
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
    serviceOtaButtons();

    if (state_ == AppState::Ota) {
        led_.update();
        serviceSerialCommands();
        serviceOtaMode();
        delay(1);
        return;
    }

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

void RealtimeVoiceApp::beginOtaButtons() {
    const int reserved_pins[] = {
        app::kMicWsPin,
        app::kMicSdPin,
        app::kMicSckPin,
        app::kSpkDinPin,
        app::kSpkBclkPin,
        app::kSpkLrcPin,
        app::kSpkAmpEnablePin,
        app::kRgbLedPin,
        app::kAuxSerialRxPin,
        app::kAuxSerialTxPin,
        app::kStm32Boot0Pin,
        app::kStm32Boot1Pin,
        app::kStm32ResetPin,
    };

    auto configure_button = [&](int pin, const char* name, bool& enabled) {
        enabled = false;
        if (pin < 0) {
            LOGW("OTA", "%s button disabled, pin not configured", name);
            return;
        }

        if (isBoardReservedPin(pin)) {
            LOGE(
                "OTA",
                "%s button pin %d is reserved by internal flash/PSRAM on ESP32-S3 N16R8; choose another GPIO",
                name,
                pin);
            return;
        }

        for (const int reserved_pin : reserved_pins) {
            if (pin >= 0 && pin == reserved_pin) {
                LOGE("OTA", "%s button pin %d conflicts with active hardware", name, pin);
                return;
            }
        }

        pinMode(
            pin,
            app::kOtaButtonActiveLevel == LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
        enabled = true;
        LOGI("OTA", "%s button ready on GPIO%d", name, pin);
    };

    if (app::kOtaEsp32ButtonPin == app::kOtaStm32ButtonPin &&
        app::kOtaEsp32ButtonPin >= 0) {
        LOGE(
            "OTA",
            "OTA button pins conflict: esp32=%d stm32=%d",
            app::kOtaEsp32ButtonPin,
            app::kOtaStm32ButtonPin);
    } else {
        configure_button(app::kOtaEsp32ButtonPin, "ESP32 OTA", ota_esp32_button_enabled_);
        configure_button(app::kOtaStm32ButtonPin, "STM32 OTA", ota_stm32_button_enabled_);
    }

    ota_esp32_button_raw_ =
        ota_esp32_button_enabled_ && readOtaButton(app::kOtaEsp32ButtonPin);
    ota_stm32_button_raw_ =
        ota_stm32_button_enabled_ && readOtaButton(app::kOtaStm32ButtonPin);
    ota_esp32_button_pressed_ = ota_esp32_button_raw_;
    ota_stm32_button_pressed_ = ota_stm32_button_raw_;
    ota_esp32_button_changed_ms_ = millis();
    ota_stm32_button_changed_ms_ = millis();
}

bool RealtimeVoiceApp::readOtaButton(int pin) const {
    if (pin < 0) {
        return false;
    }
    return digitalRead(pin) == app::kOtaButtonActiveLevel;
}

bool RealtimeVoiceApp::detectButtonPress(
    int pin,
    bool enabled,
    bool& last_raw,
    bool& stable_pressed,
    uint32_t& last_change_ms) {
    if (!enabled) {
        return false;
    }

    const bool raw_pressed = readOtaButton(pin);
    if (raw_pressed != last_raw) {
        last_raw = raw_pressed;
        last_change_ms = millis();
    }

    if ((millis() - last_change_ms) < app::kOtaButtonDebounceMs) {
        return false;
    }

    if (raw_pressed == stable_pressed) {
        return false;
    }

    stable_pressed = raw_pressed;
    return stable_pressed;
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
        if (serial_command_buffer_.length() < app::kSerialCommandMaxLength) {
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

    if (normalized == "ota" || normalized.startsWith("ota ")) {
        handleOtaCommand(command);
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

void RealtimeVoiceApp::serviceOtaButtons() {
    if (detectButtonPress(
            app::kOtaEsp32ButtonPin,
            ota_esp32_button_enabled_,
            ota_esp32_button_raw_,
            ota_esp32_button_pressed_,
            ota_esp32_button_changed_ms_)) {
        enterOtaMode(OtaTarget::Esp32Self, "esp32 ota button");
    }

    if (detectButtonPress(
            app::kOtaStm32ButtonPin,
            ota_stm32_button_enabled_,
            ota_stm32_button_raw_,
            ota_stm32_button_pressed_,
            ota_stm32_button_changed_ms_)) {
        enterOtaMode(OtaTarget::Stm32, "stm32 ota button");
    }
}

void RealtimeVoiceApp::serviceOtaMode() {
    if (state_ != AppState::Ota || ota_mode_request_seen_) {
        return;
    }

    const uint32_t now = millis();
    if ((now - ota_mode_started_ms_) >= app::kOtaRequestWindowMs) {
        exitOtaMode("OTA request timeout");
        return;
    }

    if ((now - ota_last_attempt_ms_) < app::kOtaAutoRetryIntervalMs) {
        return;
    }

    ota_last_attempt_ms_ = now;
    const OtaTarget target = ota_.selectedTarget();
    OtaRequestResolution resolution;
    String detail;
    if (!resolveConfiguredOtaRequest(target, resolution, detail)) {
        LOGW("OTA", "Auto resolve failed: %s", detail.c_str());
        return;
    }

    if (resolution.manifest_used) {
        const String installed_version = currentOtaVersion(target);
        LOGI(
            "OTA",
            "Manifest target=%s current=%s latest=%s",
            ota_.selectedTargetName(),
            installed_version.c_str(),
            resolution.version.length() > 0 ? resolution.version.c_str() : "(unknown)");
        if (resolution.update_log.length() > 0) {
            LOGI("OTA", "Update log: %s", resolution.update_log.c_str());
        }
    } else if (detail.length() > 0) {
        LOGI("OTA", "%s", detail.c_str());
    }

    if (resolution.already_latest) {
        const String installed_version = currentOtaVersion(target);
        LOGI(
            "OTA",
            "Target=%s already at latest version=%s",
            ota_.selectedTargetName(),
            installed_version.c_str());
        exitOtaMode("OTA already current");
        return;
    }

    LOGI("OTA", "Auto pull target=%s url=%s", ota_.selectedTargetName(), resolution.url.c_str());
    if (runOtaUpdate(target, resolution.url, resolution.sha256, resolution.version)) {
        ota_mode_request_seen_ = true;
        exitOtaMode("OTA finished");
        return;
    }

    setState(AppState::Ota);
    LOGW("OTA", "Auto pull failed: %s", ota_.lastError().c_str());
}

void RealtimeVoiceApp::sendStm32StopCommand() {
    const uint8_t stop_byte = app::kStm32StopCommandByte;
    aux_serial_.write(&stop_byte, 1);
    aux_serial_.flush();
    LOGI("OTA", "Sent STM32 stop command 0x%02X", static_cast<unsigned>(stop_byte));
}

bool RealtimeVoiceApp::enterOtaMode(OtaTarget target, const char* reason) {
    if (state_ != AppState::Ota) {
        if (!prepareForOta(reason)) {
            return false;
        }
        if (target != OtaTarget::Stm32) {
            sendStm32StopCommand();
        }
    }

    ota_.setSelectedTarget(target);
    ota_mode_request_seen_ = false;
    ota_mode_started_ms_ = millis();
    ota_last_attempt_ms_ = ota_mode_started_ms_ - app::kOtaAutoRetryIntervalMs;
    setState(AppState::Ota);

    LOGI(
        "OTA",
        "Mode=%s armed for %u ms, source=%s",
        ota_.selectedTargetName(),
        static_cast<unsigned>(app::kOtaRequestWindowMs),
        defaultOtaManifestUrl(target)[0] != '\0' ? defaultOtaManifestUrl(target) : defaultOtaUrl(target));
    if (WiFi.status() == WL_CONNECTED) {
        LOGI("OTA", "WiFi ip=%s", WiFi.localIP().toString().c_str());
    }
    return true;
}

void RealtimeVoiceApp::exitOtaMode(const char* reason) {
    LOGI("OTA", "Exiting OTA mode: %s", reason != nullptr ? reason : "done");
    delay(100);
    ESP.restart();
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
    if (prefs.begin(app::kVolumePrefsNamespace, false)) {
        if (prefs.isKey(app::kVolumePrefsKey)) {
            stored_volume =
                prefs.getUChar(app::kVolumePrefsKey, app::kSpeakerVolumeDefaultPercent);
        }
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
        "Serial cmds: `vol`, `vol 80`, `vol +`, `vol -`, `mute`, `ota ...`, `help`");
    LOGI(
        "APP",
        "OTA trigger: press ESP32/STM32 OTA button -> auto read manifest/direct package within 5s");
}

void RealtimeVoiceApp::printOtaHelp() {
    LOGI(
        "OTA",
        "OTA flow: press ESP32/STM32 OTA button -> auto read manifest, compare version, then update");
    LOGI(
        "OTA",
        "OTA cmds: `ota status`, `ota start [url] [sha256] [version]`, `ota target esp|stm`, `ota exit`");
    LOGI(
        "OTA",
        "Server=%s esp32_manifest=%s stm32_manifest=%s",
        app::kOtaServerBaseUrl,
        app::kOtaEsp32ManifestUrl,
        app::kOtaStm32ManifestUrl[0] == '\0' ? "(disabled)" : app::kOtaStm32ManifestUrl);
    LOGI(
        "OTA",
        "Current versions: esp32=%s stm32=%s",
        currentOtaVersion(OtaTarget::Esp32Self).c_str(),
        currentOtaVersion(OtaTarget::Stm32).c_str());
    LOGI(
        "OTA",
        "Fallback urls: esp32=%s stm32=%s",
        app::kOtaEsp32DefaultUrl,
        app::kOtaStm32DefaultUrl);
    LOGI(
        "OTA",
        "Buttons esp32=%d stm32=%d active_level=%d auto_stm32=%s BOOT0=%d NRST=%d BOOT1=%d",
        app::kOtaEsp32ButtonPin,
        app::kOtaStm32ButtonPin,
        app::kOtaButtonActiveLevel,
        ota_.canAutoEnterStm32Bootloader() ? "yes" : "no",
        app::kStm32Boot0Pin,
        app::kStm32ResetPin,
        app::kStm32Boot1Pin);
    LOGI("OTA", "ESP32-S3 N16R8 reserved GPIOs for internal memory: 35, 36, 37");
}

bool RealtimeVoiceApp::prepareForOta(const char* reason) {
    LOGI("OTA", "Preparing maintenance mode: %s", reason != nullptr ? reason : "manual");
    deactivateApiConnection("OTA requested");
    setState(AppState::Ota);

    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }

    if (!connectWiFi()) {
        setState(AppState::Error);
        LOGE("OTA", "WiFi reconnect failed");
        return false;
    }

    setState(AppState::Ota);
    return true;
}

bool RealtimeVoiceApp::runOtaUpdate(
    OtaTarget target,
    const String& url,
    const String& expected_sha256,
    const String& resolved_version) {
    const bool keep_ota_mode = (state_ == AppState::Ota);
    if (url.length() == 0) {
        LOGE("OTA", "OTA URL is empty");
        return false;
    }

    if (state_ != AppState::Ota) {
        if (!prepareForOta(target == OtaTarget::Esp32Self ? "esp32 update" : "stm32 update")) {
            return false;
        }
    } else if (WiFi.status() != WL_CONNECTED) {
        if (!connectWiFi()) {
            setState(AppState::Error);
            LOGE("OTA", "WiFi reconnect failed before OTA");
            return false;
        }
        setState(AppState::Ota);
    }

    ota_.setSelectedTarget(target);
    const bool ok = ota_.runSelectedUpdate(url, expected_sha256);

    if (ok && resolved_version.length() > 0) {
        if (saveInstalledOtaVersion(target, resolved_version)) {
            LOGI("OTA", "Recorded %s installed version=%s", ota_.selectedTargetName(), resolved_version.c_str());
        } else {
            LOGW("OTA", "Failed to persist %s installed version=%s", ota_.selectedTargetName(), resolved_version.c_str());
        }
    }

    if (target == OtaTarget::Esp32Self) {
        if (!ok) {
            setState(
                keep_ota_mode ? AppState::Ota
                              : (WiFi.status() == WL_CONNECTED ? AppState::Standby : AppState::Error));
        }
        return ok;
    }

    if (ok) {
        setState(
            keep_ota_mode ? AppState::Ota
                          : (WiFi.status() == WL_CONNECTED ? AppState::Standby : AppState::Error));
        LOGI("OTA", "STM32 OTA finished");
        return true;
    }

    setState(
        keep_ota_mode ? AppState::Ota
                      : (WiFi.status() == WL_CONNECTED ? AppState::Standby : AppState::Error));
    LOGE("OTA", "STM32 OTA failed: %s", ota_.lastError().c_str());
    return false;
}

void RealtimeVoiceApp::handleOtaCommand(const String& command) {
    String payload = command;
    payload.trim();
    if (payload.length() >= 3) {
        payload = payload.substring(3);
    } else {
        payload = "";
    }
    payload.trim();

    if (payload.length() == 0 || payload.equalsIgnoreCase("help")) {
        printOtaHelp();
        return;
    }

    String action = takeToken(payload);
    String action_lower = action;
    action_lower.toLowerCase();

    if (action_lower == "status") {
        const uint32_t elapsed_ms = millis() - ota_mode_started_ms_;
        const uint32_t remaining_ms =
            elapsed_ms >= app::kOtaRequestWindowMs ? 0 : (app::kOtaRequestWindowMs - elapsed_ms);
        LOGI(
            "OTA",
            "state=%s target=%s wifi=%s request_seen=%s remaining_ms=%u",
            state_ == AppState::Ota ? "armed" : "idle",
            ota_.selectedTargetName(),
            WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
            ota_mode_request_seen_ ? "yes" : "no",
            static_cast<unsigned>(remaining_ms));
        LOGI(
            "OTA",
            "versions esp32=%s stm32=%s manifests esp32=%s stm32=%s",
            currentOtaVersion(OtaTarget::Esp32Self).c_str(),
            currentOtaVersion(OtaTarget::Stm32).c_str(),
            app::kOtaEsp32ManifestUrl,
            app::kOtaStm32ManifestUrl[0] == '\0' ? "(disabled)" : app::kOtaStm32ManifestUrl);
        return;
    }

    if (action_lower == "exit") {
        if (state_ != AppState::Ota) {
            LOGW("OTA", "OTA mode is not active");
            return;
        }
        exitOtaMode("serial exit");
        return;
    }

    if (action_lower == "target") {
        if (state_ != AppState::Ota) {
            LOGW("OTA", "Press the OTA button first");
            return;
        }
        String target = takeToken(payload);
        target.toLowerCase();
        if (target == "esp" || target == "esp32") {
            ota_.setSelectedTarget(OtaTarget::Esp32Self);
            ota_mode_started_ms_ = millis();
            ota_last_attempt_ms_ = ota_mode_started_ms_ - app::kOtaAutoRetryIntervalMs;
            LOGI("OTA", "Selected OTA target=esp32");
            return;
        }
        if (target == "stm" || target == "stm32") {
            ota_.setSelectedTarget(OtaTarget::Stm32);
            ota_mode_started_ms_ = millis();
            ota_last_attempt_ms_ = ota_mode_started_ms_ - app::kOtaAutoRetryIntervalMs;
            LOGI("OTA", "Selected OTA target=stm32");
            return;
        }
        LOGW("OTA", "Unknown OTA target: %s", target.c_str());
        printOtaHelp();
        return;
    }

    if (action_lower == "start") {
        if (state_ != AppState::Ota) {
            LOGW("OTA", "Press the OTA button first");
            return;
        }
        String url = takeToken(payload);
        String sha256 = takeToken(payload);
        if (url.length() == 0) {
            OtaRequestResolution resolution;
            String detail;
            if (!resolveConfiguredOtaRequest(ota_.selectedTarget(), resolution, detail)) {
                LOGW("OTA", "Manual resolve failed: %s", detail.c_str());
                return;
            }
            if (resolution.manifest_used) {
                const String installed_version = currentOtaVersion(ota_.selectedTarget());
                LOGI(
                    "OTA",
                    "Manual manifest latest=%s current=%s",
                    resolution.version.c_str(),
                    installed_version.c_str());
            } else if (detail.length() > 0) {
                LOGI("OTA", "%s", detail.c_str());
            }
            if (resolution.already_latest) {
                exitOtaMode("OTA already current");
                return;
            }
            url = resolution.url;
            if (sha256.length() == 0) {
                sha256 = resolution.sha256;
            }
            if (payload.length() == 0) {
                payload = resolution.version;
            }
        }
        if (sha256.length() == 0) {
            sha256 = defaultOtaSha256(ota_.selectedTarget());
        }
        ota_mode_request_seen_ = true;
        const String resolved_version = trimCopy(payload);
        if (runOtaUpdate(ota_.selectedTarget(), url, sha256, resolved_version)) {
            exitOtaMode("OTA finished");
            return;
        }
        ota_mode_request_seen_ = false;
        ota_mode_started_ms_ = millis();
        ota_last_attempt_ms_ = ota_mode_started_ms_ - app::kOtaAutoRetryIntervalMs;
        setState(AppState::Ota);
        LOGW("OTA", "Manual OTA failed: %s", ota_.lastError().c_str());
        return;
    }

    if (action_lower == "esp" || action_lower == "esp32") {
        if (state_ != AppState::Ota) {
            LOGW("OTA", "Press the ESP32 OTA button first");
            return;
        }
        String url = takeToken(payload);
        String sha256 = takeToken(payload);
        if (url.length() == 0) {
            OtaRequestResolution resolution;
            String detail;
            if (!resolveConfiguredOtaRequest(OtaTarget::Esp32Self, resolution, detail)) {
                LOGW("OTA", "Manual ESP32 resolve failed: %s", detail.c_str());
                return;
            }
            if (resolution.manifest_used) {
                const String installed_version = currentOtaVersion(OtaTarget::Esp32Self);
                LOGI(
                    "OTA",
                    "ESP32 manifest latest=%s current=%s",
                    resolution.version.c_str(),
                    installed_version.c_str());
            } else if (detail.length() > 0) {
                LOGI("OTA", "%s", detail.c_str());
            }
            if (resolution.already_latest) {
                exitOtaMode("OTA already current");
                return;
            }
            url = resolution.url;
            if (sha256.length() == 0) {
                sha256 = resolution.sha256;
            }
            if (payload.length() == 0) {
                payload = resolution.version;
            }
        }
        if (sha256.length() == 0) {
            sha256 = defaultOtaSha256(OtaTarget::Esp32Self);
        }
        ota_mode_request_seen_ = true;
        const String resolved_version = trimCopy(payload);
        if (runOtaUpdate(OtaTarget::Esp32Self, url, sha256, resolved_version)) {
            exitOtaMode("OTA finished");
            return;
        }
        ota_mode_request_seen_ = false;
        ota_mode_started_ms_ = millis();
        ota_last_attempt_ms_ = ota_mode_started_ms_ - app::kOtaAutoRetryIntervalMs;
        setState(AppState::Ota);
        LOGW("OTA", "Manual ESP32 OTA failed: %s", ota_.lastError().c_str());
        return;
    }

    if (action_lower == "stm" || action_lower == "stm32") {
        if (state_ != AppState::Ota) {
            LOGW("OTA", "Press the STM32 OTA button first");
            return;
        }
        String url = takeToken(payload);
        String sha256 = takeToken(payload);
        if (url.length() == 0) {
            OtaRequestResolution resolution;
            String detail;
            if (!resolveConfiguredOtaRequest(OtaTarget::Stm32, resolution, detail)) {
                LOGW("OTA", "Manual STM32 resolve failed: %s", detail.c_str());
                return;
            }
            if (resolution.manifest_used) {
                const String installed_version = currentOtaVersion(OtaTarget::Stm32);
                LOGI(
                    "OTA",
                    "STM32 manifest latest=%s current=%s",
                    resolution.version.c_str(),
                    installed_version.c_str());
            } else if (detail.length() > 0) {
                LOGI("OTA", "%s", detail.c_str());
            }
            if (resolution.already_latest) {
                exitOtaMode("OTA already current");
                return;
            }
            url = resolution.url;
            if (sha256.length() == 0) {
                sha256 = resolution.sha256;
            }
            if (payload.length() == 0) {
                payload = resolution.version;
            }
        }
        if (sha256.length() == 0) {
            sha256 = defaultOtaSha256(OtaTarget::Stm32);
        }
        ota_mode_request_seen_ = true;
        const String resolved_version = trimCopy(payload);
        if (runOtaUpdate(OtaTarget::Stm32, url, sha256, resolved_version)) {
            exitOtaMode("OTA finished");
            return;
        }
        ota_mode_request_seen_ = false;
        ota_mode_started_ms_ = millis();
        ota_last_attempt_ms_ = ota_mode_started_ms_ - app::kOtaAutoRetryIntervalMs;
        setState(AppState::Ota);
        LOGW("OTA", "Manual STM32 OTA failed: %s", ota_.lastError().c_str());
        return;
    }

    LOGW("OTA", "Unknown OTA command: %s", command.c_str());
    printOtaHelp();
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
        case AppState::Ota:
            led_.setState(LedState::Ota);
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
