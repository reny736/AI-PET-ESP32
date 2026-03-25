#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <functional>

class DoubaoWsClient {
public:
    using MessageCallback = std::function<void(const uint8_t*, size_t)>;
    using DisconnectCallback = std::function<void()>;

    DoubaoWsClient();
    ~DoubaoWsClient();

    bool begin(const char* app_id, const char* access_key);
    bool connect();
    void disconnect();
    void loop();

    bool sendBinary(const uint8_t* data, size_t len);
    bool isConnected();

    void setMessageCallback(MessageCallback callback) { message_callback_ = callback; }
    void setDisconnectCallback(DisconnectCallback callback) { disconnect_callback_ = callback; }

    const String& connectId() const { return connect_id_; }
    const String& logId() const { return log_id_; }

private:
    bool performHandshake();
    bool sendFrame(uint8_t opcode, const uint8_t* data, size_t len);
    bool processIncomingFrame();
    bool ensurePayloadCapacity(size_t size);
    void resetParser();
    void notifyDisconnected();

    String generateUuid() const;
    String generateWebSocketKey() const;

    WiFiClientSecure client_;
    String app_id_;
    String access_key_;
    String connect_id_;
    String log_id_;

    MessageCallback message_callback_;
    DisconnectCallback disconnect_callback_;

    bool connected_;
    uint32_t last_io_ms_;

    bool frame_in_progress_;
    bool frame_masked_;
    uint8_t frame_opcode_;
    uint64_t frame_payload_size_;
    uint64_t frame_remaining_;
    uint8_t header_buffer_[14];
    size_t header_bytes_read_;
    size_t header_bytes_needed_;
    uint8_t mask_key_[4];

    uint8_t* payload_buffer_;
    size_t payload_capacity_;
    size_t payload_bytes_read_;
};
