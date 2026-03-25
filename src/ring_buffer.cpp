#include "ring_buffer.h"

#include <esp_heap_caps.h>
#include <string.h>

#include "logger.h"

namespace {

void* allocBuffer(size_t size) {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == nullptr) {
        ptr = malloc(size);
    }
    return ptr;
}

}  // namespace

RingBuffer::RingBuffer(size_t requested_size)
    : buffer_(nullptr),
      requested_size_(requested_size),
      storage_size_(requested_size + 1),
      head_(0),
      tail_(0),
      mutex_(nullptr) {
}

RingBuffer::~RingBuffer() {
    end();
}

bool RingBuffer::begin() {
    if (buffer_ != nullptr) {
        return true;
    }

    buffer_ = static_cast<uint8_t*>(allocBuffer(storage_size_));
    if (buffer_ == nullptr) {
        LOGE("RING", "Allocation failed for %u bytes", static_cast<unsigned>(storage_size_));
        return false;
    }

    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) {
        free(buffer_);
        buffer_ = nullptr;
        LOGE("RING", "Mutex creation failed");
        return false;
    }

    memset(buffer_, 0, storage_size_);
    head_ = 0;
    tail_ = 0;
    return true;
}

void RingBuffer::end() {
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
    if (buffer_ != nullptr) {
        free(buffer_);
        buffer_ = nullptr;
    }
    head_ = 0;
    tail_ = 0;
}

size_t RingBuffer::write(const uint8_t* data, size_t len) {
    if (buffer_ == nullptr || mutex_ == nullptr || data == nullptr || len == 0) {
        return 0;
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) {
        return 0;
    }

    const size_t to_write = len < freeSpaceUnsafe() ? len : freeSpaceUnsafe();
    if (to_write == 0) {
        xSemaphoreGive(mutex_);
        return 0;
    }

    const size_t first = storage_size_ - head_;
    if (first >= to_write) {
        memcpy(buffer_ + head_, data, to_write);
        head_ = (head_ + to_write) % storage_size_;
    } else {
        memcpy(buffer_ + head_, data, first);
        memcpy(buffer_, data + first, to_write - first);
        head_ = to_write - first;
    }

    xSemaphoreGive(mutex_);
    return to_write;
}

size_t RingBuffer::read(uint8_t* data, size_t len) {
    if (buffer_ == nullptr || mutex_ == nullptr || data == nullptr || len == 0) {
        return 0;
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) {
        return 0;
    }

    const size_t to_read = len < sizeUnsafe() ? len : sizeUnsafe();
    if (to_read == 0) {
        xSemaphoreGive(mutex_);
        return 0;
    }

    const size_t first = storage_size_ - tail_;
    if (first >= to_read) {
        memcpy(data, buffer_ + tail_, to_read);
        tail_ = (tail_ + to_read) % storage_size_;
    } else {
        memcpy(data, buffer_ + tail_, first);
        memcpy(data + first, buffer_, to_read - first);
        tail_ = to_read - first;
    }

    xSemaphoreGive(mutex_);
    return to_read;
}

size_t RingBuffer::size() {
    if (mutex_ == nullptr) {
        return 0;
    }
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
        return 0;
    }
    const size_t result = sizeUnsafe();
    xSemaphoreGive(mutex_);
    return result;
}

size_t RingBuffer::freeSpace() {
    if (mutex_ == nullptr) {
        return 0;
    }
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
        return 0;
    }
    const size_t result = freeSpaceUnsafe();
    xSemaphoreGive(mutex_);
    return result;
}

float RingBuffer::fillPercent() {
    if (requested_size_ == 0) {
        return 0.0f;
    }
    return (static_cast<float>(size()) * 100.0f) / static_cast<float>(requested_size_);
}

void RingBuffer::clear() {
    if (mutex_ == nullptr) {
        return;
    }
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;
    }
    head_ = 0;
    tail_ = 0;
    xSemaphoreGive(mutex_);
}

size_t RingBuffer::sizeUnsafe() const {
    if (head_ >= tail_) {
        return head_ - tail_;
    }
    return storage_size_ - tail_ + head_;
}

size_t RingBuffer::freeSpaceUnsafe() const {
    return storage_size_ - sizeUnsafe() - 1;
}
