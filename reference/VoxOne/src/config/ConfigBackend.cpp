#include "ConfigManager.h"
#include "../diagnostics/Logger.h"
#include "BoardConfig.h"

namespace {
bool validPin(int pin, bool output) {
    if (pin < 0 || pin > 39 || (pin >= 6 && pin <= 11) ||
        pin == 20 || pin == 24 || (pin >= 28 && pin <= 31) ||
        pin == 37 || pin == 38) return false;
    if (output && pin >= 34) return false;
    return true;
}

bool validHostname(const String& value) {
    if (value.isEmpty() || value.length() > 63 ||
        value[0] == '-' || value[value.length() - 1] == '-') return false;
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}
}

void ConfigManager::loadBackend() {
    const RuntimeConfig defaults;
    _config.device.name = _prefs.getString("dev_name", defaults.device.name);
    _config.audio.startVolume = _prefs.getInt("a_start", defaults.audio.startVolume);
    _config.audio.maxOutputVolume = _prefs.getInt("a_outmax", defaults.audio.maxOutputVolume);
    _config.audio.defaultSource = static_cast<DefaultSource>(_prefs.getUChar("a_src", static_cast<uint8_t>(defaults.audio.defaultSource)));
    _config.audio.outputType = static_cast<OutputType>(_prefs.getUChar("a_type", static_cast<uint8_t>(defaults.audio.outputType)));
    _config.audio.i2sBclk = _prefs.getInt("a_bclk", defaults.audio.i2sBclk);
    _config.audio.i2sLrclk = _prefs.getInt("a_lrclk", defaults.audio.i2sLrclk);
    _config.audio.i2sDout = _prefs.getInt("a_dout", defaults.audio.i2sDout);
    _config.playMedia.volumeMode = static_cast<PlayMediaVolumeMode>(_prefs.getUChar("p_mode", static_cast<uint8_t>(defaults.playMedia.volumeMode)));
    _config.playMedia.fixedVolume = _prefs.getInt("p_vol", defaults.playMedia.fixedVolume);
    _config.bluetooth.deviceName = _prefs.getString("b_name", defaults.bluetooth.deviceName);
    _config.bluetooth.discoverableEnabled = _prefs.getBool("b_disc", defaults.bluetooth.discoverableEnabled);
    _config.bluetooth.discoverableSec = _prefs.getUInt("b_disc_sec", defaults.bluetooth.discoverableSec);
    _config.bluetooth.rememberLastPeer = _prefs.getBool("b_peer", defaults.bluetooth.rememberLastPeer);
    _config.radio.defaultStation = _prefs.getInt("r_station", defaults.radio.defaultStation);
    _config.radio.autostart = _prefs.getBool("r_auto", defaults.radio.autostart);
    _config.radio.reconnectEnabled = _prefs.getBool("r_reconn", defaults.radio.reconnectEnabled);
    _config.radio.streamTimeoutMs = _prefs.getUInt("r_timeout", defaults.radio.streamTimeoutMs);
    _config.radio.icyMetadataEnabled = _prefs.getBool("r_icy", defaults.radio.icyMetadataEnabled);
    _config.display.type = static_cast<DisplayType>(_prefs.getUChar("d_type", static_cast<uint8_t>(defaults.display.type)));
    _config.display.brightness = _prefs.getInt("d_bright", defaults.display.brightness);
    _config.display.screensaverEnabled = _prefs.getBool("d_save", defaults.display.screensaverEnabled);
    _config.display.screensaverTimeoutSec = _prefs.getUInt("d_save_sec", defaults.display.screensaverTimeoutSec);
    _config.display.st7789.sck = _prefs.getInt("d_sck", defaults.display.st7789.sck);
    _config.display.st7789.mosi = _prefs.getInt("d_mosi", defaults.display.st7789.mosi);
    _config.display.st7789.cs = _prefs.getInt("d_cs", defaults.display.st7789.cs);
    _config.display.st7789.dc = _prefs.getInt("d_dc", defaults.display.st7789.dc);
    _config.display.st7789.rst = _prefs.getInt("d_rst", defaults.display.st7789.rst);
    _config.display.ssd1306.sda = _prefs.getInt("d_sda", defaults.display.ssd1306.sda);
    _config.display.ssd1306.scl = _prefs.getInt("d_scl", defaults.display.ssd1306.scl);
    _config.display.ssd1306.address = _prefs.getUChar("d_addr", defaults.display.ssd1306.address);
    _config.encoder.pinA = _prefs.getInt("e_a", defaults.encoder.pinA);
    _config.encoder.pinB = _prefs.getInt("e_b", defaults.encoder.pinB);
    _config.encoder.pinButton = _prefs.getInt("e_btn", defaults.encoder.pinButton);
    _config.encoder.direction = static_cast<EncoderDirection>(_prefs.getUChar("e_dir", static_cast<uint8_t>(defaults.encoder.direction)));
    _config.encoder.volumeStep = _prefs.getInt("e_step", defaults.encoder.volumeStep);
    _config.encoder.accelerationEnabled = _prefs.getBool("e_accel", defaults.encoder.accelerationEnabled);
    _config.ui.navigationTimeoutMs = _prefs.getUInt("ui_nav_ms", defaults.ui.navigationTimeoutMs);
    _config.mqtt.host = _prefs.getString("m_host", defaults.mqtt.host);
    _config.mqtt.port = _prefs.getUShort("m_port", defaults.mqtt.port);
    _config.mqtt.username = _prefs.getString("m_user", defaults.mqtt.username);
    _config.mqtt.password = _prefs.getString("m_pass", defaults.mqtt.password);
    _config.mqtt.rootTopic = _prefs.getString("m_topic", defaults.mqtt.rootTopic);
    _config.yoRadio.playlistCompat = _prefs.getBool("y_playlist", defaults.yoRadio.playlistCompat);
    _config.yoRadio.extensionsEnabled = _prefs.getBool("y_ext", defaults.yoRadio.extensionsEnabled);
    _config.network.hostname = _prefs.getString("n_host", defaults.network.hostname);
    _config.network.mdnsEnabled = _prefs.getBool("n_mdns", defaults.network.mdnsEnabled);
    _config.network.dhcpEnabled = _prefs.getBool("n_dhcp", defaults.network.dhcpEnabled);
    _config.network.ntpEnabled = _prefs.getBool("n_ntp", defaults.network.ntpEnabled);
    _config.network.timezone = _prefs.getString("n_tz", defaults.network.timezone);
}

DefaultSource ConfigManager::effectiveDefaultSource() const {
    const auto source = _config.audio.defaultSource;
    if (source == DefaultSource::Bluetooth &&
        !_config.features.bluetoothEnabled) return DefaultSource::Stop;
    if (source == DefaultSource::Radio &&
        !_config.features.radioEnabled) return DefaultSource::Stop;
    if (source != DefaultSource::Bluetooth &&
        source != DefaultSource::Radio) return DefaultSource::Stop;
    return source;
}

bool ConfigManager::validate(const RuntimeConfig& c) const {
    if (c.device.name.isEmpty() || c.device.name.length() > 48 ||
        c.bluetooth.deviceName.length() > 48 ||
        c.network.wifiSsid.length() > 32 ||
        c.network.wifiPassword.length() > 64 ||
        c.network.lastGoodIndex < -1 ||
        c.network.lastGoodIndex >= NetworkConfig::PROFILE_COUNT ||
        !validHostname(c.network.hostname) ||
        c.network.timezone.isEmpty() || c.network.timezone.length() > 64 ||
        c.mqtt.host.length() > 128 || c.mqtt.username.length() > 128 ||
        c.mqtt.password.length() > 128 || c.mqtt.rootTopic.length() > 128)
        return false;

    for (const auto& profile : c.network.profiles) {
        if (profile.ssid.length() > 32 || profile.password.length() > 64 ||
            profile.priority > 100 ||
            (profile.enabled && profile.ssid.isEmpty())) return false;
    }

    if (c.audio.volume < 0 || c.audio.volume > 100 ||
        c.audio.maxVolume < 1 || c.audio.maxVolume > 100 ||
        c.audio.volume > c.audio.maxVolume ||
        c.audio.startVolume < 0 || c.audio.startVolume > 100 ||
        c.audio.maxOutputVolume < 0 || c.audio.maxOutputVolume > 100 ||
        c.playMedia.fixedVolume < 0 || c.playMedia.fixedVolume > 100 ||
        c.display.brightness < 0 || c.display.brightness > 100 ||
        c.encoder.volumeStep < 1 || c.encoder.volumeStep > 10 ||
        c.ui.navigationTimeoutMs < 1000 || c.ui.navigationTimeoutMs > 30000)
        return false;

    if (static_cast<uint8_t>(c.audio.defaultSource) >
            static_cast<uint8_t>(DefaultSource::Bluetooth) ||
        static_cast<uint8_t>(c.audio.outputType) >
            static_cast<uint8_t>(OutputType::MAX98357A) ||
        static_cast<uint8_t>(c.playMedia.volumeMode) >
            static_cast<uint8_t>(PlayMediaVolumeMode::Fixed) ||
        static_cast<uint8_t>(c.display.type) >
            static_cast<uint8_t>(DisplayType::SSD1306) ||
        static_cast<uint8_t>(c.encoder.direction) >
            static_cast<uint8_t>(EncoderDirection::Reversed))
        return false;

    if (c.bluetooth.reconnectDelayMs > 60000 ||
        c.bluetooth.discoverableSec < 1 || c.bluetooth.discoverableSec > 3600 ||
        c.radio.streamTimeoutMs < 1000 || c.radio.streamTimeoutMs > 60000 ||
        c.display.screensaverTimeoutSec < 10 ||
        c.display.screensaverTimeoutSec > 86400 ||
        c.radio.defaultStation < 0 || c.radio.defaultStation > 9999 ||
        c.mqtt.port == 0 ||
        (c.features.mqttEnabled && c.mqtt.host.isEmpty()))
        return false;

    if (c.display.ssd1306.address < 0x08 ||
        c.display.ssd1306.address > 0x77) return false;

    // Validate field shape even when its module is disabled. Only collision
    // claims below depend on feature flags.
    if (!validPin(c.audio.i2sBclk, true) ||
        !validPin(c.audio.i2sLrclk, true) ||
        !validPin(c.audio.i2sDout, true) ||
        !validPin(c.display.st7789.sck, true) ||
        !validPin(c.display.st7789.mosi, true) ||
        !validPin(c.display.st7789.cs, true) ||
        !validPin(c.display.st7789.dc, true) ||
        (c.display.st7789.rst != -1 &&
         !validPin(c.display.st7789.rst, true)) ||
        !validPin(c.display.ssd1306.sda, true) ||
        !validPin(c.display.ssd1306.scl, true) ||
        !validPin(c.encoder.pinA, false) ||
        !validPin(c.encoder.pinB, false) ||
        !validPin(c.encoder.pinButton, false)) return false;

    bool used[40] = {};
    auto claim = [&used](int pin, bool output) {
        if (!validPin(pin, output) || used[pin]) return false;
        used[pin] = true;
        return true;
    };
    // External VoxOneBT UART2 is fixed in the DESK board profile. Reject
    // any user-configured active peripheral on either pin.
    if (c.features.bluetoothEnabled &&
        (!claim(Board::BT_UART_TX, true) ||
         !claim(Board::BT_UART_RX, false))) return false;
    // PLAY_MEDIA is always available at system level, so the shared physical
    // I2S output is active independently of the two optional base sources.
    if (!claim(c.audio.i2sBclk, true) ||
        !claim(c.audio.i2sLrclk, true) ||
        !claim(c.audio.i2sDout, true)) return false;
    if (c.features.displayEnabled) {
        if (c.display.type == DisplayType::ST7789) {
            if (!claim(c.display.st7789.sck, true) ||
                !claim(c.display.st7789.mosi, true) ||
                !claim(c.display.st7789.cs, true) ||
                !claim(c.display.st7789.dc, true) ||
                (c.display.st7789.rst != -1 &&
                 !claim(c.display.st7789.rst, true))) return false;
        } else {
            if (!claim(c.display.ssd1306.sda, true) ||
                !claim(c.display.ssd1306.scl, true)) return false;
        }
    }
    if (c.features.encoderEnabled &&
        (!claim(c.encoder.pinA, false) ||
         !claim(c.encoder.pinB, false) ||
         !claim(c.encoder.pinButton, false))) return false;
    return true;
}

bool ConfigManager::writeSnapshot(const RuntimeConfig& candidate, uint16_t version) {
#define WRITE_SCALAR(key, kind, value) \
    do { \
        const auto expected = (value); \
        if (_prefs.put##kind(key, expected) == 0 || \
            _prefs.get##kind(key, expected) != expected) return false; \
    } while (false)
#define WRITE_STRING(key, value) \
    do { \
        const String expected = (value); \
        _prefs.putString(key, expected); \
        if (!_prefs.isKey(key) || _prefs.getString(key, "") != expected) \
            return false; \
    } while (false)
    WRITE_STRING("dev_name", candidate.device.name);
    WRITE_SCALAR("volume", Int, candidate.audio.volume);
    WRITE_SCALAR("max_volume", Int, candidate.audio.maxVolume);
    WRITE_SCALAR("a_start", Int, candidate.audio.startVolume);
    WRITE_SCALAR("a_outmax", Int, candidate.audio.maxOutputVolume);
    WRITE_SCALAR("a_src", UChar, static_cast<uint8_t>(candidate.audio.defaultSource));
    WRITE_SCALAR("a_type", UChar, static_cast<uint8_t>(candidate.audio.outputType));
    WRITE_SCALAR("a_bclk", Int, candidate.audio.i2sBclk);
    WRITE_SCALAR("a_lrclk", Int, candidate.audio.i2sLrclk);
    WRITE_SCALAR("a_dout", Int, candidate.audio.i2sDout);
    WRITE_SCALAR("p_mode", UChar, static_cast<uint8_t>(candidate.playMedia.volumeMode));
    WRITE_SCALAR("p_vol", Int, candidate.playMedia.fixedVolume);
    WRITE_SCALAR("bt_reconn", Bool, candidate.bluetooth.autoReconnect);
    WRITE_SCALAR("bt_reconn_ms", UInt, candidate.bluetooth.reconnectDelayMs);
    WRITE_STRING("b_name", candidate.bluetooth.deviceName);
    WRITE_SCALAR("b_disc", Bool, candidate.bluetooth.discoverableEnabled);
    WRITE_SCALAR("b_disc_sec", UInt, candidate.bluetooth.discoverableSec);
    WRITE_SCALAR("b_peer", Bool, candidate.bluetooth.rememberLastPeer);
    WRITE_SCALAR("r_station", Int, candidate.radio.defaultStation);
    WRITE_SCALAR("r_auto", Bool, candidate.radio.autostart);
    WRITE_SCALAR("r_reconn", Bool, candidate.radio.reconnectEnabled);
    WRITE_SCALAR("r_timeout", UInt, candidate.radio.streamTimeoutMs);
    WRITE_SCALAR("r_icy", Bool, candidate.radio.icyMetadataEnabled);
    WRITE_SCALAR("d_type", UChar, static_cast<uint8_t>(candidate.display.type));
    WRITE_SCALAR("d_bright", Int, candidate.display.brightness);
    WRITE_SCALAR("d_save", Bool, candidate.display.screensaverEnabled);
    WRITE_SCALAR("d_save_sec", UInt, candidate.display.screensaverTimeoutSec);
    WRITE_SCALAR("d_sck", Int, candidate.display.st7789.sck);
    WRITE_SCALAR("d_mosi", Int, candidate.display.st7789.mosi);
    WRITE_SCALAR("d_cs", Int, candidate.display.st7789.cs);
    WRITE_SCALAR("d_dc", Int, candidate.display.st7789.dc);
    WRITE_SCALAR("d_rst", Int, candidate.display.st7789.rst);
    WRITE_SCALAR("d_sda", Int, candidate.display.ssd1306.sda);
    WRITE_SCALAR("d_scl", Int, candidate.display.ssd1306.scl);
    WRITE_SCALAR("d_addr", UChar, candidate.display.ssd1306.address);
    WRITE_SCALAR("e_a", Int, candidate.encoder.pinA);
    WRITE_SCALAR("e_b", Int, candidate.encoder.pinB);
    WRITE_SCALAR("e_btn", Int, candidate.encoder.pinButton);
    WRITE_SCALAR("e_dir", UChar, static_cast<uint8_t>(candidate.encoder.direction));
    WRITE_SCALAR("e_step", Int, candidate.encoder.volumeStep);
    WRITE_SCALAR("e_accel", Bool, candidate.encoder.accelerationEnabled);
    WRITE_SCALAR("ui_nav_ms", UInt, candidate.ui.navigationTimeoutMs);
    WRITE_STRING("m_host", candidate.mqtt.host);
    WRITE_SCALAR("m_port", UShort, candidate.mqtt.port);
    WRITE_STRING("m_user", candidate.mqtt.username);
    WRITE_STRING("m_pass", candidate.mqtt.password);
    WRITE_STRING("m_topic", candidate.mqtt.rootTopic);
    WRITE_SCALAR("y_playlist", Bool, candidate.yoRadio.playlistCompat);
    WRITE_SCALAR("y_ext", Bool, candidate.yoRadio.extensionsEnabled);
    WRITE_STRING("wifi_ssid", candidate.network.wifiSsid);
    WRITE_STRING("wifi_pass", candidate.network.wifiPassword);
    for (uint8_t i = 0; i < NetworkConfig::PROFILE_COUNT; ++i) {
        const String prefix = String("w") + i;
        const auto& profile = candidate.network.profiles[i];
        WRITE_STRING((prefix + "_ssid").c_str(), profile.ssid);
        WRITE_STRING((prefix + "_pass").c_str(), profile.password);
        WRITE_SCALAR((prefix + "_en").c_str(), Bool, profile.enabled);
        WRITE_SCALAR((prefix + "_prio").c_str(), UChar, profile.priority);
    }
    WRITE_SCALAR("w_last", Char, candidate.network.lastGoodIndex);
    WRITE_STRING("n_host", candidate.network.hostname);
    WRITE_SCALAR("n_mdns", Bool, candidate.network.mdnsEnabled);
    WRITE_SCALAR("n_dhcp", Bool, candidate.network.dhcpEnabled);
    WRITE_SCALAR("n_ntp", Bool, candidate.network.ntpEnabled);
    WRITE_STRING("n_tz", candidate.network.timezone);
    WRITE_SCALAR("feat_web", Bool, candidate.features.webEnabled);
    WRITE_SCALAR("feat_bt", Bool, candidate.features.bluetoothEnabled);
    WRITE_SCALAR("feat_radio", Bool, candidate.features.radioEnabled);
    WRITE_SCALAR("feat_play", Bool, candidate.features.playMediaEnabled);
    WRITE_SCALAR("feat_disp", Bool, candidate.features.displayEnabled);
    WRITE_SCALAR("feat_enc", Bool, candidate.features.encoderEnabled);
    WRITE_SCALAR("feat_btn", Bool, candidate.features.buttonsEnabled);
    WRITE_SCALAR("feat_mqtt", Bool, candidate.features.mqttEnabled);
    WRITE_SCALAR("feat_yoradio", Bool, candidate.features.yoRadioWsEnabled);
    WRITE_SCALAR("feat_ha", Bool, candidate.features.haDiscoveryEnabled);
#undef WRITE_STRING
#undef WRITE_SCALAR
    return _prefs.putUShort("cfg_ver", version) > 0 &&
        _prefs.getUShort("cfg_ver", 0) == version;
}

bool ConfigManager::save(const RuntimeConfig& candidate) {
    if (!validate(candidate)) return false;
    const RuntimeConfig previous = _config;
    if (!writeSnapshot(candidate)) {
        // Best effort recovery from a storage error. No RAM state is changed.
        if (!writeSnapshot(previous))
            Logger::error("CONFIG", "NVS rollback failed; reboot required");
        return false;
    }
    // App observes _config throughout its loop. Apply the snapshot on reboot.
    return true;
}

bool ConfigManager::resetToDefaults() {
    return save(RuntimeConfig{});
}
