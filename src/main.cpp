#include <Arduino.h>

#include "realtime_voice_app.h"

namespace {

RealtimeVoiceApp g_app;

}  // namespace

void setup() {
    if (!g_app.begin()) {
        while (true) {
            delay(1000);
        }
    }
}

void loop() {
    g_app.loop();
}
