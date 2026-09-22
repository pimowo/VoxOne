#pragma once

#include <Arduino.h>

namespace ConfigSchema {
static constexpr uint16_t CURRENT_VERSION = 8;
}

enum class DefaultSource : uint8_t { Stop, Radio, Bluetooth };
enum class OutputType : uint8_t { PCM5102A, MAX98357A };
enum class PlayMediaVolumeMode : uint8_t { Current, Fixed };
enum class DisplayType : uint8_t { ST7789, SSD1306 };
enum class EncoderDirection : uint8_t { Normal, Reversed };

struct DeviceConfig {
    String name = "VoxOne";
};

struct AudioConfig {
    // Existing logical volume and cap retain their NVS keys and runtime behavior.
    int volume = 25;
    int maxVolume = 100;
    int startVolume = 25;
    int maxOutputVolume = 100;
    DefaultSource defaultSource = DefaultSource::Stop;
    OutputType outputType = OutputType::PCM5102A;
    int i2sBclk = 26;
    int i2sLrclk = 25;
    int i2sDout = 27;
};

struct PlayMediaConfig {
    PlayMediaVolumeMode volumeMode = PlayMediaVolumeMode::Current;
    int fixedVolume = 60;
};

struct BluetoothConfig {
    bool autoReconnect = true;       // Existing bt_reconn key.
    uint32_t reconnectDelayMs = 10000; // Existing bt_reconn_ms key.
    String deviceName;               // Empty means inherit device.name.
    bool discoverableEnabled = true;
    uint32_t discoverableSec = 120;
    bool rememberLastPeer = true;
};

struct RadioConfig {
    int defaultStation = 1;
    bool autostart = false;
    bool reconnectEnabled = true;
    uint32_t streamTimeoutMs = 10000;
    bool icyMetadataEnabled = true;
};

struct St7789Pins {
    int sck = 18;
    int mosi = 23;
    int cs = 5;
    int dc = 4;
    int rst = -1;
};

struct Ssd1306Pins {
    int sda = 21;
    int scl = 22;
    uint8_t address = 0x3C;
};

struct DisplayConfig {
    DisplayType type = DisplayType::ST7789;
    int brightness = 80;
    bool screensaverEnabled = false;
    uint32_t screensaverTimeoutSec = 300;
    St7789Pins st7789;
    Ssd1306Pins ssd1306;
};

struct EncoderConfig {
    int pinA = 35;
    int pinB = 33;
    int pinButton = 32;
    EncoderDirection direction = EncoderDirection::Reversed;
    int volumeStep = 1;
    bool accelerationEnabled = true;
};

struct UiConfig {
    uint32_t navigationTimeoutMs = 5000;
};

struct MqttConfig {
    String host;
    uint16_t port = 1883;
    String username;
    String password;
    String rootTopic;
};

struct YoRadioConfig {
    bool playlistCompat = true;
    bool extensionsEnabled = true;
};

struct WifiProfile {
    String ssid;
    String password;
    bool enabled = false;
    uint8_t priority = 0;
};

struct NetworkConfig {
    static constexpr uint8_t PROFILE_COUNT = 5;
    WifiProfile profiles[PROFILE_COUNT] = {
        {"", "", false, 100}, {"", "", false, 80},
        {"", "", false, 60}, {"", "", false, 40},
        {"", "", false, 20}
    };
    int8_t lastGoodIndex = -1;
    String wifiSsid;
    String wifiPassword;
    String hostname = "voxone";
    bool mdnsEnabled = true;
    bool dhcpEnabled = true;
    bool ntpEnabled = true;
    String timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
};

struct FeaturesConfig {
    bool webEnabled = false; // Deprecated: MAIN WebService is always on.
    bool bluetoothEnabled = false; // External VoxOneBT, never local A2DP.
    bool radioEnabled = true;
    bool playMediaEnabled = true; // Deprecated/hidden: PLAY_MEDIA is system-level.
    bool displayEnabled = true;
    bool encoderEnabled = true;
    bool buttonsEnabled = false; // Stored for future hardware; hidden in WWW.
    bool mqttEnabled = false;
    bool yoRadioWsEnabled = true; // Stored-only; hidden until runtime exists.
    bool haDiscoveryEnabled = false; // Legacy native discovery, hidden in WWW.
};

struct RuntimeConfig {
    uint16_t schemaVersion = ConfigSchema::CURRENT_VERSION;
    DeviceConfig device;
    AudioConfig audio;
    PlayMediaConfig playMedia;
    BluetoothConfig bluetooth;
    RadioConfig radio;
    DisplayConfig display;
    EncoderConfig encoder;
    UiConfig ui;
    MqttConfig mqtt;
    YoRadioConfig yoRadio;
    NetworkConfig network;
    FeaturesConfig features;
};
