#include "ota_manager.h"

#include <HTTPClient.h>
#include <SPIFFS.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include <string.h>

#include "app_config.h"
#include "logger.h"

namespace {

constexpr char kStagingPath[] = "/stm32_ota.bin";
constexpr uint8_t kStm32Ack = 0x79;
constexpr uint8_t kStm32Nack = 0x1F;
constexpr uint8_t kStm32SyncByte = 0x7F;
constexpr uint8_t kStm32AppBootRequest[] = {0x5A, 0xA5, 0x5A, 0xA5};
constexpr uint8_t kStm32AppBootRequestAttempts = 3;
constexpr uint8_t kStm32GetCommand = 0x00;
constexpr uint8_t kStm32GetIdCommand = 0x02;
constexpr uint8_t kStm32ReadMemoryCommand = 0x11;
constexpr uint8_t kStm32GoCommand = 0x21;
constexpr uint8_t kStm32WriteMemoryCommand = 0x31;
constexpr uint8_t kStm32EraseMemoryCommand = 0x43;
constexpr uint8_t kStm32ExtendedEraseCommand = 0x44;

size_t align4(size_t value) {
    return (value + 3U) & ~static_cast<size_t>(3U);
}

String toLowerTrimmed(const String& input) {
    String normalized = input;
    normalized.trim();
    normalized.toLowerCase();
    return normalized;
}

bool isHexChar(char ch) {
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

bool normalizeSha256(const String& input, String& normalized, String& error) {
    normalized = toLowerTrimmed(input);
    if (normalized.length() == 0) {
        return true;
    }
    if (normalized.length() != 64) {
        error = "SHA256 must be 64 hex characters";
        return false;
    }
    for (size_t i = 0; i < static_cast<size_t>(normalized.length()); ++i) {
        if (!isHexChar(normalized[i])) {
            error = "SHA256 contains non-hex characters";
            return false;
        }
    }
    return true;
}

String sha256Hex(const uint8_t digest[32]) {
    static constexpr char kHex[] = "0123456789abcdef";
    char output[65] = {0};
    for (size_t i = 0; i < 32; ++i) {
        output[i * 2] = kHex[(digest[i] >> 4) & 0x0F];
        output[i * 2 + 1] = kHex[digest[i] & 0x0F];
    }
    return String(output);
}

class Sha256Accumulator {
public:
    Sha256Accumulator() : finished_(false) {
        mbedtls_sha256_init(&ctx_);
        mbedtls_sha256_starts_ret(&ctx_, 0);
    }

    ~Sha256Accumulator() {
        mbedtls_sha256_free(&ctx_);
    }

    void update(const uint8_t* data, size_t len) {
        if (finished_ || data == nullptr || len == 0) {
            return;
        }
        mbedtls_sha256_update_ret(&ctx_, data, len);
    }

    String finishHex() {
        if (!finished_) {
            mbedtls_sha256_finish_ret(&ctx_, digest_);
            finished_ = true;
        }
        return sha256Hex(digest_);
    }

private:
    mbedtls_sha256_context ctx_;
    uint8_t digest_[32] = {0};
    bool finished_;
};

class HttpDownloadSession {
public:
    HttpDownloadSession() : content_length_(-1), active_(false) {
    }

    ~HttpDownloadSession() {
        close();
    }

    bool open(const String& url, String& error) {
        close();

        const bool secure = url.startsWith("https://");
        http_.setReuse(false);
        http_.setTimeout(app::kOtaHttpTimeoutMs);
        http_.setConnectTimeout(app::kOtaHttpTimeoutMs);

        bool begun = false;
        if (secure) {
            secure_client_.setInsecure();
            begun = http_.begin(secure_client_, url);
        } else {
            begun = http_.begin(plain_client_, url);
        }

        if (!begun) {
            error = "HTTP begin failed";
            return false;
        }

        const int status = http_.GET();
        if (status != HTTP_CODE_OK) {
            error = String("HTTP GET failed: ") + status;
            close();
            return false;
        }

        content_length_ = http_.getSize();
        active_ = true;
        return true;
    }

    void close() {
        if (active_) {
            http_.end();
            active_ = false;
        }
        content_length_ = -1;
    }

    int contentLength() const {
        return content_length_;
    }

    Stream& stream() {
        return *http_.getStreamPtr();
    }

private:
    HTTPClient http_;
    WiFiClient plain_client_;
    WiFiClientSecure secure_client_;
    int content_length_;
    bool active_;
};

class Stm32Bootloader {
public:
    explicit Stm32Bootloader(HardwareSerial& serial)
        : serial_(serial),
          bootloader_request_acknowledged_(false),
          protocol_version_(0),
          device_id_(0) {
        memset(supported_commands_, 0, sizeof(supported_commands_));
    }

    const String& lastError() const { return last_error_; }
    uint8_t protocolVersion() const { return protocol_version_; }
    uint16_t deviceId() const { return device_id_; }

    bool canAutoEnterBootloader() const {
        return app::kStm32Boot0Pin >= 0 && app::kStm32ResetPin >= 0;
    }

    bool startSession() {
        clearError();
        serial_.flush();
        bootloader_request_acknowledged_ = false;
        if (!canAutoEnterBootloader()) {
            requestBootloaderFromApp();
        }
        serial_.end();

        configureBootPinsForBootloader();
        serial_.begin(
            app::kStm32BootloaderBaud,
            SERIAL_8E1,
            app::kAuxSerialRxPin,
            app::kAuxSerialTxPin);
        delay(10);
        clearInput();

        if (!sync()) {
            return false;
        }
        if (!fetchCommandSet()) {
            return false;
        }
        if (!fetchDeviceId()) {
            return false;
        }
        if (!supports(kStm32WriteMemoryCommand) || !supports(kStm32ReadMemoryCommand)) {
            setError("STM32 bootloader lacks read/write commands");
            return false;
        }
        if (!supports(kStm32EraseMemoryCommand) && !supports(kStm32ExtendedEraseCommand)) {
            setError("STM32 bootloader lacks erase commands");
            return false;
        }

        return true;
    }

    void restoreAppUart(bool reset_target, bool keep_bootloader_mode = false) {
        if (canAutoEnterBootloader()) {
            configureBootPinsForApp(keep_bootloader_mode);
            if (reset_target) {
                pulseReset();
                delay(app::kStm32BootSettleMs);
            }
        }

        serial_.flush();
        serial_.end();
        serial_.begin(
            app::kAuxSerialBaud,
            SERIAL_8N1,
            app::kAuxSerialRxPin,
            app::kAuxSerialTxPin);
        delay(10);
        clearInput();
    }

    bool jumpToApp() {
        return sendGo(app::kStm32FlashBaseAddress);
    }

    bool eraseFlash() {
        if (supports(kStm32ExtendedEraseCommand)) {
            if (!sendCommand(kStm32ExtendedEraseCommand, app::kStm32CommandTimeoutMs, "extended erase")) {
                return false;
            }
            const uint8_t payload[] = {0xFF, 0xFF, 0x00};
            if (!writeExact(payload, sizeof(payload), "extended erase payload")) {
                return false;
            }
            return waitForAck(app::kStm32EraseTimeoutMs, "extended erase");
        }

        if (!sendCommand(kStm32EraseMemoryCommand, app::kStm32CommandTimeoutMs, "erase")) {
            return false;
        }
        const uint8_t payload[] = {0xFF, 0x00};
        if (!writeExact(payload, sizeof(payload), "erase payload")) {
            return false;
        }
        return waitForAck(app::kStm32EraseTimeoutMs, "erase");
    }

    bool writeChunk(uint32_t address, const uint8_t* data, size_t len) {
        if (data == nullptr || len == 0 || len > 256) {
            setError("STM32 write chunk length invalid");
            return false;
        }
        if ((len % 4U) != 0U) {
            setError("STM32 write chunk must be 4-byte aligned");
            return false;
        }

        if (!sendCommand(kStm32WriteMemoryCommand, app::kStm32CommandTimeoutMs, "write")) {
            return false;
        }
        if (!sendAddress(address, "write")) {
            return false;
        }

        uint8_t frame[258];
        frame[0] = static_cast<uint8_t>(len - 1U);
        uint8_t checksum = frame[0];
        for (size_t i = 0; i < len; ++i) {
            frame[i + 1] = data[i];
            checksum ^= data[i];
        }
        frame[len + 1] = checksum;

        if (!writeExact(frame, len + 2U, "write payload")) {
            return false;
        }
        return waitForAck(app::kStm32WriteTimeoutMs, "write payload");
    }

    bool readChunk(uint32_t address, uint8_t* data, size_t len) {
        if (data == nullptr || len == 0 || len > 256) {
            setError("STM32 read chunk length invalid");
            return false;
        }

        if (!sendCommand(kStm32ReadMemoryCommand, app::kStm32CommandTimeoutMs, "read")) {
            return false;
        }
        if (!sendAddress(address, "read")) {
            return false;
        }

        const uint8_t request[] = {
            static_cast<uint8_t>(len - 1U),
            static_cast<uint8_t>((len - 1U) ^ 0xFFU),
        };
        if (!writeExact(request, sizeof(request), "read payload")) {
            return false;
        }
        if (!waitForAck(app::kStm32CommandTimeoutMs, "read payload")) {
            return false;
        }
        return readExact(data, len, app::kStm32VerifyTimeoutMs, "read data");
    }

private:
    void clearError() {
        last_error_ = "";
    }

    void setError(const String& error) {
        last_error_ = error;
        LOGE("OTA", "%s", error.c_str());
    }

    bool supports(uint8_t command) const {
        return supported_commands_[command];
    }

    void clearInput() {
        while (serial_.available() > 0) {
            serial_.read();
        }
    }

    bool requestBootloaderFromApp() {
        serial_.flush();
        serial_.end();
        serial_.begin(
            app::kAuxSerialBaud,
            SERIAL_8N1,
            app::kAuxSerialRxPin,
            app::kAuxSerialTxPin);
        delay(10);
        clearInput();
        LOGI("OTA", "Requesting STM32 app to enter system bootloader");

        for (uint8_t attempt = 0; attempt < kStm32AppBootRequestAttempts; ++attempt) {
            bool write_ok = true;
            for (size_t i = 0; i < sizeof(kStm32AppBootRequest); ++i) {
                if (serial_.write(&kStm32AppBootRequest[i], 1) != 1) {
                    write_ok = false;
                    break;
                }
                serial_.flush();
                delay(20);
            }
            if (!write_ok) {
                clearInput();
                delay(40);
                continue;
            }

            if (waitForAppBootAck(app::kStm32CommandTimeoutMs)) {
                bootloader_request_acknowledged_ = true;
                LOGI("OTA", "STM32 app acknowledged bootloader request");
                delay(app::kStm32BootSettleMs);
                clearInput();
                return true;
            }

            clearInput();
            delay(40);
        }

        LOGW("OTA", "STM32 app bootloader request not acknowledged, trying direct sync");
        return false;
    }

    bool waitForAppBootAck(uint32_t timeout_ms) {
        const uint32_t start_ms = millis();
        while ((millis() - start_ms) < timeout_ms) {
            if (serial_.available() <= 0) {
                delay(1);
                continue;
            }

            const int value = serial_.read();
            if (value < 0) {
                continue;
            }

            if (static_cast<uint8_t>(value) == kStm32Ack) {
                return true;
            }
        }

        return false;
    }

    void configureBootPinsForBootloader() {
        if (app::kStm32Boot0Pin >= 0) {
            pinMode(app::kStm32Boot0Pin, OUTPUT);
            digitalWrite(app::kStm32Boot0Pin, app::kStm32Boot0BootloaderLevel);
        }
        if (app::kStm32Boot1Pin >= 0) {
            pinMode(app::kStm32Boot1Pin, OUTPUT);
            digitalWrite(app::kStm32Boot1Pin, app::kStm32Boot1BootloaderLevel);
        }
        if (app::kStm32ResetPin >= 0) {
            pinMode(app::kStm32ResetPin, OUTPUT);
            digitalWrite(app::kStm32ResetPin, app::kStm32ResetDeassertLevel);
            pulseReset();
            delay(app::kStm32BootSettleMs);
        } else {
            delay(app::kStm32BootSettleMs);
        }
    }

    void configureBootPinsForApp(bool keep_bootloader_mode) {
        if (app::kStm32Boot1Pin >= 0) {
            digitalWrite(
                app::kStm32Boot1Pin,
                keep_bootloader_mode ? app::kStm32Boot1BootloaderLevel : app::kStm32Boot1AppLevel);
        }
        if (app::kStm32Boot0Pin >= 0) {
            digitalWrite(
                app::kStm32Boot0Pin,
                keep_bootloader_mode ? app::kStm32Boot0BootloaderLevel : app::kStm32Boot0AppLevel);
        }
    }

    void pulseReset() {
        if (app::kStm32ResetPin < 0) {
            return;
        }
        digitalWrite(app::kStm32ResetPin, app::kStm32ResetAssertLevel);
        delay(app::kStm32ResetPulseMs);
        digitalWrite(app::kStm32ResetPin, app::kStm32ResetDeassertLevel);
    }

    bool sync() {
        for (uint8_t attempt = 0; attempt < 4; ++attempt) {
            clearInput();
            if (!writeByte(kStm32SyncByte, "sync")) {
                return false;
            }
            if (waitForAck(app::kStm32BootSyncTimeoutMs, "sync")) {
                return true;
            }
            delay(50);
        }
        setError(
            canAutoEnterBootloader()
                ? "STM32 sync failed after reset sequence"
                : bootloader_request_acknowledged_
                      ? "STM32 sync failed after app bootloader request"
                : "STM32 sync failed, put target in system bootloader mode manually");
        return false;
    }

    bool fetchCommandSet() {
        memset(supported_commands_, 0, sizeof(supported_commands_));
        if (!sendCommand(kStm32GetCommand, app::kStm32CommandTimeoutMs, "get")) {
            return false;
        }

        uint8_t count = 0;
        if (!readExact(&count, 1, app::kStm32CommandTimeoutMs, "get count")) {
            return false;
        }
        if (!readExact(&protocol_version_, 1, app::kStm32CommandTimeoutMs, "get version")) {
            return false;
        }

        for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
            uint8_t command = 0;
            if (!readExact(&command, 1, app::kStm32CommandTimeoutMs, "get command list")) {
                return false;
            }
            supported_commands_[command] = true;
        }

        if (!waitForAck(app::kStm32CommandTimeoutMs, "get completion")) {
            return false;
        }
        return true;
    }

    bool fetchDeviceId() {
        if (!sendCommand(kStm32GetIdCommand, app::kStm32CommandTimeoutMs, "get id")) {
            return false;
        }

        uint8_t count = 0;
        if (!readExact(&count, 1, app::kStm32CommandTimeoutMs, "get id count")) {
            return false;
        }
        uint8_t id_bytes[3] = {0};
        const size_t id_len = static_cast<size_t>(count) + 1U;
        if (id_len > sizeof(id_bytes)) {
            setError("STM32 returned an unexpected device ID length");
            return false;
        }
        if (!readExact(id_bytes, id_len, app::kStm32CommandTimeoutMs, "get id bytes")) {
            return false;
        }
        if (!waitForAck(app::kStm32CommandTimeoutMs, "get id completion")) {
            return false;
        }

        device_id_ = 0;
        for (size_t i = 0; i < id_len; ++i) {
            device_id_ = static_cast<uint16_t>((device_id_ << 8) | id_bytes[i]);
        }
        return true;
    }

    bool sendGo(uint32_t address) {
        if (!sendCommand(kStm32GoCommand, app::kStm32CommandTimeoutMs, "go")) {
            return false;
        }
        return sendAddress(address, "go");
    }

    bool sendAddress(uint32_t address, const char* phase) {
        uint8_t frame[5] = {
            static_cast<uint8_t>((address >> 24) & 0xFF),
            static_cast<uint8_t>((address >> 16) & 0xFF),
            static_cast<uint8_t>((address >> 8) & 0xFF),
            static_cast<uint8_t>(address & 0xFF),
            0,
        };
        frame[4] = frame[0] ^ frame[1] ^ frame[2] ^ frame[3];
        if (!writeExact(frame, sizeof(frame), phase)) {
            return false;
        }
        return waitForAck(app::kStm32CommandTimeoutMs, phase);
    }

    bool sendCommand(uint8_t command, uint32_t timeout_ms, const char* phase) {
        const uint8_t frame[2] = {command, static_cast<uint8_t>(command ^ 0xFFU)};
        if (!writeExact(frame, sizeof(frame), phase)) {
            return false;
        }
        return waitForAck(timeout_ms, phase);
    }

    bool writeByte(uint8_t value, const char* phase) {
        return writeExact(&value, 1, phase);
    }

    bool writeExact(const uint8_t* data, size_t len, const char* phase) {
        if (data == nullptr || len == 0) {
            return true;
        }
        const size_t written = serial_.write(data, len);
        serial_.flush();
        if (written != len) {
            setError(String("STM32 write failed during ") + phase);
            return false;
        }
        return true;
    }

    bool readExact(uint8_t* data, size_t len, uint32_t timeout_ms, const char* phase) {
        size_t offset = 0;
        const uint32_t start_ms = millis();
        while (offset < len && (millis() - start_ms) < timeout_ms) {
            if (serial_.available() <= 0) {
                delay(1);
                continue;
            }
            const int value = serial_.read();
            if (value < 0) {
                continue;
            }
            data[offset++] = static_cast<uint8_t>(value);
        }

        if (offset != len) {
            setError(String("STM32 timeout while reading ") + phase);
            return false;
        }
        return true;
    }

    bool waitForAck(uint32_t timeout_ms, const char* phase) {
        uint8_t response = 0;
        if (!readExact(&response, 1, timeout_ms, phase)) {
            return false;
        }
        if (response == kStm32Ack) {
            return true;
        }
        if (response == kStm32Nack) {
            setError(String("STM32 NACK during ") + phase);
            return false;
        }
        setError(String("STM32 unexpected response 0x") + String(response, HEX) + " during " + phase);
        return false;
    }

    HardwareSerial& serial_;
    bool bootloader_request_acknowledged_;
    bool supported_commands_[256];
    uint8_t protocol_version_;
    uint16_t device_id_;
    String last_error_;
};

bool ensureStagingFilesystem(String& error) {
    if (SPIFFS.begin(true)) {
        return true;
    }
    error = "SPIFFS mount failed";
    return false;
}

bool isPlausibleStm32VectorTable(File& image, String& error) {
    if (!image || image.size() < 8) {
        error = "STM32 image is too small";
        return false;
    }

    uint8_t header[8] = {0};
    const size_t read = image.read(header, sizeof(header));
    image.seek(0, SeekSet);
    if (read != sizeof(header)) {
        error = "STM32 image header read failed";
        return false;
    }

    const uint32_t initial_sp =
        static_cast<uint32_t>(header[0]) |
        (static_cast<uint32_t>(header[1]) << 8) |
        (static_cast<uint32_t>(header[2]) << 16) |
        (static_cast<uint32_t>(header[3]) << 24);
    const uint32_t reset_vector =
        static_cast<uint32_t>(header[4]) |
        (static_cast<uint32_t>(header[5]) << 8) |
        (static_cast<uint32_t>(header[6]) << 16) |
        (static_cast<uint32_t>(header[7]) << 24);

    const bool sp_ok = (initial_sp & 0x2FFE0000UL) == 0x20000000UL;
    const bool reset_ok =
        reset_vector >= app::kStm32FlashBaseAddress &&
        reset_vector < (app::kStm32FlashBaseAddress + app::kStm32FlashSizeBytes);

    if (!sp_ok || !reset_ok) {
        error = String("STM32 vector table invalid sp=0x") + String(initial_sp, HEX) +
                " reset=0x" + String(reset_vector, HEX);
        return false;
    }

    return true;
}

bool downloadToFile(
    const String& url,
    const String& expected_sha256,
    const char* target_label,
    fs::FS& fs,
    const char* path,
    size_t max_size_bytes,
    size_t& staged_size,
    String& actual_sha256,
    String& error) {
    HttpDownloadSession session;
    if (!session.open(url, error)) {
        return false;
    }

    const int content_length = session.contentLength();
    if (content_length <= 0) {
        error = "HTTP server must provide Content-Length";
        return false;
    }
    if (static_cast<size_t>(content_length) > max_size_bytes) {
        error = String(target_label) + " image is larger than target flash";
        return false;
    }

    if (fs.exists(path)) {
        fs.remove(path);
    }

    File staged = fs.open(path, FILE_WRITE);
    if (!staged) {
        error = "Failed to open OTA staging file";
        return false;
    }

    uint8_t buffer[app::kOtaHttpBufferBytes];
    Stream& stream = session.stream();
    Sha256Accumulator sha256;
    size_t total = 0;
    uint32_t last_data_ms = millis();
    uint32_t last_progress_ms = 0;

    while (total < static_cast<size_t>(content_length)) {
        const size_t available = stream.available();
        if (available == 0) {
            if ((millis() - last_data_ms) > app::kOtaHttpTimeoutMs) {
                error = "HTTP download timed out";
                staged.close();
                fs.remove(path);
                return false;
            }
            delay(1);
            continue;
        }

        const size_t to_read = min(sizeof(buffer), min(available, static_cast<size_t>(content_length) - total));
        const size_t read = stream.readBytes(buffer, to_read);
        if (read == 0) {
            if ((millis() - last_data_ms) > app::kOtaHttpTimeoutMs) {
                error = "HTTP stream stalled";
                staged.close();
                fs.remove(path);
                return false;
            }
            delay(1);
            continue;
        }

        last_data_ms = millis();
        if (staged.write(buffer, read) != read) {
            error = "Failed to write OTA staging file";
            staged.close();
            fs.remove(path);
            return false;
        }
        sha256.update(buffer, read);
        total += read;

        if ((millis() - last_progress_ms) >= app::kOtaProgressIntervalMs ||
            total == static_cast<size_t>(content_length)) {
            LOGI(
                "OTA",
                "%s download %u/%u bytes",
                target_label,
                static_cast<unsigned>(total),
                static_cast<unsigned>(content_length));
            last_progress_ms = millis();
        }
    }

    staged.close();
    actual_sha256 = sha256.finishHex();
    staged_size = total;

    if (expected_sha256.length() > 0 && actual_sha256 != expected_sha256) {
        error = String(target_label) + " SHA256 mismatch";
        fs.remove(path);
        return false;
    }

    return true;
}

bool streamEsp32Update(
    const String& url,
    const String& expected_sha256,
    String& actual_sha256,
    String& error) {
    HttpDownloadSession session;
    if (!session.open(url, error)) {
        return false;
    }

    const int content_length = session.contentLength();
    if (content_length <= 0) {
        error = "HTTP server must provide Content-Length";
        return false;
    }

    if (!Update.begin(static_cast<size_t>(content_length))) {
        error = String("ESP32 OTA begin failed: ") + Update.errorString();
        return false;
    }

    uint8_t buffer[app::kOtaHttpBufferBytes];
    Stream& stream = session.stream();
    Sha256Accumulator sha256;
    size_t total = 0;
    uint32_t last_data_ms = millis();
    uint32_t last_progress_ms = 0;

    while (total < static_cast<size_t>(content_length)) {
        const size_t available = stream.available();
        if (available == 0) {
            if ((millis() - last_data_ms) > app::kOtaHttpTimeoutMs) {
                error = "HTTP download timed out";
                Update.abort();
                return false;
            }
            delay(1);
            continue;
        }

        const size_t to_read = min(sizeof(buffer), min(available, static_cast<size_t>(content_length) - total));
        const size_t read = stream.readBytes(buffer, to_read);
        if (read == 0) {
            if ((millis() - last_data_ms) > app::kOtaHttpTimeoutMs) {
                error = "HTTP stream stalled";
                Update.abort();
                return false;
            }
            delay(1);
            continue;
        }

        last_data_ms = millis();
        if (Update.write(buffer, read) != read) {
            error = String("ESP32 OTA write failed: ") + Update.errorString();
            Update.abort();
            return false;
        }
        sha256.update(buffer, read);
        total += read;

        if ((millis() - last_progress_ms) >= app::kOtaProgressIntervalMs ||
            total == static_cast<size_t>(content_length)) {
            LOGI(
                "OTA",
                "ESP32 download %u/%u bytes",
                static_cast<unsigned>(total),
                static_cast<unsigned>(content_length));
            last_progress_ms = millis();
        }
    }

    actual_sha256 = sha256.finishHex();
    if (expected_sha256.length() > 0 && actual_sha256 != expected_sha256) {
        error = "ESP32 SHA256 mismatch";
        Update.abort();
        return false;
    }

    if (!Update.end()) {
        error = String("ESP32 OTA finalize failed: ") + Update.errorString();
        return false;
    }
    if (!Update.isFinished()) {
        error = "ESP32 OTA did not finish cleanly";
        return false;
    }

    return true;
}

bool writeStm32File(Stm32Bootloader& bootloader, File& image, size_t image_size) {
    uint8_t buffer[256];
    size_t remaining = image_size;
    uint32_t address = app::kStm32FlashBaseAddress;
    uint32_t last_progress_ms = 0;
    size_t written = 0;

    while (remaining > 0) {
        const size_t raw_len = min(sizeof(buffer), remaining);
        const size_t read = image.read(buffer, raw_len);
        if (read != raw_len) {
            LOGE("OTA", "STM32 image read failed during flash");
            return false;
        }

        const size_t padded_len = align4(raw_len);
        if (padded_len > raw_len) {
            memset(buffer + raw_len, 0xFF, padded_len - raw_len);
        }

        if (!bootloader.writeChunk(address, buffer, padded_len)) {
            return false;
        }

        address += padded_len;
        remaining -= raw_len;
        written += raw_len;

        if ((millis() - last_progress_ms) >= app::kOtaProgressIntervalMs || remaining == 0) {
            LOGI(
                "OTA",
                "STM32 flash %u/%u bytes",
                static_cast<unsigned>(written),
                static_cast<unsigned>(image_size));
            last_progress_ms = millis();
        }
    }

    return true;
}

bool verifyStm32File(Stm32Bootloader& bootloader, File& image, size_t image_size) {
    uint8_t expected[256];
    uint8_t actual[256];
    size_t remaining = image_size;
    uint32_t address = app::kStm32FlashBaseAddress;
    uint32_t last_progress_ms = 0;
    size_t verified = 0;

    while (remaining > 0) {
        const size_t raw_len = min(sizeof(expected), remaining);
        const size_t read = image.read(expected, raw_len);
        if (read != raw_len) {
            LOGE("OTA", "STM32 image read failed during verify");
            return false;
        }

        const size_t padded_len = align4(raw_len);
        if (padded_len > raw_len) {
            memset(expected + raw_len, 0xFF, padded_len - raw_len);
        }

        if (!bootloader.readChunk(address, actual, padded_len)) {
            return false;
        }
        if (memcmp(expected, actual, padded_len) != 0) {
            LOGE("OTA", "STM32 verify mismatch at 0x%08lX", static_cast<unsigned long>(address));
            return false;
        }

        address += padded_len;
        remaining -= raw_len;
        verified += raw_len;

        if ((millis() - last_progress_ms) >= app::kOtaProgressIntervalMs || remaining == 0) {
            LOGI(
                "OTA",
                "STM32 verify %u/%u bytes",
                static_cast<unsigned>(verified),
                static_cast<unsigned>(image_size));
            last_progress_ms = millis();
        }
    }

    return true;
}

}  // namespace

OtaManager::OtaManager()
    : aux_serial_(nullptr),
      selected_target_(OtaTarget::Esp32Self) {
}

void OtaManager::begin(HardwareSerial& aux_serial) {
    aux_serial_ = &aux_serial;
}

const char* OtaManager::selectedTargetName() const {
    switch (selected_target_) {
        case OtaTarget::Esp32Self:
            return "esp32";
        case OtaTarget::Stm32:
            return "stm32";
        default:
            return "unknown";
    }
}

bool OtaManager::canAutoEnterStm32Bootloader() const {
    return app::kStm32Boot0Pin >= 0 && app::kStm32ResetPin >= 0;
}

bool OtaManager::runSelectedUpdate(const String& url, const String& expected_sha256) {
    switch (selected_target_) {
        case OtaTarget::Esp32Self:
            return runEsp32Update(url, expected_sha256);
        case OtaTarget::Stm32:
            return runStm32Update(url, expected_sha256);
        default:
            setError("Unsupported OTA target");
            return false;
    }
}

bool OtaManager::runEsp32Update(const String& url, const String& expected_sha256) {
    last_error_ = "";

    if (WiFi.status() != WL_CONNECTED) {
        setError("WiFi is not connected");
        return false;
    }

    String normalized_sha;
    String validation_error;
    if (!normalizeSha256(expected_sha256, normalized_sha, validation_error)) {
        setError(validation_error);
        return false;
    }

    String actual_sha256;
    String error;
    LOGI("OTA", "ESP32 OTA start url=%s", url.c_str());
    if (!streamEsp32Update(url, normalized_sha, actual_sha256, error)) {
        setError(error);
        return false;
    }

    LOGI("OTA", "ESP32 OTA ready, sha256=%s", actual_sha256.c_str());
    delay(150);
    return true;
}

bool OtaManager::runStm32Update(const String& url, const String& expected_sha256) {
    last_error_ = "";

    if (aux_serial_ == nullptr) {
        setError("STM32 OTA requires UART1 to be initialized");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        setError("WiFi is not connected");
        return false;
    }

    String normalized_sha;
    String validation_error;
    if (!normalizeSha256(expected_sha256, normalized_sha, validation_error)) {
        setError(validation_error);
        return false;
    }

    String fs_error;
    if (!ensureStagingFilesystem(fs_error)) {
        setError(fs_error);
        return false;
    }

    size_t staged_size = 0;
    String staged_sha256;
    String download_error;
    LOGI("OTA", "STM32 OTA stage start url=%s", url.c_str());
    if (!downloadToFile(
            url,
            normalized_sha,
            "STM32",
            SPIFFS,
            kStagingPath,
            app::kStm32FlashSizeBytes,
            staged_size,
            staged_sha256,
            download_error)) {
        setError(download_error);
        return false;
    }

    File image = SPIFFS.open(kStagingPath, FILE_READ);
    if (!image) {
        setError("Failed to reopen STM32 staged image");
        return false;
    }

    String image_error;
    if (!isPlausibleStm32VectorTable(image, image_error)) {
        image.close();
        SPIFFS.remove(kStagingPath);
        setError(image_error);
        return false;
    }

    Stm32Bootloader bootloader(*aux_serial_);
    if (!bootloader.startSession()) {
        image.close();
        bootloader.restoreAppUart(bootloader.canAutoEnterBootloader(), false);
        setError(bootloader.lastError());
        return false;
    }

    LOGI(
        "OTA",
        "STM32 bootloader protocol=0x%02X device=0x%04X auto=%s sha256=%s",
        bootloader.protocolVersion(),
        bootloader.deviceId(),
        bootloader.canAutoEnterBootloader() ? "yes" : "no",
        staged_sha256.c_str());

    if (app::kStm32ExpectedDeviceId != 0 &&
        bootloader.deviceId() != app::kStm32ExpectedDeviceId) {
        LOGW(
            "OTA",
            "Unexpected STM32 device id=0x%04X expected=0x%04X",
            bootloader.deviceId(),
            app::kStm32ExpectedDeviceId);
    }

    bool success = false;
    do {
        if (!bootloader.eraseFlash()) {
            setError(bootloader.lastError());
            break;
        }

        image.seek(0, SeekSet);
        if (!writeStm32File(bootloader, image, staged_size)) {
            setError(bootloader.lastError());
            break;
        }

        image.seek(0, SeekSet);
        if (!verifyStm32File(bootloader, image, staged_size)) {
            setError(bootloader.lastError().length() == 0 ? "STM32 verify failed" : bootloader.lastError());
            break;
        }

        if (!bootloader.canAutoEnterBootloader()) {
            if (!bootloader.jumpToApp()) {
                LOGW("OTA", "STM32 jump-to-app failed, manual reset may be required");
            }
        }

        success = true;
    } while (false);

    image.close();
    bootloader.restoreAppUart(success || bootloader.canAutoEnterBootloader(), false);

    if (success) {
        SPIFFS.remove(kStagingPath);
        LOGI("OTA", "STM32 OTA complete");
        return true;
    }

    return false;
}

void OtaManager::setError(const String& error) {
    last_error_ = error;
    LOGE("OTA", "%s", error.c_str());
}
