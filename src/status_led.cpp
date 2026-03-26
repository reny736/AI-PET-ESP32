#include "status_led.h"

/**
 * @file status_led.cpp
 * @brief 状态LED控制实现
 * @details 负责控制状态LED的显示，根据应用状态显示不同的颜色和效果
 */

/**
 * @brief 构造函数，初始化状态LED
 * @param pin LED连接的GPIO引脚号
 */
StatusLed::StatusLed(int pin)
    : pin_(pin),
      state_(LedState::Off),
      last_tick_ms_(0),
      phase_(0),
      initialized_(false) {
}

/**
 * @brief 初始化LED硬件
 * @return 初始化成功返回true
 */
bool StatusLed::begin() {
    if (initialized_) {
        return true;
    }
    pinMode(pin_, OUTPUT);
    initialized_ = true;
    off();
    return true;
}

/**
 * @brief 设置LED状态
 * @param state 目标LED状态
 * @note 根据不同状态设置不同的颜色和动画效果
 */
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

/**
 * @brief 更新LED状态（动画效果）
 * @note 根据当前状态和时间更新LED的显示效果，包括呼吸灯和脉冲效果
 */
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

/**
 * @brief 关闭LED
 */
void StatusLed::off() {
    showColor(0, 0, 0);
}

/**
 * @brief 显示指定颜色
 * @param r 红色分量 (0-255)
 * @param g 绿色分量 (0-255)
 * @param b 蓝色分量 (0-255)
 */
void StatusLed::showColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!initialized_) {
        return;
    }
    neopixelWrite(pin_, r, g, b);
}
