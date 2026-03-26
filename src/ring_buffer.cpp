#include "ring_buffer.h"

#include <esp_heap_caps.h>
#include <string.h>

#include "logger.h"

/**
 * @file ring_buffer.cpp
 * @brief 环形缓冲区实现
 * @details 提供线程安全的环形缓冲区，支持跨边界读写操作，用于音频数据的临时存储
 */

namespace {

/**
 * @brief 分配缓冲区内存
 * @param size 所需内存大小
 * @return 分配的内存指针，失败返回nullptr
 * @note 优先尝试从SPI RAM分配内存，失败则使用普通内存
 */
void* allocBuffer(size_t size) {
    // 尝试从SPI RAM分配内存，要求8位对齐
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == nullptr) {
        // SPI RAM分配失败，尝试普通内存分配
        ptr = malloc(size);
    }
    return ptr;
}

}  // namespace

/**
 * @brief 环形缓冲区构造函数
 * @param requested_size 请求的缓冲区大小
 * @note 实际分配的存储大小为requested_size + 1，用于区分缓冲区满和空的状态
 */
RingBuffer::RingBuffer(size_t requested_size)
    : buffer_(nullptr),           // 缓冲区指针
      requested_size_(requested_size),  // 请求的缓冲区大小
      storage_size_(requested_size + 1), // 实际存储大小（+1用于区分满/空）
      head_(0),                   // 写入位置指针
      tail_(0),                   // 读取位置指针
      mutex_(nullptr) {           // 互斥锁指针
}

/**
 * @brief 环形缓冲区析构函数
 * @note 调用end()方法释放资源
 */
RingBuffer::~RingBuffer() {
    end();
}

/**
 * @brief 初始化环形缓冲区
 * @return 初始化成功返回true，失败返回false
 * @note 分配内存并创建互斥锁
 */
bool RingBuffer::begin() {
    // 如果缓冲区已经初始化，直接返回成功
    if (buffer_ != nullptr) {
        return true;
    }

    // 分配缓冲区内存
    buffer_ = static_cast<uint8_t*>(allocBuffer(storage_size_));
    if (buffer_ == nullptr) {
        LOGE("RING", "Allocation failed for %u bytes", static_cast<unsigned>(storage_size_));
        return false;
    }

    // 创建互斥锁用于线程安全操作
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) {
        // 互斥锁创建失败，释放缓冲区内存
        free(buffer_);
        buffer_ = nullptr;
        LOGE("RING", "Mutex creation failed");
        return false;
    }

    // 初始化缓冲区内容为0
    memset(buffer_, 0, storage_size_);
    // 重置读写指针
    head_ = 0;
    tail_ = 0;
    return true;
}

/**
 * @brief 释放环形缓冲区资源
 * @note 释放互斥锁和缓冲区内存
 */
void RingBuffer::end() {
    // 释放互斥锁
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
    // 释放缓冲区内存
    if (buffer_ != nullptr) {
        free(buffer_);
        buffer_ = nullptr;
    }
    // 重置读写指针
    head_ = 0;
    tail_ = 0;
}

/**
 * @brief 向环形缓冲区写入数据
 * @param data 要写入的数据指针
 * @param len 要写入的数据长度
 * @return 实际写入的数据长度
 * @note 线程安全，会自动处理环形边界
 */
size_t RingBuffer::write(const uint8_t* data, size_t len) {
    // 参数有效性检查
    if (buffer_ == nullptr || mutex_ == nullptr || data == nullptr || len == 0) {
        return 0;
    }

    // 尝试获取互斥锁，超时时间5ms
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) {
        return 0;
    }

    // 计算实际可写入的长度（不超过可用空间）
    const size_t to_write = len < freeSpaceUnsafe() ? len : freeSpaceUnsafe();
    if (to_write == 0) {
        // 缓冲区已满，释放互斥锁并返回0
        xSemaphoreGive(mutex_);
        return 0;
    }

    // 计算从当前head位置到缓冲区末尾的长度
    const size_t first = storage_size_ - head_;
    if (first >= to_write) {
        // 无需跨边界，直接写入
        memcpy(buffer_ + head_, data, to_write);
        // 更新head位置
        head_ = (head_ + to_write) % storage_size_;
    } else {
        // 需要跨边界，分两次写入
        // 先写入从head到缓冲区末尾的部分
        memcpy(buffer_ + head_, data, first);
        // 再写入从缓冲区开头的剩余部分
        memcpy(buffer_, data + first, to_write - first);
        // 更新head位置
        head_ = to_write - first;
    }

    // 释放互斥锁
    xSemaphoreGive(mutex_);
    // 返回实际写入的长度
    return to_write;
}

/**
 * @brief 从环形缓冲区读取数据
 * @param data 存储读取数据的缓冲区指针
 * @param len 要读取的数据长度
 * @return 实际读取的数据长度
 * @note 线程安全，会自动处理环形边界
 */
size_t RingBuffer::read(uint8_t* data, size_t len) {
    // 参数有效性检查
    if (buffer_ == nullptr || mutex_ == nullptr || data == nullptr || len == 0) {
        return 0;
    }

    // 尝试获取互斥锁，超时时间5ms
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) {
        return 0;
    }

    // 计算实际可读取的长度（不超过可用数据）
    const size_t to_read = len < sizeUnsafe() ? len : sizeUnsafe();
    if (to_read == 0) {
        // 缓冲区为空，释放互斥锁并返回0
        xSemaphoreGive(mutex_);
        return 0;
    }

    // 计算从当前tail位置到缓冲区末尾的长度
    const size_t first = storage_size_ - tail_;
    if (first >= to_read) {
        // 无需跨边界，直接读取
        memcpy(data, buffer_ + tail_, to_read);
        // 更新tail位置
        tail_ = (tail_ + to_read) % storage_size_;
    } else {
        // 需要跨边界，分两次读取
        // 先读取从tail到缓冲区末尾的部分
        memcpy(data, buffer_ + tail_, first);
        // 再读取从缓冲区开头的剩余部分
        memcpy(data + first, buffer_, to_read - first);
        // 更新tail位置
        tail_ = to_read - first;
    }

    // 释放互斥锁
    xSemaphoreGive(mutex_);
    // 返回实际读取的长度
    return to_read;
}

/**
 * @brief 获取环形缓冲区中已使用的空间大小
 * @return 已使用的空间大小
 * @note 线程安全，通过互斥锁保护
 */
size_t RingBuffer::size() {
    // 检查互斥锁是否存在
    if (mutex_ == nullptr) {
        return 0;
    }
    // 尝试获取互斥锁，超时时间2ms
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
        return 0;
    }
    // 调用非线程安全版本的sizeUnsafe()方法
    const size_t result = sizeUnsafe();
    // 释放互斥锁
    xSemaphoreGive(mutex_);
    // 返回结果
    return result;
}

/**
 * @brief 获取环形缓冲区中可用的空间大小
 * @return 可用的空间大小
 * @note 线程安全，通过互斥锁保护
 */
size_t RingBuffer::freeSpace() {
    // 检查互斥锁是否存在
    if (mutex_ == nullptr) {
        return 0;
    }
    // 尝试获取互斥锁，超时时间2ms
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
        return 0;
    }
    // 调用非线程安全版本的freeSpaceUnsafe()方法
    const size_t result = freeSpaceUnsafe();
    // 释放互斥锁
    xSemaphoreGive(mutex_);
    // 返回结果
    return result;
}

/**
 * @brief 获取环形缓冲区的填充百分比
 * @return 填充百分比（0.0-100.0）
 * @note 基于requested_size_计算，而非实际存储大小
 */
float RingBuffer::fillPercent() {
    // 避免除以零
    if (requested_size_ == 0) {
        return 0.0f;
    }
    // 计算填充百分比
    return (static_cast<float>(size()) * 100.0f) / static_cast<float>(requested_size_);
}

/**
 * @brief 清空环形缓冲区
 * @note 线程安全，通过互斥锁保护
 */
void RingBuffer::clear() {
    // 检查互斥锁是否存在
    if (mutex_ == nullptr) {
        return;
    }
    // 尝试获取互斥锁，超时时间5ms
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;
    }
    // 重置读写指针，实现清空操作
    head_ = 0;
    tail_ = 0;
    // 释放互斥锁
    xSemaphoreGive(mutex_);
}

/**
 * @brief 获取环形缓冲区中已使用的空间大小（非线程安全版本）
 * @return 已使用的空间大小
 * @note 内部使用，不进行互斥锁保护
 */
size_t RingBuffer::sizeUnsafe() const {
    if (head_ >= tail_) {
        // head在tail之后或相同位置，直接相减
        return head_ - tail_;
    }
    // head在tail之前，需要计算环形长度
    return storage_size_ - tail_ + head_;
}

/**
 * @brief 获取环形缓冲区中可用的空间大小（非线程安全版本）
 * @return 可用的空间大小
 * @note 内部使用，不进行互斥锁保护
 */
size_t RingBuffer::freeSpaceUnsafe() const {
    // 可用空间 = 总存储大小 - 已使用空间 - 1（用于区分满/空状态）
    return storage_size_ - sizeUnsafe() - 1;
}
