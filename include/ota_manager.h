#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

/**
 * @enum OtaTarget
 * @brief OTA更新目标枚举
 */
enum class OtaTarget {
    Esp32Self,  // ESP32自身
    Stm32       // STM32设备
};

/**
 * @class OtaManager
 * @brief OTA更新管理器类
 * @details 负责处理ESP32和STM32设备的OTA更新，包括固件下载、验证和刷新等功能
 */
class OtaManager {
public:
    /**
     * @brief 构造函数
     */
    OtaManager();

    /**
     * @brief 初始化OTA管理器
     * @param aux_serial 辅助串口
     */
    void begin(HardwareSerial& aux_serial);

    /**
     * @brief 获取当前选择的更新目标
     * @return 更新目标
     */
    OtaTarget selectedTarget() const { return selected_target_; }
    
    /**
     * @brief 设置更新目标
     * @param target 更新目标
     */
    void setSelectedTarget(OtaTarget target) { selected_target_ = target; }
    
    /**
     * @brief 获取当前选择的更新目标名称
     * @return 更新目标名称
     */
    const char* selectedTargetName() const;

    /**
     * @brief 检查是否可以自动进入STM32 bootloader
     * @return 可以返回true，否则返回false
     */
    bool canAutoEnterStm32Bootloader() const;
    
    /**
     * @brief 运行当前选择的更新
     * @param url 固件URL
     * @param expected_sha256 期望的SHA256校验和
     * @return 更新成功返回true，失败返回false
     */
    bool runSelectedUpdate(const String& url, const String& expected_sha256 = "");
    
    /**
     * @brief 运行ESP32更新
     * @param url 固件URL
     * @param expected_sha256 期望的SHA256校验和
     * @return 更新成功返回true，失败返回false
     */
    bool runEsp32Update(const String& url, const String& expected_sha256 = "");
    
    /**
     * @brief 运行STM32更新
     * @param url 固件URL
     * @param expected_sha256 期望的SHA256校验和
     * @return 更新成功返回true，失败返回false
     */
    bool runStm32Update(const String& url, const String& expected_sha256 = "");

    /**
     * @brief 获取最后一次错误信息
     * @return 错误信息
     */
    const String& lastError() const { return last_error_; }

private:
    /**
     * @brief 设置错误信息
     * @param error 错误信息
     */
    void setError(const String& error);

    HardwareSerial* aux_serial_;  // 辅助串口
    OtaTarget selected_target_;    // 选择的更新目标
    String last_error_;            // 最后一次错误信息
};
