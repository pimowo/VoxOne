#pragma once
#include <Arduino.h>
#include "../btlink/BtLink.h"

enum class PlaybackState : uint8_t {
    Stop,
    Playing,
    Paused
};

enum class AudioSource : uint8_t {
    Stop,
    Test,
    Bluetooth,
    Radio
};

enum class BluetoothOwnershipState : uint8_t {
    Disconnected,
    ConnectedIdle,
    Playing,
    ReconnectGrace
};

struct DeviceState {
    int volume = 25;
    PlaybackState playback = PlaybackState::Stop;
    AudioSource audioSource = AudioSource::Stop;
    // Orthogonal to audioSource: PLAY_MEDIA never becomes a base source.
    bool playMediaActive = false;

    bool bluetoothStarted = false;
    bool bluetoothConnected = false;
    bool bluetoothPlaying = false;
    BtModuleState bluetoothModuleState = BtModuleState::Disabled;
    bool bluetoothUnavailableNotice = false;
    String bluetoothDeviceName;
    String bluetoothPeerName;
    String bluetoothTitle;
    String bluetoothArtist;
    // Optional radio display metadata; current minimal RadioService does not publish it.
    String radioStation;
    String radioArtist;
    String radioTitle;
    uint32_t radioBitrate = 0;
    String radioCodec;

    BluetoothOwnershipState bluetoothOwnership =
        BluetoothOwnershipState::Disconnected;
    bool bluetoothReconnectGrace = false;

    bool wifiConnected = false;
    int8_t wifiActiveProfile = -1;
    int8_t wifiLastGoodProfile = -1;
    uint8_t wifiEnabledProfiles = 0;
    String wifiSsid;
    String ip;
    int wifiRssi = 0;

    bool apMode = false;
    String apSsid;
    String apIp;
    String hostname;

    bool timeValid = false;
    String clockText;

    String lastMessage = "BOOT";
};

class StateStore {
public:
    static StateStore& instance();
    DeviceState snapshot() const;
    void update(const DeviceState& s);

private:
    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    DeviceState _state;
};
