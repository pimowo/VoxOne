#include "ConfigManager.h"
#include "../diagnostics/Logger.h"

namespace {
// Legacy NVS compatibility: copy verified values from "dinaudio" to
// "voxone", write the completion marker last, and keep legacy data intact.
static constexpr const char* NVS_NAMESPACE = "voxone";
static constexpr const char* LEGACY_NVS_NAMESPACE = "dinaudio";
static constexpr const char* KEY_MIGRATION_COMPLETE = "mig_done";

static constexpr const char* KEY_SCHEMA_VERSION = "cfg_ver";
static constexpr const char* KEY_WIFI_SSID = "wifi_ssid";
static constexpr const char* KEY_WIFI_PASS = "wifi_pass";
static constexpr const char* KEY_VOLUME = "volume";

static constexpr const char* KEY_MAX_VOLUME = "max_volume";
static constexpr const char* KEY_BT_AUTO_RECONNECT = "bt_reconn";
static constexpr const char* KEY_BT_RECONNECT_DELAY = "bt_reconn_ms";

static constexpr const char* KEY_FEATURE_WEB = "feat_web";
static constexpr const char* KEY_FEATURE_BT = "feat_bt";
static constexpr const char* KEY_FEATURE_RADIO = "feat_radio";
static constexpr const char* KEY_FEATURE_PLAY = "feat_play";
static constexpr const char* KEY_FEATURE_DISPLAY = "feat_disp";
static constexpr const char* KEY_FEATURE_ENCODER = "feat_enc";
static constexpr const char* KEY_FEATURE_BUTTONS = "feat_btn";
static constexpr const char* KEY_FEATURE_MQTT = "feat_mqtt";
static constexpr const char* KEY_FEATURE_YORADIO = "feat_yoradio";
static constexpr const char* KEY_FEATURE_HA = "feat_ha";


static constexpr int DEFAULT_VOLUME = 25;
static constexpr int DEFAULT_MAX_VOLUME = 100;
static constexpr bool DEFAULT_BT_AUTO_RECONNECT = true;
static constexpr uint32_t DEFAULT_BT_RECONNECT_DELAY_MS = 10000;
}

bool ConfigManager::begin() {
    if (!_prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    if (!_prefs.getBool(KEY_MIGRATION_COMPLETE, false) &&
        !migrateLegacyNamespace()) {
        return false;
    }

    const uint16_t storedVersion =
        _prefs.getUShort(KEY_SCHEMA_VERSION, 0);

    if (!migrateIfNeeded(storedVersion)) {
        return false;
    }

    return load();
}

bool ConfigManager::migrateIfNeeded(uint16_t storedVersion) {
    if (storedVersion > ConfigSchema::CURRENT_VERSION) {
        // Newer firmware wrote the configuration.
        // Do not overwrite unknown/newer data.
        return true;
    }

    uint16_t version = storedVersion;

    if (version == 0) {
        if (!initializeSchemaV1()) {
            return false;
        }
        version = 1;
    }

    if (version == 1) {
        if (!migrateV1ToV2()) {
            return false;
        }
        version = 2;
    }

    if (version == 2) {
        if (!migrateV2ToV3()) {
            return false;
        }
        version = 3;
    }

    if (version == 3) {
        if (!migrateV3ToV4()) return false;
        version = 4;
    }
    if (version == 4) {
        if (!migrateV4ToV5()) return false;
        version = 5;
    }

    if (version == 5) {
        if (!migrateV5ToV6()) return false;
        version = 6;
    }
    if (version == 6) {
        if (!migrateV6ToV7()) return false;
        version = 7;
    }

    if (version == 7) {
        if (!migrateV7ToV8()) return false;
        version = 8;
    }

    return version == ConfigSchema::CURRENT_VERSION;
}

bool ConfigManager::migrateLegacyNamespace() {
    // A read-only open leaves a fresh device's legacy namespace untouched.
    if (!_legacyPrefs.begin(LEGACY_NVS_NAMESPACE, true)) {
        return _prefs.putBool(KEY_MIGRATION_COMPLETE, true) > 0;
    }

    auto copyString = [this](const char* key) {
        if (!_legacyPrefs.isKey(key)) return true;
        const String value = _legacyPrefs.getString(key, "");
        // Preferences::putString returns zero for a valid empty string.
        _prefs.putString(key, value);
        return _prefs.isKey(key) && _prefs.getString(key, "") == value;
    };
    auto copyInt = [this](const char* key) {
        if (!_legacyPrefs.isKey(key)) return true;
        const int value = _legacyPrefs.getInt(key, 0);
        return _prefs.putInt(key, value) > 0 &&
               _prefs.getInt(key, 0) == value;
    };
    auto copyUInt = [this](const char* key) {
        if (!_legacyPrefs.isKey(key)) return true;
        const uint32_t value = _legacyPrefs.getUInt(key, 0);
        return _prefs.putUInt(key, value) > 0 &&
               _prefs.getUInt(key, 0) == value;
    };
    auto copyBool = [this](const char* key) {
        if (!_legacyPrefs.isKey(key)) return true;
        const bool value = _legacyPrefs.getBool(key, false);
        return _prefs.putBool(key, value) > 0 &&
               _prefs.getBool(key, !value) == value;
    };
    auto copyVersion = [this]() {
        if (!_legacyPrefs.isKey(KEY_SCHEMA_VERSION)) return true;
        const uint16_t value = _legacyPrefs.getUShort(KEY_SCHEMA_VERSION, 0);
        return _prefs.putUShort(KEY_SCHEMA_VERSION, value) > 0 &&
               _prefs.getUShort(KEY_SCHEMA_VERSION, 0) == value;
    };

    const bool copied =
        copyString(KEY_WIFI_SSID) && copyString(KEY_WIFI_PASS) &&
        copyInt(KEY_VOLUME) && copyInt(KEY_MAX_VOLUME) &&
        copyBool(KEY_BT_AUTO_RECONNECT) &&
        copyUInt(KEY_BT_RECONNECT_DELAY) && copyVersion();
    _legacyPrefs.end();
    if (!copied) return false;

    // Marker last: interrupted migration repeats the verified copy.
    return _prefs.putBool(KEY_MIGRATION_COMPLETE, true) > 0;
}


bool ConfigManager::initializeSchemaV1() {
    if (!_prefs.isKey(KEY_MAX_VOLUME)) {
        _prefs.putInt(KEY_MAX_VOLUME, DEFAULT_MAX_VOLUME);
    }

    if (!_prefs.isKey(KEY_BT_AUTO_RECONNECT)) {
        // Historical schema-v1 default.
        _prefs.putBool(KEY_BT_AUTO_RECONNECT, false);
    }

    if (!_prefs.isKey(KEY_BT_RECONNECT_DELAY)) {
        _prefs.putUInt(
            KEY_BT_RECONNECT_DELAY,
            DEFAULT_BT_RECONNECT_DELAY_MS
        );
    }

    // Write schema marker last.
    return _prefs.putUShort(KEY_SCHEMA_VERSION, 1) > 0;
}

bool ConfigManager::migrateV1ToV2() {
    // v0.4.0 activates the BT reconnect/ownership feature.
    // Prior firmware exposed no user-facing control for this setting,
    // so schema-v2 enables it by default for existing installations.
    if (_prefs.putBool(
            KEY_BT_AUTO_RECONNECT,
            DEFAULT_BT_AUTO_RECONNECT
        ) == 0) {
        return false;
    }

    if (!_prefs.isKey(KEY_BT_RECONNECT_DELAY)) {
        if (_prefs.putUInt(
                KEY_BT_RECONNECT_DELAY,
                DEFAULT_BT_RECONNECT_DELAY_MS
            ) == 0) {
            return false;
        }
    }

    return _prefs.putUShort(
        KEY_SCHEMA_VERSION,
        2
    ) > 0;
}

bool ConfigManager::migrateV3ToV4() {
    return _prefs.putUShort(KEY_SCHEMA_VERSION, 4) > 0;
}

bool ConfigManager::migrateV4ToV5() {
    // Keep cfg_ver=4 until every schema-5 field has been written and verified.
    // Missing fields load from schema-5 defaults; invalid stored data must not
    // be replaced by the runtime safe fallback during migration.
    if (!load(false)) return false;
    return writeSnapshot(_config, 5);
}

bool ConfigManager::migrateV5ToV6() {
    // Keep cfg_ver=5 until the new field and full snapshot are verified.
    if (!load(false)) return false;
    return writeSnapshot(_config, 6);
}

bool ConfigManager::migrateV6ToV7() {
    // Existing installations start with WWW OFF in NORMAL MODE. The complete
    // verified snapshot is written before cfg_ver=7.
    if (!load(false)) return false;
    _config.features.webEnabled = false;
    if (!validate(_config)) return false;
    return writeSnapshot(_config, 7);
}

bool ConfigManager::migrateV7ToV8() {
    // Rebuild from schema-7 keys on every retry; incomplete profile keys
    // from a power loss never become an authoritative snapshot.
    if (!load(false)) return false;
    _config.network.lastGoodIndex = -1;
    if (!_config.network.wifiSsid.isEmpty()) {
        auto& first = _config.network.profiles[0];
        first.ssid = _config.network.wifiSsid;
        first.password = _config.network.wifiPassword;
        first.enabled = true;
        _config.network.lastGoodIndex = 0;
    }
    if (!validate(_config)) return false;
    return writeSnapshot(_config, 8);
}

bool ConfigManager::migrateV2ToV3() {
    // VoxOne 0.4.0: wydluzony grace period dla realnego
    // wylaczenia i ponownego wlaczenia Bluetooth w telefonie.
    if (_prefs.putUInt(
            KEY_BT_RECONNECT_DELAY,
            DEFAULT_BT_RECONNECT_DELAY_MS
        ) == 0) {
        return false;
    }

    return _prefs.putUShort(
        KEY_SCHEMA_VERSION,
        3
    ) > 0;
}

bool ConfigManager::load(bool allowFallback) {
    _config.schemaVersion =
        _prefs.getUShort(
            KEY_SCHEMA_VERSION,
            ConfigSchema::CURRENT_VERSION
        );

    _config.network.wifiSsid =
        _prefs.getString(KEY_WIFI_SSID, "");

    _config.network.wifiPassword =
        _prefs.getString(KEY_WIFI_PASS, "");

    if (_config.schemaVersion >= 8) {
        for (uint8_t i = 0; i < NetworkConfig::PROFILE_COUNT; ++i) {
            const String prefix = String("w") + i;
            auto& profile = _config.network.profiles[i];
            profile.ssid = _prefs.getString((prefix + "_ssid").c_str(), "");
            profile.password = _prefs.getString((prefix + "_pass").c_str(), "");
            profile.enabled = _prefs.getBool((prefix + "_en").c_str(), false);
            profile.priority = _prefs.getUChar((prefix + "_prio").c_str(), profile.priority);
        }
        _config.network.lastGoodIndex = _prefs.getChar("w_last", -1);
    }

    _config.audio.maxVolume =
        _prefs.getInt(KEY_MAX_VOLUME, DEFAULT_MAX_VOLUME);

    _config.audio.volume =
        _prefs.getInt(KEY_VOLUME, DEFAULT_VOLUME);

    _config.bluetooth.autoReconnect =
        _prefs.getBool(
            KEY_BT_AUTO_RECONNECT,
            DEFAULT_BT_AUTO_RECONNECT
        );

    _config.bluetooth.reconnectDelayMs =
        _prefs.getUInt(
            KEY_BT_RECONNECT_DELAY,
            DEFAULT_BT_RECONNECT_DELAY_MS
        );

    _config.features.webEnabled = _prefs.getBool(KEY_FEATURE_WEB, false);
    _config.features.bluetoothEnabled = _prefs.getBool(KEY_FEATURE_BT, false);
    _config.features.radioEnabled = _prefs.getBool(KEY_FEATURE_RADIO, true);
    _config.features.playMediaEnabled = _prefs.getBool(KEY_FEATURE_PLAY, true);
    _config.features.displayEnabled = _prefs.getBool(KEY_FEATURE_DISPLAY, true);
    _config.features.encoderEnabled = _prefs.getBool(KEY_FEATURE_ENCODER, true);
    _config.features.buttonsEnabled = _prefs.getBool(KEY_FEATURE_BUTTONS, false);
    _config.features.mqttEnabled = _prefs.getBool(KEY_FEATURE_MQTT, false);
    _config.features.yoRadioWsEnabled = _prefs.getBool(KEY_FEATURE_YORADIO, true);
    _config.features.haDiscoveryEnabled = _prefs.getBool(KEY_FEATURE_HA, false);
    loadBackend();
    if (_config.schemaVersion >= 8 && allowFallback) {
        const NetworkConfig defaults;
        for (uint8_t i = 0; i < NetworkConfig::PROFILE_COUNT; ++i) {
            const auto& profile = _config.network.profiles[i];
            if (profile.ssid.length() > 32 || profile.password.length() > 64 ||
                profile.priority > 100 ||
                (profile.enabled && profile.ssid.isEmpty())) {
                _config.network.profiles[i] = defaults.profiles[i];
                Logger::warn("CONFIG", "Invalid Wi-Fi profile ignored index=" + String(i));
            }
        }
        if (_config.network.lastGoodIndex < -1 ||
            _config.network.lastGoodIndex >= NetworkConfig::PROFILE_COUNT)
            _config.network.lastGoodIndex = -1;
    }
    if (!validate(_config)) {
        if (!allowFallback) return false;
        // Preserve only valid credentials/legacy values; boot with modules OFF.
        RuntimeConfig safe;
        if (_config.network.wifiSsid.length() <= 32 &&
            _config.network.wifiPassword.length() <= 64) {
            safe.network.wifiSsid = _config.network.wifiSsid;
            safe.network.wifiPassword = _config.network.wifiPassword;
        }
        if (_config.audio.maxVolume >= 1 &&
            _config.audio.maxVolume <= 100) {
            safe.audio.maxVolume = _config.audio.maxVolume;
        }
        if (_config.audio.volume >= 0 &&
            _config.audio.volume <= safe.audio.maxVolume) {
            safe.audio.volume = _config.audio.volume;
        }
        // Preserve each individually valid profile in the safe fallback.
        for (uint8_t i = 0; i < NetworkConfig::PROFILE_COUNT; ++i) {
            const auto& source = _config.network.profiles[i];
            if (source.ssid.length() <= 32 && source.password.length() <= 64 &&
                source.priority <= 100 && (!source.enabled || !source.ssid.isEmpty()))
                safe.network.profiles[i] = source;
        }
        if (_config.network.lastGoodIndex >= -1 &&
            _config.network.lastGoodIndex < NetworkConfig::PROFILE_COUNT)
            safe.network.lastGoodIndex = _config.network.lastGoodIndex;
        safe.bluetooth.autoReconnect = _config.bluetooth.autoReconnect;
        if (_config.bluetooth.reconnectDelayMs <= 60000) {
            safe.bluetooth.reconnectDelayMs =
                _config.bluetooth.reconnectDelayMs;
        }
        safe.features.bluetoothEnabled = false;
        safe.features.radioEnabled = false;
        safe.features.displayEnabled = false;
        safe.features.encoderEnabled = false;
        safe.features.playMediaEnabled = false;
        safe.features.mqttEnabled = false;
        safe.features.yoRadioWsEnabled = false;
        safe.features.haDiscoveryEnabled = false;
        safe.schemaVersion = _config.schemaVersion;
        if (!validate(safe)) {
            Logger::error("CONFIG", "Safe fallback validation failed");
            return false;
        }
        _config = safe;
        Logger::warn("CONFIG", "Invalid stored configuration; safe runtime defaults");
    }
    return true;
}

bool ConfigManager::saveWifi(const String& ssid, const String& password) {
    RuntimeConfig candidate = _config;
    String normalized = ssid;
    normalized.trim();
    if (normalized.isEmpty()) return false;
    candidate.network.profiles[0].ssid = normalized;
    candidate.network.profiles[0].password = password;
    candidate.network.profiles[0].enabled = true;
    candidate.network.wifiSsid = normalized;
    candidate.network.wifiPassword = password;
    return save(candidate);
}

void ConfigManager::clearWifi() {
    RuntimeConfig candidate = _config;
    for (auto& profile : candidate.network.profiles) {
        profile.ssid = "";
        profile.password = "";
        profile.enabled = false;
    }
    candidate.network.lastGoodIndex = -1;
    candidate.network.wifiSsid = "";
    candidate.network.wifiPassword = "";
    if (!save(candidate)) Logger::error("CONFIG", "Wi-Fi clear failed");
}

bool ConfigManager::saveLastGoodProfile(int8_t index) {
    if (index < 0 || index >= NetworkConfig::PROFILE_COUNT ||
        !_config.network.profiles[index].enabled ||
        _config.network.profiles[index].ssid.isEmpty()) return false;
    if (_config.network.lastGoodIndex == index) return true;
    if (_prefs.putChar("w_last", index) == 0 ||
        _prefs.getChar("w_last", -1) != index) return false;
    _config.network.lastGoodIndex = index;
    return true;
}

void ConfigManager::saveVolume(int value) {
    const int safeValue =
        constrain(value, 0, _config.audio.maxVolume);

    if (_prefs.putInt(KEY_VOLUME, safeValue) > 0) {
        _config.audio.volume = safeValue;
    }
}

bool ConfigManager::saveMaxVolume(int value) {
    const int safeValue = constrain(value, 1, 100);

    if (_prefs.putInt(KEY_MAX_VOLUME, safeValue) == 0) {
        return false;
    }

    _config.audio.maxVolume = safeValue;

    if (_config.audio.volume > safeValue) {
        saveVolume(safeValue);
    }

    return true;
}

bool ConfigManager::saveDefaultStation(uint16_t id) {
    if (_prefs.putInt("r_station", id) == 0 ||
        _prefs.getInt("r_station", -1) != id) return false;
    _config.radio.defaultStation = id;
    return true;
}

bool ConfigManager::saveBluetoothAutoReconnect(bool enabled) {
    if (_prefs.putBool(KEY_BT_AUTO_RECONNECT, enabled) == 0) {
        return false;
    }

    _config.bluetooth.autoReconnect = enabled;
    return true;
}

bool ConfigManager::saveBluetoothReconnectDelayMs(uint32_t value) {
    const uint32_t safeValue =
        constrain(
            value,
            static_cast<uint32_t>(0),
            static_cast<uint32_t>(60000)
        );

    if (_prefs.putUInt(KEY_BT_RECONNECT_DELAY, safeValue) == 0) {
        return false;
    }

    _config.bluetooth.reconnectDelayMs = safeValue;
    return true;
}
