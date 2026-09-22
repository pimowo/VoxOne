#pragma once
#include <memory>

#include "StateStore.h"
#include "BoardConfig.h"

#include "../config/ConfigManager.h"
#include "../hal/EncoderInput.h"
#include "../ui/DisplayService.h"
#include "../ui/UiMode.h"
#include "../audio/AudioOutputManager.h"
#include "../btlink/RemoteBluetoothBackend.h"
#include "../audio/BtPcmInput.h"
#include "../radio/RadioService.h"
#include "../playmedia/PlayMediaService.h"
#include "../network/WiFiService.h"
#include "../network/WebService.h"
#include "../time/TimeService.h"
#include "../mqtt/MqttService.h"

class App {
public:
    bool begin();
    void loop();

private:
    ConfigManager _config;
    bool _timeStarted = false;
    EncoderInput _encoder;
    std::unique_ptr<DisplayService> _display;

    AudioOutputManager _audioOutput;
    RemoteBluetoothBackend _btLink{Serial2, Board::BT_UART_RX, Board::BT_UART_TX};
    UnwiredBtPcmInput _btPcmInput;
    RadioService _radio;
    bool _radioAvailable = false;
    bool _radioSession = false;
    AudioSource _sourceIntent = AudioSource::Stop;
    PlayMediaService _playMedia;
    bool _playMediaAvailable = false;
    bool _playMediaOverrideActive = false;
    struct PlayMediaSnapshot {
        bool valid = false;
        AudioSource baseSource = AudioSource::Stop;
        uint16_t stationId = 0;
        bool radioWasPlaying = false;
        bool radioWasPaused = false;
        bool btWasPlaying = false;
        int logicalVolume = 0; // Diagnostic snapshot; global volume is not restored.
    } _playMediaSnapshot;
    String _pendingPlayMediaUrl;
    bool _yoRadioOn = true;
    AudioSource _yoRadioRestoreSource = AudioSource::Stop;
    uint16_t _yoRadioRestoreStationId = 0;
    bool _yoRadioDuplicateStopArmed = false;
    uint32_t _yoRadioDuplicateStopUntil = 0;
    static constexpr uint32_t YORADIO_DUPLICATE_STOP_GUARD_MS = 1500;
    char _radioCommand[24] = {0};
    uint8_t _radioCommandLength = 0;
    bool _radioCommandOverflow = false;

    WiFiService _wifi;
    WebService _web;
    TimeService _time;
    MqttService _mqtt;

    uint32_t _volumeSaveDue = 0;
    bool _volumeDirty = false;
    UiMode _uiMode = UiMode::Home;
    uint32_t _overlayActivityMs = 0;
    bool _btNavArtistPending = false;
    uint16_t _radioHighlightedStationId = 0;

    BtModuleState _lastBtModuleState = BtModuleState::Disabled;

    void processCommands();
    void enterMode(UiMode mode);
    void returnHome();
    void touchOverlayTimeout();
    bool startRadio(uint16_t stationId);
    void stopRadio();
    bool selectSource(AudioSource source, uint16_t stationId = 0,
                      bool startPlayback = true);
    bool selectSourceDuringPlayMedia(AudioSource source, uint16_t stationId);
    bool queuePlayMediaUrl(const String& url);
    void startPendingPlayMedia();
    void startPlayMedia(const String& url);
    void finishPlayMedia(const char* reason);
    void restoreAfterPlayMedia();
    void cancelPlayMedia(const char* reason);
    void processRadioTestCommands();
    void updateRemoteBluetooth();
    void mqttSetVolume(int value);
    void mqttPlay();
    void mqttPause();
    void mqttToggle();
    void mqttNext();
    void mqttPrevious();
    void mqttYoRadioStop();
    void mqttYoRadioTurnOn();
    void mqttYoRadioTurnOff();
    void pauseRadio();
    void resumeRadio();
    void toggleBasePlayback();
    void handleEncoderDoubleClick();
    void handleStationRemoved(uint16_t id);
};
