#pragma once

#include <Arduino.h>

/**
 * @namespace doubao
 * @brief 豆包协议命名空间
 * @details 包含豆包协议的相关定义和实现，包括消息类型、事件代码、协议帧构建和解析等
 */
namespace doubao {

/**
 * @enum MessageType
 * @brief 消息类型枚举
 */
enum MessageType : uint8_t {
    kClientFullRequest = 0x01,      // 客户端完整请求
    kClientAudioOnlyRequest = 0x02,  // 客户端仅音频请求
    kServerFullResponse = 0x09,      // 服务器完整响应
    kServerAudioResponse = 0x0B,     // 服务器音频响应
    kServerErrorResponse = 0x0F      // 服务器错误响应
};

/**
 * @enum MessageFlag
 * @brief 消息标志枚举
 */
enum MessageFlag : uint8_t {
    kNoSequence = 0x00,            // 无序列
    kPositiveSequence = 0x01,      // 正序列
    kNegativeSequence = 0x02,      // 负序列
    kNegativeSequenceOne = 0x03,   // 负序列1
    kWithEvent = 0x04              // 带事件
};

/**
 * @enum Serialization
 * @brief 序列化类型枚举
 */
enum Serialization : uint8_t {
    kNoSerialization = 0x00,     // 无序列化
    kJsonSerialization = 0x01    // JSON序列化
};

/**
 * @enum Compression
 * @brief 压缩类型枚举
 */
enum Compression : uint8_t {
    kNoCompression = 0x00,     // 无压缩
    kGzipCompression = 0x01    // Gzip压缩
};

/**
 * @enum EventCode
 * @brief 事件代码枚举
 */
enum EventCode : uint32_t {
    kStartConnection = 1,       // 开始连接
    kFinishConnection = 2,      // 结束连接
    kStartSession = 100,        // 开始会话
    kFinishSession = 102,       // 结束会话
    kTaskRequest = 200,         // 任务请求
    kSayHello = 300,            // 打招呼
    kConnectionStarted = 50,     // 连接已开始
    kConnectionFailed = 51,      // 连接失败
    kConnectionFinished = 52,    // 连接已结束
    kSessionStarted = 150,       // 会话已开始
    kSessionFinished = 152,      // 会话已结束
    kSessionFailed = 153,        // 会话失败
    kUsageResponse = 154,        // 使用响应
    kTtsSentenceStart = 350,     // TTS句子开始
    kTtsSentenceEnd = 351,       // TTS句子结束
    kTtsResponse = 352,          // TTS响应
    kTtsEnded = 359,             // TTS结束
    kAsrInfo = 450,              // ASR信息
    kAsrResponse = 451,          // ASR响应
    kAsrEnded = 459,             // ASR结束
    kChatResponse = 550,          // 聊天响应
    kChatEnded = 559,             // 聊天结束
    kDialogError = 599            // 对话错误
};

/**
 * @struct SessionConfig
 * @brief 会话配置结构体
 */
struct SessionConfig {
    String bot_name;              // 机器人名称
    String system_role;           // 系统角色
    String speaking_style;        // 说话风格
    String speaker;               //  speakers
    String location_city;         // 位置城市
    String model;                 // 模型
    uint32_t recv_timeout_seconds = 10;  // 接收超时时间（秒）
    bool strict_audit = false;    // 是否严格审核
};

/**
 * @struct ServerMessage
 * @brief 服务器消息结构体
 */
struct ServerMessage {
    uint8_t message_type = 0;     // 消息类型
    uint8_t flags = 0;            // 消息标志
    uint8_t serialization = 0;    // 序列化类型
    uint8_t compression = 0;      // 压缩类型

    bool has_sequence = false;     // 是否有序列
    uint32_t sequence = 0;        // 序列值

    bool has_event = false;        // 是否有事件
    uint32_t event = 0;            // 事件代码

    String session_id;             // 会话ID
    String payload_json;           // JSON负载

    uint8_t* payload = nullptr;    // 负载数据
    size_t payload_length = 0;     // 负载长度
    bool payload_owned = false;    // 负载是否为自有

    bool is_error = false;         // 是否为错误
    uint32_t error_code = 0;       // 错误代码
};

/**
 * @class Protocol
 * @brief 豆包协议类
 * @details 负责构建和解析豆包协议消息
 */
class Protocol {
public:
    /**
     * @brief 构建开始连接帧
     * @param out 输出缓冲区
     * @param out_len 输出长度
     * @return 构建成功返回true，失败返回false
     */
    static bool buildStartConnectionFrame(uint8_t*& out, size_t& out_len);
    
    /**
     * @brief 构建开始会话帧
     * @param session_id 会话ID
     * @param config 会话配置
     * @param out 输出缓冲区
     * @param out_len 输出长度
     * @return 构建成功返回true，失败返回false
     */
    static bool buildStartSessionFrame(
        const String& session_id,
        const SessionConfig& config,
        uint8_t*& out,
        size_t& out_len);
    
    /**
     * @brief 构建音频帧
     * @param session_id 会话ID
     * @param audio 音频数据
     * @param audio_len 音频长度
     * @param out 输出缓冲区
     * @param out_len 输出长度
     * @return 构建成功返回true，失败返回false
     */
    static bool buildAudioFrame(
        const String& session_id,
        const uint8_t* audio,
        size_t audio_len,
        uint8_t*& out,
        size_t& out_len);
    
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
    static bool buildAudioFrameInto(
        const String& session_id,
        const uint8_t* audio,
        size_t audio_len,
        uint8_t* out,
        size_t out_capacity,
        size_t& out_len);
    
    /**
     * @brief 构建打招呼帧
     * @param session_id 会话ID
     * @param content 内容
     * @param out 输出缓冲区
     * @param out_len 输出长度
     * @return 构建成功返回true，失败返回false
     */
    static bool buildSayHelloFrame(
        const String& session_id,
        const String& content,
        uint8_t*& out,
        size_t& out_len);

    /**
     * @brief 解析服务器消息
     * @param data 数据
     * @param len 数据长度
     * @param message 服务器消息
     * @return 解析成功返回true，失败返回false
     */
    static bool parseServerMessage(
        const uint8_t* data,
        size_t len,
        ServerMessage& message);
    
    /**
     * @brief 释放服务器消息
     * @param message 服务器消息
     */
    static void freeServerMessage(ServerMessage& message);

private:
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
    static bool buildCommonFrame(
        uint8_t message_type,
        uint8_t flags,
        uint8_t serialization,
        uint8_t compression,
        uint32_t event,
        const String& session_id,
        const uint8_t* payload,
        size_t payload_len,
        uint8_t*& out,
        size_t& out_len);
    
    /**
     * @brief 写入大端32位整数
     * @param target 目标缓冲区
     * @param value 值
     */
    static void writeUint32BE(uint8_t* target, uint32_t value);
    
    /**
     * @brief 读取大端32位整数
     * @param source 源缓冲区
     * @return 值
     */
    static uint32_t readUint32BE(const uint8_t* source);
    
    /**
     * @brief 将字节数组转换为字符串
     * @param data 数据
     * @param len 长度
     * @return 字符串
     */
    static String bytesToString(const uint8_t* data, size_t len);
};

}  // namespace doubao
