#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * @class RingBuffer
 * @brief 环形缓冲区实现
 * @details 提供线程安全的环形缓冲区，支持跨边界读写操作，用于音频数据的临时存储
 * @note 线程安全，支持跨边界读写操作
 */
class RingBuffer {
public:
    /**
     * @brief 构造函数
     * @param requested_size 请求的缓冲区大小
     */
    explicit RingBuffer(size_t requested_size);
    
    /**
     * @brief 析构函数
     */
    ~RingBuffer();

    /**
     * @brief 初始化缓冲区
     * @return 初始化成功返回true，失败返回false
     */
    bool begin();
    
    /**
     * @brief 释放缓冲区资源
     */
    void end();

    /**
     * @brief 写入数据到缓冲区
     * @param data 数据指针
     * @param len 数据长度
     * @return 实际写入的长度
     */
    size_t write(const uint8_t* data, size_t len);
    
    /**
     * @brief 从缓冲区读取数据
     * @param data 数据缓冲区指针
     * @param len 读取长度
     * @return 实际读取的长度
     */
    size_t read(uint8_t* data, size_t len);

    /**
     * @brief 获取已使用空间大小
     * @return 已使用空间大小
     */
    size_t size();
    
    /**
     * @brief 获取可用空间大小
     * @return 可用空间大小
     */
    size_t freeSpace();
    
    /**
     * @brief 获取缓冲区容量
     * @return 缓冲区容量
     */
    size_t capacity() const { return requested_size_; }
    
    /**
     * @brief 获取填充百分比
     * @return 填充百分比（0.0-100.0）
     */
    float fillPercent();

    /**
     * @brief 清空缓冲区
     */
    void clear();

private:
    /**
     * @brief 获取已使用空间大小（非线程安全）
     * @return 已使用空间大小
     */
    size_t sizeUnsafe() const;
    
    /**
     * @brief 获取可用空间大小（非线程安全）
     * @return 可用空间大小
     */
    size_t freeSpaceUnsafe() const;

    uint8_t* buffer_;         // 缓冲区指针
    const size_t requested_size_;  // 请求的缓冲区大小
    const size_t storage_size_;    // 实际存储大小（requested_size_ + 1）
    size_t head_;             // 写入位置
    size_t tail_;             // 读取位置
    SemaphoreHandle_t mutex_; // 互斥锁
};
