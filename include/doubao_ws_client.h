#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <functional>

/**
 * @class DoubaoWsClient
 * @brief 豆包WebSocket客户端类
 * @details 负责与豆包服务器建立WebSocket连接并处理通信，包括握手、消息发送和接收等功能
 */
class DoubaoWsClient {
public:
    /**
     * @typedef MessageCallback
     * @brief 消息回调函数类型
     * @param data 消息数据
     * @param len 消息长度
     */
    using MessageCallback = std::function<void(const uint8_t*, size_t)>;
    
    /**
     * @typedef DisconnectCallback
     * @brief 断开连接回调函数类型
     */
    using DisconnectCallback = std::function<void()>;

    /**
     * @brief 构造函数
     */
    DoubaoWsClient();
    
    /**
     * @brief 析构函数
     */
    ~DoubaoWsClient();

    /**
     * @brief 初始化客户端
     * @param app_id 应用ID
     * @param access_key 访问密钥
     * @return 初始化成功返回true，失败返回false
     */
    bool begin(const char* app_id, const char* access_key);
    
    /**
     * @brief 连接到服务器
     * @return 连接成功返回true，失败返回false
     */
    bool connect();
    
    /**
     * @brief 断开连接
     */
    void disconnect();
    
    /**
     * @brief 主循环，处理WebSocket通信
     */
    void loop();

    /**
     * @brief 发送二进制数据
     * @param data 数据指针
     * @param len 数据长度
     * @return 发送成功返回true，失败返回false
     */
    bool sendBinary(const uint8_t* data, size_t len);
    
    /**
     * @brief 检查是否已连接
     * @return 已连接返回true，否则返回false
     */
    bool isConnected();

    /**
     * @brief 设置消息回调函数
     * @param callback 回调函数
     */
    void setMessageCallback(MessageCallback callback) { message_callback_ = callback; }
    
    /**
     * @brief 设置断开连接回调函数
     * @param callback 回调函数
     */
    void setDisconnectCallback(DisconnectCallback callback) { disconnect_callback_ = callback; }

    /**
     * @brief 获取连接ID
     * @return 连接ID
     */
    const String& connectId() const { return connect_id_; }
    
    /**
     * @brief 获取日志ID
     * @return 日志ID
     */
    const String& logId() const { return log_id_; }

private:
    /**
     * @brief 执行WebSocket握手
     * @return 握手成功返回true，失败返回false
     */
    bool performHandshake();
    
    /**
     * @brief 发送WebSocket帧
     * @param opcode 操作码
     * @param data 数据
     * @param len 数据长度
     * @return 发送成功返回true，失败返回false
     */
    bool sendFrame(uint8_t opcode, const uint8_t* data, size_t len);
    
    /**
     * @brief 处理传入的WebSocket帧
     * @return 处理成功返回true，失败返回false
     */
    bool processIncomingFrame();
    
    /**
     * @brief 确保负载缓冲区容量
     * @param size 所需容量
     * @return 成功返回true，失败返回false
     */
    bool ensurePayloadCapacity(size_t size);
    
    /**
     * @brief 重置解析器
     */
    void resetParser();
    
    /**
     * @brief 通知断开连接
     */
    void notifyDisconnected();

    /**
     * @brief 生成UUID
     * @return UUID字符串
     */
    String generateUuid() const;
    
    /**
     * @brief 生成WebSocket密钥
     * @return WebSocket密钥
     */
    String generateWebSocketKey() const;

    WiFiClientSecure client_;  // WiFi安全客户端
    String app_id_;  // 应用ID
    String access_key_;  // 访问密钥
    String connect_id_;  // 连接ID
    String log_id_;  // 日志ID

    MessageCallback message_callback_;  // 消息回调函数
    DisconnectCallback disconnect_callback_;  // 断开连接回调函数

    bool connected_;  // 是否已连接
    uint32_t last_io_ms_;  // 最后一次IO时间

    bool frame_in_progress_;  // 是否正在处理帧
    bool frame_masked_;  // 帧是否被掩码
    uint8_t frame_opcode_;  // 帧操作码
    uint64_t frame_payload_size_;  // 帧负载大小
    uint64_t frame_remaining_;  // 帧剩余数据
    uint8_t header_buffer_[14];  // 头部缓冲区
    size_t header_bytes_read_;  // 已读取的头部字节数
    size_t header_bytes_needed_;  // 需要的头部字节数
    uint8_t mask_key_[4];  // 掩码键

    uint8_t* payload_buffer_;  // 负载缓冲区
    size_t payload_capacity_;  // 负载缓冲区容量
    size_t payload_bytes_read_;  // 已读取的负载字节数
};
