#pragma once

#include <Arduino.h>

/**
 * @file status_led.h
 * @brief 状态LED控制
 * @details 负责控制状态LED的显示，根据应用状态显示不同的颜色和效果
 */

/**
 * @enum LedState
 * @brief LED状态枚举
 */
enum class LedState {
    Off,            // 关闭
    Booting,        // 启动中
    WifiConnecting, // Wi-Fi连接中
    Standby,        // 待机
    ApiConnecting,  // API连接中
    Ota,           // OTA更新
    Listening,      // 监听中
    Thinking,       // 思考中
    Speaking,       // 说话中
    Error           // 错误
};

/**
 * @class StatusLed
 * @brief 状态LED类
 * @details 负责控制状态LED的显示，根据应用状态显示不同的颜色和效果
 */
class StatusLed {
public:
    /**
     * @brief 构造函数
     * @param pin LED引脚
     */
    explicit StatusLed(int pin);

    /**
     * @brief 初始化LED
     * @return 初始化成功返回true，失败返回false
     */
    bool begin();
    
    /**
     * @brief 设置LED状态
     * @param state LED状态
     */
    void setState(LedState state);
    
    /**
     * @brief 获取当前LED状态
     * @return LED状态
     */
    LedState state() const { return state_; }
    
    /**
     * @brief 更新LED状态
     */
    void update();
    
    /**
     * @brief 关闭LED
     */
    void off();

private:
    /**
     * @brief 显示颜色
     * @param r 红色分量
     * @param g 绿色分量
     * @param b 蓝色分量
     */
    void showColor(uint8_t r, uint8_t g, uint8_t b);

    int pin_;  // LED引脚
    LedState state_;  // LED状态
    uint32_t last_tick_ms_;  // 最后一次更新时间
    uint16_t phase_;  // 相位
    bool initialized_;  // 是否已初始化
};
