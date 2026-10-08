#ifndef display_h
#define display_h
#include <stdint.h>
#include "common.h"
#if defined(VOXONE_PROFILE_SALON) && DSP_MODEL==DSP_ST7796
#include "salon_player_scroll.h"
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
inline bool displayVolumeMuted(uint8_t userVolume, bool muted = false) {
    return muted || userVolume == 0;
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
#if DSP_MODEL==DSP_ST7796
class SalonVolumeWidget;
class SalonBluetoothWidget;
class SalonLabelFrameWidget;
class SalonPlaybackIconWidget;
#endif
    
class Display {
  public:
    uint16_t currentPlItem;
    uint16_t numOfNextStation;
    displayMode_e _mode;
  public:
    Display() {};
    ~Display();
    displayMode_e mode() { return _mode; }
    void mode(displayMode_e m) { _mode=m; }
    void init();
    void loop();
    void _start();
    bool ready() { return _bootStep==2; }
    void resetQueue();
    void putRequest(displayRequestType_e type, int payload=0);
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
    SliderWidget *_volbar, *_heapbar;
    Pager *_pager;
    Page *_footer;
    VuWidget *_vuwidget;
    NumWidget *_nums;
    ClockWidget *_clock;
    Page *_boot;
    TextWidget *_bootstring, *_volip, *_voltxt, *_rssi, *_bitrate;
#if DSP_MODEL==DSP_ST7796
    TextWidget *_btTransportPlayback;
    SalonPlaybackIconWidget *_salonPlayback = nullptr;
    SalonVolumeWidget *_salonVolume;
    SalonBluetoothWidget *_salonBluetoothIcon;
    SalonLabelFrameWidget *_salonSource = nullptr, *_salonEq = nullptr;
    SalonLabelFrameWidget *_salonLoud = nullptr, *_salonMode = nullptr;
    ScrollWidget *_btTransportArtist, *_btTransportTitle;
    Page *_btTransportPage;
    Page *_salonUpdatePage = nullptr;
#endif
#if DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7796
    TextWidget *_plheader, *_plcounter, *_plplaying;
#endif
#if DSP_MODEL==DSP_ST7789_76
    ScrollWidget *_deskStation;
    TextWidget *_deskRssi, *_deskVolume, *_deskClock;
#endif
    bool _locked = false;
    volatile bool _volumePending = false;
    volatile bool _volumeModePending = false;
    uint32_t _lastVolumeDraw = 0;
    uint8_t _bootStep;
    void _time(bool redraw = false);
    void _apScreen();
    void _swichMode(displayMode_e newmode);
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
#if DSP_MODEL==DSP_ST7796
    void _updatePlaybackStatus();
#endif
#if defined(VOXONE_PROFILE_SALON) && DSP_MODEL==DSP_ST7796
    SalonPlayerScroll _salonScroll;
    void _salonScrollTextChanged(uint8_t row);
    void _salonScrollTick();
    void _salonScrollMode(bool playerMode);
#endif
};

#else

class Display {
  public:
    uint16_t currentPlItem;
    uint16_t numOfNextStation;
    displayMode_e _mode;
  public:
    Display() {};
    displayMode_e mode() { return _mode; }
    void mode(displayMode_e m) { _mode=m; }
    void init();
    void _start();
    void putRequest(displayRequestType_e type, int payload=0);
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
