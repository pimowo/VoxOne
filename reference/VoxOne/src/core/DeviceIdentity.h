#pragma once

#include <Arduino.h>
#include <esp_mac.h>

namespace DeviceIdentity {
inline String mac6Upper() {
    uint8_t mac[6] = {};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return "000000";
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    return String(suffix);
}

inline String mac6Lower() {
    String suffix = mac6Upper();
    suffix.toLowerCase();
    return suffix;
}
}
