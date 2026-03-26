#include "doubao_protocol.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "app_config.h"

namespace {

/**
 * @brief 分配PSRAM内存
 * @param size 所需内存大小
 * @return 分配的内存指针，失败返回nullptr
 * @note 优先尝试从PSRAM分配内存，失败则使用普通内存
 */
void* allocPsram(size_t size) {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == nullptr) {
        ptr = malloc(size);
    }
    return ptr;
}

constexpr uint8_t kProtocolVersion = 0x01;  // 协议版本
constexpr uint8_t kHeaderWords = 0x01;      // 头部字数

}  // namespace

namespace doubao {

/**
 * @brief 构建开始连接帧
 * @param out 输出缓冲区
 * @param out_len 输出长度
 * @return 构建成功返回true，失败返回false
 */
bool Protocol::buildStartConnectionFrame(uint8_t*& out, size_t& out_len) {
    static constexpr char kStartConnectionPayload[] = "{}";
    const size_t payload_len = sizeof(kStartConnectionPayload) - 1;

    // 计算帧大小
    out_len = 4 + 4 + 4 + payload_len;
    // 分配内存
    out = static_cast<uint8_t*>(allocPsram(out_len));
    if (out == nullptr) {
        return false;
    }

    // 构建头部
    out[0] = (kProtocolVersion << 4) | kHeaderWords;
    out[1] = (kClientFullRequest << 4) | kWithEvent;
    out[2] = (kJsonSerialization << 4) | kNoCompression;
    out[3] = 0x00;

    // 写入事件代码和负载长度
    writeUint32BE(out + 4, kStartConnection);
    writeUint32BE(out + 8, payload_len);
    // 写入负载
    memcpy(out + 12, kStartConnectionPayload, payload_len);
    return true;
}

/**
 * @brief 构建开始会话帧
 * @param session_id 会话ID
 * @param config 会话配置
 * @param out 输出缓冲区
 * @param out_len 输出长度
 * @return 构建成功返回true，失败返回false
 */
bool Protocol::buildStartSessionFrame(
    const String& session_id,
    const SessionConfig& config,
    uint8_t*& out,
    size_t& out_len) {
    StaticJsonDocument<1536> doc;

    // 构建ASR配置
    JsonObject asr = doc.createNestedObject("asr");
    JsonObject asr_audio = asr.createNestedObject("audio_config");
    asr_audio["channel"] = 1;
    asr_audio["format"] = "pcm_s16le";
    asr_audio["sample_rate"] = app::kInputSampleRate;
    JsonObject asr_extra = asr.createNestedObject("extra");
    asr_extra["end_smooth_window_ms"] = app::kVadSilenceFrames * app::kAudioFrameMs;
    asr_extra["max_auto_stop_ms"] = 3500;

    // 构建TTS配置
    JsonObject tts = doc.createNestedObject("tts");
    tts["speaker"] = config.speaker;
    JsonObject tts_audio = tts.createNestedObject("audio_config");
    tts_audio["channel"] = 1;
    tts_audio["format"] = "pcm_s16le";
    tts_audio["sample_rate"] = app::kOutputSampleRate;

    // 构建对话配置
    JsonObject dialog = doc.createNestedObject("dialog");
    dialog["bot_name"] = config.bot_name;
    dialog["system_role"] = config.system_role;
    dialog["speaking_style"] = config.speaking_style;

    // 构建位置配置
    JsonObject location = dialog.createNestedObject("location");
    location["city"] = config.location_city;

    // 构建对话额外配置
    JsonObject dialog_extra = dialog.createNestedObject("extra");
    dialog_extra["strict_audit"] = config.strict_audit;
    dialog_extra["recv_timeout"] = config.recv_timeout_seconds;
    if (config.model.length() > 0) {
        dialog_extra["model"] = config.model;
    }

    // 序列化JSON
    String payload;
    serializeJson(doc, payload);

    // 构建通用帧
    return buildCommonFrame(
        kClientFullRequest,
        kWithEvent,
        kJsonSerialization,
        kNoCompression,
        kStartSession,
        session_id,
        reinterpret_cast<const uint8_t*>(payload.c_str()),
        payload.length(),
        out,
        out_len);
}

/**
 * @brief 构建音频帧
 * @param session_id 会话ID
 * @param audio 音频数据
 * @param audio_len 音频长度
 * @param out 输出缓冲区
 * @param out_len 输出长度
 * @return 构建成功返回true，失败返回false
 */
bool Protocol::buildAudioFrame(
    const String& session_id,
    const uint8_t* audio,
    size_t audio_len,
    uint8_t*& out,
    size_t& out_len) {
    const size_t session_len = session_id.length();
    // 计算帧大小
    out_len = 4 + 4 + 4 + session_len + 4 + audio_len;
    // 分配内存
    out = static_cast<uint8_t*>(allocPsram(out_len));
    if (out == nullptr) {
        return false;
    }

    // 使用buildAudioFrameInto构建帧
    size_t written = 0;
    if (!buildAudioFrameInto(session_id, audio, audio_len, out, out_len, written)) {
        free(out);
        out = nullptr;
        out_len = 0;
        return false;
    }

    out_len = written;
    return true;
}

/**
 * @brief 构建音频帧到指定缓冲区
 * @param session_id 会话ID
 * @param audio 音频数据
 * @param audio_len 音频长度
 * @param out 输出缓冲区
 * @param out_capacity 输出缓冲区容量
 * @param out_len 输出长度
 * @return 构建成功返回true，失败返回false
 */
bool Protocol::buildAudioFrameInto(
    const String& session_id,
    const uint8_t* audio,
    size_t audio_len,
    uint8_t* out,
    size_t out_capacity,
    size_t& out_len) {
    const size_t session_len = session_id.length();
    // 计算帧大小
    out_len = 4 + 4 + 4 + session_len + 4 + audio_len;
    // 检查缓冲区容量
    if (out == nullptr || out_capacity < out_len) {
        return false;
    }

    // 构建头部
    out[0] = (kProtocolVersion << 4) | kHeaderWords;
    out[1] = (kClientAudioOnlyRequest << 4) | kWithEvent;
    out[2] = (kNoSerialization << 4) | kNoCompression;
    out[3] = 0x00;

    // 写入事件代码
    size_t offset = 4;
    writeUint32BE(out + offset, kTaskRequest);
    offset += 4;

    // 写入会话ID长度和会话ID
    writeUint32BE(out + offset, session_len);
    offset += 4;
    if (session_len > 0) {
        memcpy(out + offset, session_id.c_str(), session_len);
        offset += session_len;
    }

    // 写入音频长度和音频数据
    writeUint32BE(out + offset, audio_len);
    offset += 4;
    if (audio_len > 0 && audio != nullptr) {
        memcpy(out + offset, audio, audio_len);
    }

    return true;
}

/**
 * @brief 构建打招呼帧
 * @param session_id 会话ID
 * @param content 打招呼内容
 * @param out 输出缓冲区
 * @param out_len 输出长度
 * @return 构建成功返回true，失败返回false
 */
bool Protocol::buildSayHelloFrame(
    const String& session_id,
    const String& content,
    uint8_t*& out,
    size_t& out_len) {
    StaticJsonDocument<256> doc;
    doc["content"] = content;

    // 序列化JSON
    String payload;
    serializeJson(doc, payload);

    // 构建通用帧
    return buildCommonFrame(
        kClientFullRequest,
        kWithEvent,
        kJsonSerialization,
        kNoCompression,
        kSayHello,
        session_id,
        reinterpret_cast<const uint8_t*>(payload.c_str()),
        payload.length(),
        out,
        out_len);
}

/**
 * @brief 构建通用帧
 * @param message_type 消息类型
 * @param flags 消息标志
 * @param serialization 序列化类型
 * @param compression 压缩类型
 * @param event 事件代码
 * @param session_id 会话ID
 * @param payload 负载数据
 * @param payload_len 负载长度
 * @param out 输出缓冲区
 * @param out_len 输出长度
 * @return 构建成功返回true，失败返回false
 */
bool Protocol::buildCommonFrame(
    uint8_t message_type,
    uint8_t flags,
    uint8_t serialization,
    uint8_t compression,
    uint32_t event,
    const String& session_id,
    const uint8_t* payload,
    size_t payload_len,
    uint8_t*& out,
    size_t& out_len) {
    const size_t session_len = session_id.length();
    // 计算帧大小
    out_len = 4 + 4 + 4 + session_len + 4 + payload_len;
    // 分配内存
    out = static_cast<uint8_t*>(allocPsram(out_len));
    if (out == nullptr) {
        return false;
    }

    // 构建头部
    out[0] = (kProtocolVersion << 4) | kHeaderWords;
    out[1] = (message_type << 4) | flags;
    out[2] = (serialization << 4) | compression;
    out[3] = 0x00;

    // 写入事件代码
    size_t offset = 4;
    writeUint32BE(out + offset, event);
    offset += 4;

    // 写入会话ID长度和会话ID
    writeUint32BE(out + offset, session_len);
    offset += 4;
    if (session_len > 0) {
        memcpy(out + offset, session_id.c_str(), session_len);
        offset += session_len;
    }

    // 写入负载长度和负载数据
    writeUint32BE(out + offset, payload_len);
    offset += 4;
    if (payload_len > 0 && payload != nullptr) {
        memcpy(out + offset, payload, payload_len);
    }
    return true;
}

/**
 * @brief 解析服务器消息
 * @param data 消息数据
 * @param len 消息长度
 * @param message 服务器消息结构
 * @return 解析成功返回true，失败返回false
 */
bool Protocol::parseServerMessage(const uint8_t* data, size_t len, ServerMessage& message) {
    if (data == nullptr || len < 4) {
        return false;
    }

    // 解析头部信息
    const uint8_t header_words = data[0] & 0x0F;
    const size_t header_size = header_words * 4;
    if (header_size < 4 || header_size > len) {
        return false;
    }

    // 初始化消息结构
    message.message_type = data[1] >> 4;
    message.flags = data[1] & 0x0F;
    message.serialization = data[2] >> 4;
    message.compression = data[2] & 0x0F;
    message.has_event = false;
    message.has_sequence = false;
    message.is_error = false;
    message.payload = nullptr;
    message.payload_length = 0;
    message.payload_owned = false;
    message.payload_json = "";
    message.session_id = "";

    size_t offset = header_size;

    // 处理完整响应或音频响应
    if (message.message_type == kServerFullResponse ||
        message.message_type == kServerAudioResponse) {
        // 解析序列
        if ((message.flags & 0x03) != kNoSequence) {
            if (offset + 4 > len) {
                return false;
            }
            message.sequence = readUint32BE(data + offset);
            message.has_sequence = true;
            offset += 4;
        }

        // 解析事件
        if (message.flags & kWithEvent) {
            if (offset + 4 > len) {
                return false;
            }
            message.event = readUint32BE(data + offset);
            message.has_event = true;
            offset += 4;
        }

        // 解析会话ID
        if (offset + 4 > len) {
            return false;
        }
        const uint32_t session_len = readUint32BE(data + offset);
        offset += 4;
        if (offset + session_len > len) {
            return false;
        }
        message.session_id = bytesToString(data + offset, session_len);
        offset += session_len;

        // 解析负载
        if (offset + 4 > len) {
            return true;
        }
        message.payload_length = readUint32BE(data + offset);
        offset += 4;
        if (message.payload_length == 0) {
            return true;
        }
        if (offset + message.payload_length > len) {
            return false;
        }

        // 处理不同类型的负载
        if (message.serialization == kJsonSerialization) {
            message.payload_json = bytesToString(data + offset, message.payload_length);
        } else {
            message.payload = const_cast<uint8_t*>(data + offset);
        }
        return true;
    }

    // 处理错误响应
    if (message.message_type == kServerErrorResponse) {
        message.is_error = true;
        if (offset + 4 > len) {
            return false;
        }
        message.error_code = readUint32BE(data + offset);
        offset += 4;

        // 解析错误负载
        if (offset + 4 <= len) {
            const uint32_t payload_len = readUint32BE(data + offset);
            offset += 4;
            if (payload_len > 0 && offset + payload_len <= len) {
                message.payload_json = bytesToString(data + offset, payload_len);
            }
        }
        return true;
    }

    return false;
}

/**
 * @brief 释放服务器消息资源
 * @param message 服务器消息结构
 * @note 释放消息中动态分配的内存资源
 */
void Protocol::freeServerMessage(ServerMessage& message) {
    if (message.payload_owned && message.payload != nullptr) {
        free(message.payload);
    }
    message.payload = nullptr;
    message.payload_length = 0;
    message.payload_owned = false;
    message.payload_json = "";
    message.session_id = "";
}

/**
 * @brief 将32位无符号整数写入大端字节序
 * @param target 目标缓冲区
 * @param value 要写入的值
 */
void Protocol::writeUint32BE(uint8_t* target, uint32_t value) {
    target[0] = (value >> 24) & 0xFF;
    target[1] = (value >> 16) & 0xFF;
    target[2] = (value >> 8) & 0xFF;
    target[3] = value & 0xFF;
}

/**
 * @brief 从大端字节序读取32位无符号整数
 * @param source 源缓冲区
 * @return 读取的32位无符号整数值
 */
uint32_t Protocol::readUint32BE(const uint8_t* source) {
    return (static_cast<uint32_t>(source[0]) << 24) |
           (static_cast<uint32_t>(source[1]) << 16) |
           (static_cast<uint32_t>(source[2]) << 8) |
           static_cast<uint32_t>(source[3]);
}

/**
 * @brief 将字节数组转换为字符串
 * @param data 字节数组指针
 * @param len 字节数组长度
 * @return 转换后的字符串
 */
String Protocol::bytesToString(const uint8_t* data, size_t len) {
    String result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        result += static_cast<char>(data[i]);
    }
    return result;
}

}  // namespace doubao
