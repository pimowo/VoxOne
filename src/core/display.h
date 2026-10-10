#ifndef display_h
#define display_h
#include <stdint.h>
#include <stdio.h>
#include "common.h"
#include "ui_state.h"
#include "update_display_view.h"
#include "../displays/display_profile.h"
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
#include "st7796_player_scroll.h"
#endif

enum class DisplaySourceKind : uint8_t { Radio, Bluetooth, Dlna, Aux, Spdif, Tts };
inline const char* displaySourceLabel(DisplaySourceKind kind) {
    switch (kind) {
        case DisplaySourceKind::Radio: return "WEB";
        case DisplaySourceKind::Bluetooth: return "BT";
        case DisplaySourceKind::Dlna: return "DLNA";
        case DisplaySourceKind::Aux: return "AUX";
        case DisplaySourceKind::Spdif: return "SPDIF";
        case DisplaySourceKind::Tts: return "TTS";
    }
    return "";
}
inline bool displayVolumeMuted(uint8_t /*userVolume*/, bool muted = false) {
    return muted;
}
inline const char* displaySt7789VolumeText(uint8_t userVolume, bool muted,
                                         char* buffer, size_t bufferSize) {
    if (displayVolumeMuted(userVolume, muted)) return "MUTE";
    snprintf(buffer, bufferSize, "\023 %u", static_cast<unsigned>(userVolume));
    return buffer;
}
inline const char* displayLoudLabel(bool active) {
    return active ? "LOUD" : "";
}
enum class DisplayDlnaMode : uint8_t { Unavailable, All, Random, One };
inline const char* displayDlnaModeLabel(DisplayDlnaMode mode) {
    switch (mode) {
        case DisplayDlnaMode::All: return "ALL";
        case DisplayDlnaMode::Random: return "RND";
        case DisplayDlnaMode::One: return "ONE";
        case DisplayDlnaMode::Unavailable: return "";
    }
    return "";
}
inline bool displaySlotVisible(const char* label) {
    return label && label[0] != '\0';
}
enum class DisplayPlaybackState : uint8_t { None, Stopped, Playing, Paused };
inline bool displayVolumeFrameRed(uint8_t userVolume, bool muted,
                                 bool logicalDacMuted) {
    return displayVolumeMuted(userVolume, muted) || logicalDacMuted;
}
inline const char* displayPlaybackLabel(DisplayPlaybackState state) {
    switch (state) {
        case DisplayPlaybackState::None: return "";
        case DisplayPlaybackState::Playing: return "PLAY";
        case DisplayPlaybackState::Paused: return "PAUZA";
        case DisplayPlaybackState::Stopped: return "STOP";
    }
    return "STOP";
}
struct DisplaySourceView {
    DisplaySourceKind kind;
    bool connected;
    DisplayPlaybackState playback;
    const char* peerName;
    const char* artist;
    const char* title;
    uint32_t sampleRate = 0;
};
inline const char* displayPlayerStationText(const DisplaySourceView& source,
                                            const char* radioStationName) {
    if (source.kind == DisplaySourceKind::Bluetooth)
        return source.connected && source.peerName && source.peerName[0]
                   ? source.peerName : "Bluetooth";
    if (source.kind == DisplaySourceKind::Radio)
        return source.playback == DisplayPlaybackState::Playing &&
                       radioStationName && radioStationName[0]
                   ? radioStationName : "WEB Radio";
    return displaySourceLabel(source.kind);
}
inline bool displaySourceVuVisible(const DisplaySourceView& source) {
    return source.playback == DisplayPlaybackState::Playing ||
           (source.kind == DisplaySourceKind::Bluetooth &&
            source.playback == DisplayPlaybackState::Paused);
}
inline bool displayVuUnlocked(bool enabled, bool sourceVisible,
                              uint8_t userVolume) {
    return enabled && sourceVisible && userVolume > 0;
}
// Optional Source Manager view. Strings must stay valid until this call returns.
extern bool getDisplaySourceView(DisplaySourceView& view) __attribute__((weak));

#if DSP_MODEL==DSP_DUMMY
#define DUMMYDISPLAY
#endif

#ifndef DUMMYDISPLAY
class ScrollWidget;
class PlayListWidget;
class BitrateWidget;
class FillWidget;
class SliderWidget;
class Pager;
class Page;
class VuWidget;
class NumWidget;
class ClockWidget;
class TextWidget;
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
class St7796VolumeWidget;
class St7796UpdateProgressWidget;
class St7796BluetoothWidget;
class St7796LabelFrameWidget;
class St7796PlaybackIconWidget;
#endif
#if (VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_OLED_128X64)
class Oled128x64UpdateProgressWidget;
#endif
    
class Display {
  public:
    Display() {};
    ~Display();
    displayMode_e mode() const { return uiState.mode(); }
    void init();
    void loop();
    void _start();
    bool ready() { return _bootStep==2; }
    void resetQueue();
    void putRequest(displayRequestType_e type, int payload=0);
    void queueModeRender(displayMode_e mode);
    void flip();
    void invert();
    bool deepsleep();
    void wakeup();
    void setContrast();
    void lock()   { _locked=true; }
    void unlock() { _locked=false; }
    uint16_t width();
    uint16_t height();
  private:
    ScrollWidget *_meta, *_title1, *_plcurrent, *_title2;
    PlayListWidget *_plwidget;
    BitrateWidget *_fullbitrate;
    FillWidget *_metabackground, *_plbackground;
    SliderWidget *_volbar;
    Pager *_pager;
    Page *_footer;
    VuWidget *_vuwidget;
    NumWidget *_nums;
    ClockWidget *_clock;
    Page *_boot;
    TextWidget *_bootstring, *_volip, *_voltxt, *_rssi, *_bitrate;
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    TextWidget *_btTransportPlayback;
    St7796PlaybackIconWidget *_st7796Playback = nullptr;
    St7796VolumeWidget *_st7796Volume;
    St7796BluetoothWidget *_st7796BluetoothIcon;
    St7796LabelFrameWidget *_st7796Source = nullptr, *_st7796Eq = nullptr;
    St7796LabelFrameWidget *_st7796Loud = nullptr, *_st7796Mode = nullptr;
    ScrollWidget *_btTransportArtist, *_btTransportTitle;
    Page *_btTransportPage;
    Page *_st7796UpdatePage = nullptr;
    TextWidget *_st7796UpdateTarget = nullptr, *_st7796UpdatePercent = nullptr;
    TextWidget *_st7796UpdateActivity = nullptr;
    St7796UpdateProgressWidget *_st7796UpdateBar = nullptr;
#endif
#if (VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_OLED_128X64)
    TextWidget *_oled128x64PlaylistPrevious = nullptr, *_oled128x64PlaylistNext = nullptr;
    Page *_oled128x64UpdatePage = nullptr;
    TextWidget *_oled128x64UpdateTarget = nullptr, *_oled128x64UpdatePercent = nullptr;
    TextWidget *_oled128x64UpdateActivity = nullptr;
    Oled128x64UpdateProgressWidget *_oled128x64UpdateBar = nullptr;
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76 || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320 || (VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_OLED_128X64)
    TextWidget *_plheader, *_plcounter, *_plplaying;
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
    ScrollWidget *_st7789Station;
    TextWidget *_st7789Rssi, *_st7789Volume, *_st7789Clock;
#endif
    bool _locked = false;
    volatile bool _volumePending = false;
    volatile bool _volumeModePending = false;
    uint32_t _lastVolumeDraw = 0;
    uint8_t _bootStep;
    displayMode_e _renderedMode = PLAYER;
    UpdateDisplayProgressState _updateDisplayProgress;
    uint32_t _updateDisplayRevision = 0;
    uint32_t _updateDisplayAcquisition = 0;
    void _updateUpdateScreen(const UpdateProgressSnapshot& snapshot);
    void _time(bool redraw = false);
    void _apScreen();
    void _renderMode(displayMode_e newmode);
    void _drawPlaylist();
    void _volume();
    void _title();
    void _station();
    void _drawNextStationNum(uint16_t num);
    void _createDspTask();
    void _showDialog(const char *title);
    void _buildPager();
    void _bootScreen();
    void _layoutChange(bool played);
    void _setVuVisibility(bool sourceVisible);
    void _setRSSI(int rssi);
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    void _updatePlaybackStatus();
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    St7796PlayerScroll _st7796Scroll;
    bool _st7796PlayerReady = false;
    void _st7796ScrollTextChanged(uint8_t row);
    void _st7796ScrollTick();
    void _st7796ScrollMode(bool playerMode);
#endif
};

#else

class Display {
  public:
    Display() {};
    displayMode_e mode() const { return uiState.mode(); }
    void init();
    void _start();
    void putRequest(displayRequestType_e type, int payload=0);
    void queueModeRender(displayMode_e mode) {}
    void loop(){}
    bool ready() { return true; }
    void resetQueue(){}
    void centerText(const char* text, uint8_t y, uint16_t fg, uint16_t bg){}
    void rightText(const char* text, uint8_t y, uint16_t fg, uint16_t bg){}
    void flip(){}
    void invert(){}
    void setContrast(){}
    bool deepsleep(){return true;}
    void wakeup(){}
    void printPLitem(uint8_t pos, const char* item){}
    void lock()   {}
    void unlock() {}
    uint16_t width(){ return 0; }
    uint16_t height(){ return 0; }
  private:
    void _createDspTask();
};

#endif

extern Display display;
uint32_t displayTaskStackHighWaterMark();


#endif
