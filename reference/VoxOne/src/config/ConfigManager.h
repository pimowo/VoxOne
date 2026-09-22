#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "ConfigModel.h"

class ConfigManager {
public:
    bool begin();

    const RuntimeConfig& config() const { return _config; }
    uint16_t schemaVersion() const { return _config.schemaVersion; }

    // Save validates and persists a complete candidate. Runtime reloads on reboot.
    bool save(const RuntimeConfig& candidate);
    bool resetToDefaults();
    bool validate(const RuntimeConfig& candidate) const;
    DefaultSource effectiveDefaultSource() const;

    String wifiSsid() const { return _config.network.wifiSsid; }
    String wifiPassword() const { return _config.network.wifiPassword; }
    bool saveWifi(const String& ssid, const String& password);
    void clearWifi();
    bool saveLastGoodProfile(int8_t index);

    int volume() const { return _config.audio.volume; }
    void saveVolume(int value);

    int maxVolume() const { return _config.audio.maxVolume; }
    bool saveMaxVolume(int value);
    bool saveDefaultStation(uint16_t id);

    bool bluetoothAutoReconnect() const {
        return _config.bluetooth.autoReconnect;
    }
    bool saveBluetoothAutoReconnect(bool enabled);

    uint32_t bluetoothReconnectDelayMs() const {
        return _config.bluetooth.reconnectDelayMs;
    }
    bool saveBluetoothReconnectDelayMs(uint32_t value);
    const FeaturesConfig& features() const { return _config.features; }

private:
    Preferences _prefs;
    Preferences _legacyPrefs;
    RuntimeConfig _config;

    bool load(bool allowFallback = true);
    void loadBackend();
    bool migrateIfNeeded(uint16_t storedVersion);
    bool initializeSchemaV1();
    bool migrateV1ToV2();
    bool migrateV2ToV3();
    bool migrateV3ToV4();
    bool migrateV4ToV5();
    bool migrateV5ToV6();
    bool migrateV6ToV7();
    bool migrateV7ToV8();
    bool migrateLegacyNamespace();
    bool writeSnapshot(const RuntimeConfig& candidate, uint16_t version = ConfigSchema::CURRENT_VERSION);
};
