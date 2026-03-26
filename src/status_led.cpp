#include "status_led.h"

StatusLed::StatusLed(int pin)
    : pin_(pin),
      state_(LedState::Off),
      last_tick_ms_(0),
      phase_(0),
      initialized_(false) {
}

bool StatusLed::begin() {
    if (initialized_) {
        return true;
    }
    pinMode(pin_, OUTPUT);
    initialized_ = true;
    off();
    return true;
}

void StatusLed::setState(LedState state) {
    state_ = state;
    phase_ = 0;
    last_tick_ms_ = 0;

    switch (state_) {
        case LedState::Off:
            off();
            break;
        case LedState::Speaking:
            showColor(255, 0, 120);
            break;
        case LedState::Ota:
            showColor(0, 0, 255);
            break;
        case LedState::Error:
            showColor(255, 0, 0);
            break;
        default:
            update();
            break;
    }
}

void StatusLed::update() {
    if (!initialized_) {
        return;
    }

    const uint32_t now = millis();
    if (last_tick_ms_ != 0 && (now - last_tick_ms_) < 24) {
        return;
    }
    last_tick_ms_ = now;
    phase_ = static_cast<uint16_t>((phase_ + 8) % 512);

    auto breath = [](uint16_t phase) -> uint8_t {
        const uint16_t folded = phase < 256 ? phase : (511 - phase);
        return static_cast<uint8_t>(32 + ((folded * 223U) / 255U));
    };

    auto pulse = [](uint16_t phase) -> uint8_t {
        const uint16_t folded = phase < 256 ? phase : (511 - phase);
        return static_cast<uint8_t>(8 + ((folded * 247U) / 255U));
    };

    switch (state_) {
        case LedState::Booting: {
            const uint8_t v = breath(phase_);
            showColor(v, v, v);
            break;
        }
        case LedState::WifiConnecting: {
            const uint8_t v = breath(phase_);
            showColor(0, v / 3, v);
            break;
        }
        case LedState::Standby: {
            const uint8_t v = pulse(phase_);
            showColor(0, v / 2, 0);
            break;
        }
        case LedState::ApiConnecting: {
            const uint8_t v = breath(phase_);
            showColor(0, v, v / 4);
            break;
        }
        case LedState::Ota:
            showColor(0, 0, 255);
            break;
        case LedState::Listening: {
            const uint8_t v = pulse(phase_);
            showColor(0, v, v / 2);
            break;
        }
        case LedState::Thinking: {
            const uint8_t v = breath(phase_);
            showColor(v, v / 3, 0);
            break;
        }
        case LedState::Speaking:
            showColor(255, 0, 120);
            break;
        case LedState::Error:
            if ((phase_ / 64) % 2 == 0) {
                showColor(255, 0, 0);
            } else {
                off();
            }
            break;
        case LedState::Off:
        default:
            off();
            break;
    }
}

void StatusLed::off() {
    showColor(0, 0, 0);
}

void StatusLed::showColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!initialized_) {
        return;
    }
    neopixelWrite(pin_, r, g, b);
}
