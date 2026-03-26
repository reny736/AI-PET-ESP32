#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

enum class OtaTarget {
    Esp32Self,
    Stm32
};

class OtaManager {
public:
    OtaManager();

    void begin(HardwareSerial& aux_serial);

    OtaTarget selectedTarget() const { return selected_target_; }
    void setSelectedTarget(OtaTarget target) { selected_target_ = target; }
    const char* selectedTargetName() const;

    bool canAutoEnterStm32Bootloader() const;
    bool runSelectedUpdate(const String& url, const String& expected_sha256 = "");
    bool runEsp32Update(const String& url, const String& expected_sha256 = "");
    bool runStm32Update(const String& url, const String& expected_sha256 = "");

    const String& lastError() const { return last_error_; }

private:
    void setError(const String& error);

    HardwareSerial* aux_serial_;
    OtaTarget selected_target_;
    String last_error_;
};
