/*
 * doubao_ws_client.cpp
 * WebSocket客户端实现，用于与Doubao API进行通信
 * 处理WebSocket连接、消息发送和接收，包括握手、帧解析等功能
 */

#include "doubao_ws_client.h"

#include <esp_heap_caps.h>
#include <esp_random.h>
#include <mbedtls/base64.h>

#include "app_config.h"
#include "logger.h"

namespace {

/**
 * @brief 分配PSRAM内存，如果PSRAM不可用则使用常规内存
 * @param size 需要分配的内存大小
 * @return 分配的内存指针，失败返回nullptr
 */
void* allocPsram(size_t size) {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == nullptr) {
        ptr = malloc(size);
    }
    return ptr;
}

/**
 * @brief 将64位无符号整数转换为大端字节序
 * @param target 目标缓冲区
 * @param value 要转换的64位值
 */
void writeUint64BE(uint8_t* target, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        target[i] = static_cast<uint8_t>((value >> ((7 - i) * 8)) & 0xFF);
    }
}

}  // namespace

/**
 * @brief 构造函数，初始化WebSocket客户端
 */
DoubaoWsClient::DoubaoWsClient()
    : connected_(false),
      last_io_ms_(0),
      frame_in_progress_(false),
      frame_masked_(false),
      frame_opcode_(0),
      frame_payload_size_(0),
      frame_remaining_(0),
      header_bytes_read_(0),
      header_bytes_needed_(2),
      payload_buffer_(nullptr),
      payload_capacity_(0),
      payload_bytes_read_(0) {
    memset(header_buffer_, 0, sizeof(header_buffer_));
    memset(mask_key_, 0, sizeof(mask_key_));
}

/**
 * @brief 析构函数，清理资源
 */
DoubaoWsClient::~DoubaoWsClient() {
    disconnect();
    if (payload_buffer_ != nullptr) {
        free(payload_buffer_);
        payload_buffer_ = nullptr;
    }
}

/**
 * @brief 初始化WebSocket客户端
 * @param app_id 应用ID
 * @param access_key 访问密钥
 * @return 初始化是否成功
 */
bool DoubaoWsClient::begin(const char* app_id, const char* access_key) {
    app_id_ = app_id;
    access_key_ = access_key;
    connect_id_ = generateUuid();
    return true;
}

/**
 * @brief 连接到WebSocket服务器
 * @return 连接是否成功
 */
bool DoubaoWsClient::connect() {
    disconnect();
    resetParser();

    if (WiFi.status() != WL_CONNECTED) {
        LOGE("WSS", "WiFi is not connected, skip TLS connect");
        return false;
    }

    client_.setInsecure();
    client_.setHandshakeTimeout((app::kWsHandshakeTimeoutMs + 999UL) / 1000UL);

    LOGI("WSS", "Connecting to %s:%u", app::kWsHost, app::kWsPort);
    if (!performHandshake()) {
        client_.stop();
        connected_ = false;
        return false;
    }

    connected_ = true;
    last_io_ms_ = millis();
    return true;
}

/**
 * @brief 断开WebSocket连接
 */
void DoubaoWsClient::disconnect() {
    connected_ = false;
    client_.stop();
    resetParser();
}

/**
 * @brief 主循环，处理WebSocket消息
 */
void DoubaoWsClient::loop() {
    if (!connected_) {
        return;
    }

    if (!client_.connected()) {
        notifyDisconnected();
        return;
    }

    uint8_t processed = 0;
    while (processed < app::kMaxFramesPerLoop && client_.available() > 0) {
        if (!processIncomingFrame()) {
            break;
        }
        ++processed;
    }

    if (connected_ && (millis() - last_io_ms_) >= app::kWsKeepAliveMs) {
        sendFrame(0x09, nullptr, 0);
    }
}

/**
 * @brief 发送二进制数据
 * @param data 数据指针
 * @param len 数据长度
 * @return 发送是否成功
 */
bool DoubaoWsClient::sendBinary(const uint8_t* data, size_t len) {
    return sendFrame(0x02, data, len);
}

/**
 * @brief 检查WebSocket连接状态
 * @return 是否连接
 */
bool DoubaoWsClient::isConnected() {
    return connected_ && client_.connected();
}

/**
 * @brief 执行WebSocket握手
 * @return 握手是否成功
 */
bool DoubaoWsClient::performHandshake() {
    if (!client_.connect(app::kWsHost, app::kWsPort)) {
        LOGE("WSS", "TCP connect failed");
        return false;
    }

    const String ws_key = generateWebSocketKey();

    String request;
    request.reserve(512);
    request += "GET ";
    request += app::kWsPath;
    request += " HTTP/1.1\r\n";
    request += "Host: ";
    request += app::kWsHost;
    request += "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: ";
    request += ws_key;
    request += "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    request += "X-Api-App-Id: ";
    request += app_id_;
    request += "\r\n";
    request += "X-Api-Access-Key: ";
    request += access_key_;
    request += "\r\n";
    request += "X-Api-Resource-Id: ";
    request += app::kWsResourceId;
    request += "\r\n";
    request += "X-Api-App-Key: ";
    request += app::kWsAppKey;
    request += "\r\n";
    request += "X-Api-Connect-Id: ";
    request += connect_id_;
    request += "\r\n\r\n";

    client_.print(request);

    String response;
    response.reserve(1024);
    const uint32_t start_ms = millis();
    while (client_.connected() && (millis() - start_ms) < app::kWsHandshakeTimeoutMs) {
        while (client_.available() > 0) {
            response += static_cast<char>(client_.read());
            if (response.endsWith("\r\n\r\n")) {
                break;
            }
        }
        if (response.endsWith("\r\n\r\n")) {
            break;
        }
        delay(1);
    }

    if (response.indexOf("101 Switching Protocols") == -1) {
        String preview = response;
        if (preview.length() > 180) {
            preview = preview.substring(0, 180);
            preview += "...";
        }
        LOGE("WSS", "Handshake failed, response=%s", preview.c_str());
        return false;
    }

    const int logid_index = response.indexOf("X-Tt-Logid:");
    if (logid_index >= 0) {
        const int value_start = logid_index + 11;
        const int value_end = response.indexOf("\r\n", value_start);
        if (value_end > value_start) {
            log_id_ = response.substring(value_start, value_end);
            log_id_.trim();
        }
    }

    LOGI("WSS", "Handshake ok, logid=%s", log_id_.c_str());
    return true;
}

/**
 * @brief 发送WebSocket帧
 * @param opcode 操作码
 * @param data 数据指针
 * @param len 数据长度
 * @return 发送是否成功
 */
bool DoubaoWsClient::sendFrame(uint8_t opcode, const uint8_t* data, size_t len) {
    if (!isConnected()) {
        return false;
    }

    uint8_t header[14] = {0};
    size_t header_len = 2;
    header[0] = 0x80 | (opcode & 0x0F);

    if (len < 126) {
        header[1] = 0x80 | static_cast<uint8_t>(len);
    } else if (len <= 0xFFFF) {
        header[1] = 0x80 | 126;
        header[2] = static_cast<uint8_t>((len >> 8) & 0xFF);
        header[3] = static_cast<uint8_t>(len & 0xFF);
        header_len = 4;
    } else {
        header[1] = 0x80 | 127;
        writeUint64BE(header + 2, len);
        header_len = 10;
    }

    uint8_t mask_key[4];
    for (size_t i = 0; i < sizeof(mask_key); ++i) {
        mask_key[i] = static_cast<uint8_t>(esp_random() & 0xFF);
    }

    if (client_.write(header, header_len) != header_len) {
        notifyDisconnected();
        return false;
    }
    if (client_.write(mask_key, sizeof(mask_key)) != sizeof(mask_key)) {
        notifyDisconnected();
        return false;
    }

    if (len > 0 && data != nullptr) {
        uint8_t chunk[app::kWsSendChunkBytes];
        size_t sent = 0;
        while (sent < len) {
            const size_t chunk_len =
                (len - sent) > sizeof(chunk) ? sizeof(chunk) : (len - sent);
            for (size_t i = 0; i < chunk_len; ++i) {
                chunk[i] = data[sent + i] ^ mask_key[(sent + i) % sizeof(mask_key)];
            }

            size_t chunk_sent = 0;
            while (chunk_sent < chunk_len) {
                const size_t wrote = client_.write(chunk + chunk_sent, chunk_len - chunk_sent);
                if (wrote == 0) {
                    notifyDisconnected();
                    return false;
                }
                chunk_sent += wrote;
            }
            sent += chunk_len;
        }
    }

    last_io_ms_ = millis();
    return true;
}

/**
 * @brief 处理 incoming WebSocket帧
 * @return 处理是否成功
 */
bool DoubaoWsClient::processIncomingFrame() {
    if (!frame_in_progress_) {
        while (header_bytes_read_ < 2 && client_.available() > 0) {
            header_buffer_[header_bytes_read_++] = static_cast<uint8_t>(client_.read());
        }
        if (header_bytes_read_ < 2) {
            return false;
        }

        if (header_bytes_read_ == 2) {
            const uint8_t payload_hint = header_buffer_[1] & 0x7F;
            size_t extended = 0;
            if (payload_hint == 126) {
                extended = 2;
            } else if (payload_hint == 127) {
                extended = 8;
            }
            frame_masked_ = (header_buffer_[1] & 0x80) != 0;
            header_bytes_needed_ = 2 + extended + (frame_masked_ ? 4 : 0);
        }

        while (header_bytes_read_ < header_bytes_needed_ && client_.available() > 0) {
            header_buffer_[header_bytes_read_++] = static_cast<uint8_t>(client_.read());
        }
        if (header_bytes_read_ < header_bytes_needed_) {
            return false;
        }

        frame_opcode_ = header_buffer_[0] & 0x0F;
        uint8_t payload_hint = header_buffer_[1] & 0x7F;
        size_t offset = 2;

        if (payload_hint == 126) {
            frame_payload_size_ =
                (static_cast<uint64_t>(header_buffer_[offset]) << 8) |
                static_cast<uint64_t>(header_buffer_[offset + 1]);
            offset += 2;
        } else if (payload_hint == 127) {
            frame_payload_size_ = 0;
            for (int i = 0; i < 8; ++i) {
                frame_payload_size_ =
                    (frame_payload_size_ << 8) | static_cast<uint64_t>(header_buffer_[offset + i]);
            }
            offset += 8;
        } else {
            frame_payload_size_ = payload_hint;
        }

        if (frame_masked_) {
            memcpy(mask_key_, header_buffer_ + offset, sizeof(mask_key_));
        }

        if (!ensurePayloadCapacity(static_cast<size_t>(frame_payload_size_))) {
            notifyDisconnected();
            return false;
        }

        frame_remaining_ = frame_payload_size_;
        payload_bytes_read_ = 0;
        frame_in_progress_ = true;
    }

    const size_t available = client_.available();
    if (available == 0 && frame_remaining_ > 0) {
        return false;
    }

    const size_t to_read = static_cast<size_t>(
        frame_remaining_ < available ? frame_remaining_ : available);
    if (to_read > 0) {
        const size_t read = client_.readBytes(payload_buffer_ + payload_bytes_read_, to_read);
        payload_bytes_read_ += read;
        frame_remaining_ -= read;
    }

    if (frame_remaining_ != 0) {
        return false;
    }

    if (frame_masked_) {
        for (size_t i = 0; i < payload_bytes_read_; ++i) {
            payload_buffer_[i] ^= mask_key_[i % sizeof(mask_key_)];
        }
    }

    last_io_ms_ = millis();

    switch (frame_opcode_) {
        case 0x01:  // 文本帧
        case 0x02:  // 二进制帧
            if (message_callback_) {
                message_callback_(payload_buffer_, payload_bytes_read_);
            }
            break;
        case 0x08:  // 关闭帧
            notifyDisconnected();
            return true;
        case 0x09:  // Ping帧
            sendFrame(0x0A, payload_buffer_, payload_bytes_read_);  // 发送Pong响应
            break;
        case 0x0A:  // Pong帧
        default:
            break;
    }

    resetParser();
    return true;
}

/**
 * @brief 确保payload缓冲区有足够的容量
 * @param size 需要的容量
 * @return 是否成功分配内存
 */
bool DoubaoWsClient::ensurePayloadCapacity(size_t size) {
    if (size <= payload_capacity_) {
        return true;
    }

    uint8_t* new_buffer = static_cast<uint8_t*>(allocPsram(size + app::kWsReceiveFrameSlack));
    if (new_buffer == nullptr) {
        return false;
    }

    if (payload_buffer_ != nullptr) {
        if (payload_bytes_read_ > 0) {
            memcpy(new_buffer, payload_buffer_, payload_bytes_read_);
        }
        free(payload_buffer_);
    }

    payload_buffer_ = new_buffer;
    payload_capacity_ = size + app::kWsReceiveFrameSlack;
    return true;
}

/**
 * @brief 重置WebSocket帧解析器
 */
void DoubaoWsClient::resetParser() {
    frame_in_progress_ = false;
    frame_masked_ = false;
    frame_opcode_ = 0;
    frame_payload_size_ = 0;
    frame_remaining_ = 0;
    header_bytes_read_ = 0;
    header_bytes_needed_ = 2;
    payload_bytes_read_ = 0;
    memset(header_buffer_, 0, sizeof(header_buffer_));
    memset(mask_key_, 0, sizeof(mask_key_));
}

/**
 * @brief 通知断开连接
 */
void DoubaoWsClient::notifyDisconnected() {
    const bool was_connected = connected_;
    connected_ = false;
    client_.stop();
    resetParser();
    if (was_connected && disconnect_callback_) {
        disconnect_callback_();
    }
}

/**
 * @brief 生成UUID
 * @return 生成的UUID字符串
 */
String DoubaoWsClient::generateUuid() const {
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

/**
 * @brief 生成WebSocket密钥
 * @return 生成的WebSocket密钥字符串
 */
String DoubaoWsClient::generateWebSocketKey() const {
    uint8_t raw[16];
    for (size_t i = 0; i < sizeof(raw); ++i) {
        raw[i] = static_cast<uint8_t>(esp_random() & 0xFF);
    }

    unsigned char encoded[32] = {0};
    size_t out_len = 0;
    mbedtls_base64_encode(encoded, sizeof(encoded) - 1, &out_len, raw, sizeof(raw));
    encoded[out_len] = '\0';
    return String(reinterpret_cast<char*>(encoded));
}
