#pragma once

#include <Arduino.h>

enum class LedState {
    Off,
    Booting,
    WifiConnecting,
    Standby,
    ApiConnecting,
    Ota,
    Listening,
    Thinking,
    Speaking,
    Error
};

class StatusLed {
public:
    explicit StatusLed(int pin);

    bool begin();
    void setState(LedState state);
    LedState state() const { return state_; }
    void update();
    void off();

private:
    void showColor(uint8_t r, uint8_t g, uint8_t b);

    int pin_;
    LedState state_;
    uint32_t last_tick_ms_;
    uint16_t phase_;
    bool initialized_;
};
