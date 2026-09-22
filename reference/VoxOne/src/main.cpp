#include <Arduino.h>
#include "core/App.h"

static App app;

void setup() {
    if (!app.begin()) {
        Serial.println("FATAL: VoxOne init failed");
    }
}

void loop() {
    app.loop();
}
