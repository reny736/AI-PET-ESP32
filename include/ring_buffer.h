#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class RingBuffer {
public:
    explicit RingBuffer(size_t requested_size);
    ~RingBuffer();

    bool begin();
    void end();

    size_t write(const uint8_t* data, size_t len);
    size_t read(uint8_t* data, size_t len);

    size_t size();
    size_t freeSpace();
    size_t capacity() const { return requested_size_; }
    float fillPercent();

    void clear();

private:
    size_t sizeUnsafe() const;
    size_t freeSpaceUnsafe() const;

    uint8_t* buffer_;
    const size_t requested_size_;
    const size_t storage_size_;
    size_t head_;
    size_t tail_;
    SemaphoreHandle_t mutex_;
};
