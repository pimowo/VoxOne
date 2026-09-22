#pragma once

#include <Arduino.h>
#include <memory>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "../config/ConfigModel.h"
#include "UiMode.h"
#include "../core/StateStore.h"
#include "../radio/StationStore.h"

class ReusableCanvas16;

class DisplayService {
public:
    explicit DisplayService(const St7789Pins& pins);
    ~DisplayService();

    void begin();
    void finishStartup();
    void requestUpdate(UiMode mode, bool btNavArtistPending,
                       const StationStore& stations,
                       uint16_t highlightedStationId,
                       uint16_t playingStationId);
    void redraw();
    bool clearToBlack();
    bool isInitialized() const { return _initialized; }

private:
    struct DisplayInput {
        UiMode mode = UiMode::Home;
        bool btNavArtistPending = false;
        String radioListStationName;
        size_t radioListCount = 0;
        size_t radioListPosition = 0;
        bool radioListPlaying = false;
    };

    St7789Pins _pins;
    Adafruit_ST7789 _tft;

    volatile bool _initialized = false;
    bool _visualDisabled = false;
    bool _layoutDrawn = false;
    bool _startupActive = false;
    bool _runtimeRequestSent = false;
    bool _runtimeActive = false;
    bool _stackReported = false;
    volatile bool _clearResult = false;
    TaskHandle_t _displayTask = nullptr;
    SemaphoreHandle_t _inputMutex = nullptr;
    SemaphoreHandle_t _initDone = nullptr;
    SemaphoreHandle_t _clearDone = nullptr;
    DisplayInput _pendingInput;
    DisplayInput _activeInput;
    uint32_t _pendingGeneration = 1;
    uint32_t _appliedGeneration = 0;
    UiMode _renderedMode = UiMode::Home; // Presentation cache; App owns UiMode.

    int _lastVolume = -1;
    bool _lastBtConnected = false;
    bool _lastBtPlaying = false;
    bool _lastBtReconnectGrace = false;
    PlaybackState _lastPlayback = PlaybackState::Stop;
    uint32_t _lastRadioBitrate = 0;
    String _lastRadioCodec;
    bool _lastPlayMediaActive = false;
    String _lastBtPeer;
    String _lastBtTitle;
    String _lastBtArtist;
    String _lastBtNavArtist;
    bool _lastBtNavArtistPending = false;
    String _radioListStationName;
    size_t _radioListCount = 0;
    size_t _radioListPosition = 0;
    bool _radioListPlaying = false;

    int _lastWifiLevel = -1;
    bool _lastWifiConnected = false;
    bool _lastApMode = false;
    uint32_t _lastWifiDrawMs = 0;

    bool _lastTimeValid = false;
    String _lastClockText;

    struct ScrollLine {
        String text;
        int textWidth = 0;
        int viewportWidth = 0;
        int offset = 0;
        uint32_t holdStartMs = 0;
        uint32_t lastStepMs = 0;
        bool needsScroll = false;
    };
    ScrollLine _scrollLines[4];
    std::unique_ptr<ReusableCanvas16> _scrollCanvas;
    int8_t _activeScrollRow = -1;
    uint8_t _nextScrollRow = 0;
    AudioSource _lastScrollSource = AudioSource::Stop;
    bool _scrollStopStyle = true;

    void resetScrolls();
    void setScrollLine(uint8_t row, const String& text, bool force);
    void updateScroll(bool radioList);
    void renderScrollLine(uint8_t row);
    void drawScrollText(ReusableCanvas16& canvas, const String& text, int x,
                        int baselineY, int viewportWidth, bool artistFont,
                        uint8_t scale, uint16_t color);


    static void displayTaskEntry(void* context);
    void displayTaskLoop();
    void initializeDisplay();
    void finishStartupOnTask();
    bool copyPendingInput();
    void renderFrame();
    void showStartupScreen();
    void drawStaticLayout();
    void updatePlayerFields(bool force = false);

    void drawHeader(
        bool btConnected,
        bool reconnectGrace,
        const String& peerName
    );

    void drawMetadata(
        bool btConnected,
        bool reconnectGrace,
        const String& artist,
        const String& title
    );

    void drawSourceInfo(const DeviceState& state);

    void drawWifiIndicator(
        bool wifiConnected,
        int wifiRssi,
        bool apMode
    );

    void drawClock(
        bool valid,
        const String& clockText
    );

    void drawVolumeIndicator(int volume);

    void showVolumeScreen(
        int volume,
        const String& ip
    );

    void drawVolumeValue(int volume);
    void drawVolumeIp(const String& ip);
    void drawBtTrackNavScreen();
    void drawBtNavArtist(const String& artist);
    void drawRadioListScreen();
    void drawRadioListFooter();

    static int wifiLevel(int rssi);

    static int glyphAdvance(uint32_t codepoint, bool artistFont, uint8_t scale);
    static int textWidth(const String& value, bool artistFont, uint8_t scale);
    void drawStartupCentered(const String& value, int y, uint8_t scale,
                             uint16_t color, uint16_t background);
    void drawUtf8Line(
        const String& value, int x, int y, int maxWidth,
        uint16_t color, uint16_t background, bool artistFont, uint8_t scale
    );
};
