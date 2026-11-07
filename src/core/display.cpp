#include "Arduino.h"
#include <ctype.h>
#include "options.h"
#include "WiFi.h"
#include "time.h"
#include "config.h"
#include "display.h"
#include "display_audio_info.h"
#include "first_boot_core.h"
#include "dac_mute.h"
#include "eq_preset_label.h"
#include "player.h"
#include "volume_map.h"
#include "station_metadata.h"
#include "player_display_metadata.h"
#include "player_display_view.h"
#include "network.h"
#include "netserver.h"
#include "system_operation_state.h"
#include "update_progress.h"
#include "update_bar_render_state.h"
#include "station_selection_view.h"
#include "timekeeper.h"
#include "../displays/dspcore.h"
#include "../displays/widgets/widgets.h"
#include "../displays/widgets/pages.h"
#include "../displays/tools/l10n.h"
#include "../hardware/hardware_descriptor.h"
#include <new>
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
#include "../displays/conf/player128x64conf.h"
#include "../displays/conf/player128x32conf.h"
#include "../displays/oled128x32_player.h"
#include "../displays/oled128x64_layout.h"
#include "../displays/fonts/voxone_boot_logo_oled_128x32.h"
#include "../displays/fonts/voxone_boot_logo_oled_128x64.h"
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
#include "st7789_284x76_layout.h"
#endif
#if VOXONE_DISPLAY_COMPILES_ST7796
#include "../displays/yamaha_tft_layout.h"
#endif

Display display;
static TaskHandle_t displayTaskHandle = nullptr;

#ifndef CORE_STACK_SIZE
  // ESP-IDF expresses task stack sizes and high-water marks in bytes.
  // Rendering and NetServer::loop() share this task and exceed 4 KiB under
  // combined OLED/metadata/volume/station activity.
  #define CORE_STACK_SIZE  1024*6
#endif
#ifndef DSP_TASK_PRIORITY
  #define DSP_TASK_PRIORITY  2
#endif
#ifndef DSP_TASK_CORE_ID
  #define DSP_TASK_CORE_ID  0
#endif
#ifndef DSP_TASK_DELAY
  #define DSP_TASK_DELAY pdMS_TO_TICKS(10) // cap for 50 fps
#endif

#define DSP_QUEUE_TICKS 0

#ifndef DSQ_SEND_DELAY
  //#define DSQ_SEND_DELAY portMAX_DELAY
  #define DSQ_SEND_DELAY  pdMS_TO_TICKS(200)
#endif

#ifndef DISPLAY_VOLUME_INTERVAL_MS
  #define DISPLAY_VOLUME_INTERVAL_MS 40
#endif

QueueHandle_t displayQueue;
portMUX_TYPE displayVolumeMux = portMUX_INITIALIZER_UNLOCKED;

static void loopDspTask(void * pvParameters){
  while(true){
  #ifndef DUMMYDISPLAY
    if(display.active()) {
      if(displayQueue==NULL) break;
      if(timekeeper.loop0()){
        display.loop();
      #ifndef NETSERVER_LOOP1
        netserver.loop();
      #endif
      }
    } else {
      timekeeper.loop0();
    #ifndef NETSERVER_LOOP1
      netserver.loop();
    #endif
    }
  #else
    timekeeper.loop0();
    #ifndef NETSERVER_LOOP1
      netserver.loop();
    #endif
  #endif
    vTaskDelay(DSP_TASK_DELAY);
  }
  displayTaskHandle = nullptr;
  vTaskDelete( NULL );
}

void Display::_createDspTask(){
  xTaskCreatePinnedToCore(loopDspTask, "DspTask", CORE_STACK_SIZE, NULL,
                         DSP_TASK_PRIORITY, &displayTaskHandle,
                         DSP_TASK_CORE_ID);
}

uint32_t displayTaskStackHighWaterMark() {
  return displayTaskHandle ? uxTaskGetStackHighWaterMark(displayTaskHandle) : 0;
}

#ifndef DUMMYDISPLAY
//============================================================================================================================
namespace {
using voxone::display_scroll::PlayerScrollRowMetrics;
using voxone::display_scroll::PlayerScrollWidgetBinding;
using voxone::display_scroll::ScrollRendererProfile;

PlayerScrollRowMetrics playerScrollMetrics(const void* context) {
  return static_cast<const ScrollWidget*>(context)->coreScrollMetrics();
}

void setPlayerScrollCoreControlled(void* context, bool enabled) {
  static_cast<ScrollWidget*>(context)->setCoreControlled(enabled);
}

bool renderPlayerScrollOffset(void* context, uint16_t offsetPixels) {
  return static_cast<ScrollWidget*>(context)->renderScrollOffset(offsetPixels);
}

ScrollRendererProfile activePlayerScrollProfile() {
#if defined(VOXONE_RUNTIME_L0_DISPLAY_BACKENDS)
  return dsp.activeProfile() ==
                 voxone::display_profile::ProfileId::St7796_480x320
             ? ScrollRendererProfile::St7796_480x320
             : ScrollRendererProfile::Oled128x64;
#else
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
  return ScrollRendererProfile::Oled128x64;
#elif VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  return ScrollRendererProfile::St7789_284x76;
#elif VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  return ScrollRendererProfile::St7796_480x320;
#else
  return ScrollRendererProfile::None;
#endif
#endif
}

PlayerScrollWidgetBinding playerScrollBinding(ScrollWidget* widget) {
  return PlayerScrollWidgetBinding{widget, playerScrollMetrics,
                                   setPlayerScrollCoreControlled,
                                   renderPlayerScrollOffset};
}
}  // namespace

constexpr uint16_t kDisplayVolumeMax = 100;
static const char* runtimeEqPresetLabel() {
  const auto runtime = config.runtimeSnapshot();
  return eqPresetLabel(runtime.bass, runtime.middle, runtime.treble);
}

DspCore dsp;

#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
static bool oled128x32Active() { return dsp.height() == 32; }
static bool smallOledActive() {
  return dsp.activeProfile() ==
             voxone::display_profile::ProfileId::Oled128x32 ||
         dsp.activeProfile() ==
             voxone::display_profile::ProfileId::Oled128x64;
}

static bool oled128x64Active() {
  return dsp.activeProfile() ==
         voxone::display_profile::ProfileId::Oled128x64;
}

class Oled128x64StartupLogoWidget : public Widget {
 public:
  Oled128x64StartupLogoWidget() {
    Widget::init({voxone::oled128x64::kStartupLogo.left,
                  voxone::oled128x64::kStartupLogo.top,
                  0, WA_LEFT}, WHITE, BLACK);
  }

 private:
  void _draw() override {
    if (!_active) return;
    using namespace voxone::display_boot_branding::oled;
    dsp.drawBitmap(voxone::oled128x64::kStartupLogo.left,
                   voxone::oled128x64::kStartupLogo.top,
                   kBootLogoBitmap, kBootLogoWidth, kBootLogoHeight, WHITE);
  }
};

class Oled128x32StartupLogoWidget : public Widget {
 public:
  Oled128x32StartupLogoWidget() {
    Widget::init({voxone::oled128x32::kStartupLogo.left,
                  voxone::oled128x32::kStartupLogo.top,
                  0, WA_LEFT}, WHITE, BLACK);
  }

  void loop() override { _draw(); }

 private:
  void _draw() override {
    if (!_active) return;
    using namespace voxone::display_boot_branding::oled32;
    dsp.fillRect(voxone::oled128x32::kStartupLogoBand.left,
                 voxone::oled128x32::kStartupLogoBand.top,
                 voxone::oled128x32::kStartupLogoBand.width,
                 voxone::oled128x32::kStartupLogoBand.height, BLACK);
    dsp.drawBitmap(kBootLogoLeft, kBootLogoTop, kBootLogoBitmap,
                   kBootLogoWidth, kBootLogoHeight, WHITE);
  }
};

class Oled128x32PlayerStatusWidget : public Widget {
 public:
  Oled128x32PlayerStatusWidget() {
    Widget::init({0, voxone::oled128x32::kPlayerStatus.top, 1, WA_LEFT},
                 WHITE, BLACK);
  }

  void setWifiRssi(int rssi) { rssi_ = rssi; }

  void loop() override {
    if (!_active || static_cast<uint32_t>(millis() - lastCheck_) < 100) return;
    lastCheck_ = millis();
    render(false);
  }

 private:
  bool drawn_ = false;
  uint32_t lastCheck_ = 0;
  DisplayPlaybackState playback_ = DisplayPlaybackState::None;
  DisplaySourceKind source_ = DisplaySourceKind::Radio;
  uint8_t volume_ = 255;
  bool muted_ = false;
  bool btConnected_ = false;
  bool wifiConnected_ = false;
  uint8_t wifiBars_ = 255;
  int rssi_ = -127;

  void _draw() override { if (_active) render(true); }

  void render(bool force) {
    const bool wifiConnected = network.status == CONNECTED;
    if (!wifiConnected) rssi_ = -127;
    PlayerDisplayView view{};
    capturePlayerDisplayView(view, rssi_);
    const uint8_t wifiBars = voxone::oled128x32::wifiBars(
        wifiConnected, rssi_);
    if (!force && drawn_ && playback_ == view.playback &&
        source_ == view.source &&
        volume_ == view.userVolume && muted_ == view.muted &&
        btConnected_ == view.btConnected &&
        wifiConnected_ == wifiConnected && wifiBars_ == wifiBars) return;

    dsp.fillRect(0, voxone::oled128x32::kPlayerStatus.top,
                 voxone::oled128x32::kPlayerStatus.width,
                 voxone::oled128x32::kPlayerStatus.height, BLACK);

    switch (voxone::oled128x32::playbackIcon(
        view.playback == DisplayPlaybackState::Playing,
        view.playback == DisplayPlaybackState::Paused,
        view.playback == DisplayPlaybackState::Stopped)) {
      case voxone::oled128x32::PlaybackIcon::Play:
        dsp.fillTriangle(voxone::oled128x32::kPlayerPlayLeft,
                         voxone::oled128x32::kPlayerPlayTop,
                         voxone::oled128x32::kPlayerPlayLeft,
                         voxone::oled128x32::kPlayerPlayBottom,
                         voxone::oled128x32::kPlayerPlayRight,
                         voxone::oled128x32::kPlayerPlayTipY, WHITE);
        break;
      case voxone::oled128x32::PlaybackIcon::Pause:
        dsp.fillRect(1, 24, 2, 8, WHITE);
        dsp.fillRect(5, 24, 2, 8, WHITE);
        break;
      case voxone::oled128x32::PlaybackIcon::Stop: {
        const auto stop = voxone::oled128x32::kPlayerStop;
        dsp.fillRect(stop.left, stop.top, stop.width, stop.height, WHITE);
        break;
      }
      case voxone::oled128x32::PlaybackIcon::None:
        break;
    }

    char source[9];
    voxone::oled128x32::formatSource(
        source, sizeof(source), displaySourceLabel(view.source));
    dsp.setFont();
    dsp.setTextColor(WHITE, BLACK);
    dsp.setTextSize(1);
    dsp.setCursor(voxone::oled128x32::centeredTextLeft(
                      voxone::oled128x32::kPlayerSource, strlen(source)),
                  voxone::oled128x32::kPlayerSource.top);
    dsp.print(source);

    char level[5];
    voxone::oled128x32::formatVolume(level, sizeof(level), view.userVolume,
                                     view.muted);
    const uint8_t shownVolume = voxone::oled128x32::displayVolume(
        view.userVolume);
    const uint16_t volumeLeft = voxone::oled128x32::playerVolumeTextLeft(
        shownVolume, view.muted);
    const uint16_t top = voxone::oled128x32::kPlayerStatus.top;
    const uint16_t speakerLeft = voxone::oled128x32::kPlayerSpeaker.left;
    dsp.fillRect(speakerLeft, top + 2, 2, 4, WHITE);
    dsp.fillTriangle(speakerLeft + 1, top + 2, speakerLeft + 6, top,
                     speakerLeft + 6, top + 7, WHITE);
    dsp.fillTriangle(speakerLeft + 1, top + 2, speakerLeft + 1, top + 5,
                     speakerLeft + 6, top + 7, WHITE);
    if (view.muted) {
      dsp.drawLine(speakerLeft + 4, top + 1, speakerLeft + 6, top + 6,
                   BLACK);
      dsp.drawLine(speakerLeft + 6, top + 1, speakerLeft + 4, top + 6,
                   BLACK);
    }
    dsp.setCursor(volumeLeft, top);
    dsp.print(level);

    if (voxone::oled128x32::bluetoothVisible(view.btConnected)) {
      const uint16_t volumeRight = volumeLeft +
          voxone::oled128x32::playerVolumeTextWidth(shownVolume, view.muted);
      const uint16_t btLeft =
          voxone::oled128x32::playerBluetoothTextLeft(volumeRight);
      dsp.setCursor(btLeft, voxone::oled128x32::kPlayerBluetoothBand.top);
      dsp.print("BT");
    }

    if (wifiConnected) {
      for (uint8_t i = 0; i < wifiBars; ++i) {
        const auto bar = voxone::oled128x32::playerWifiBar(i);
        dsp.fillRect(bar.left, bar.top, bar.width, bar.height, WHITE);
      }
    } else {
      const auto disconnected =
          voxone::oled128x32::kPlayerWifiDisconnected;
      const uint16_t right = disconnected.left + disconnected.width - 1;
      const uint16_t bottom = disconnected.top + disconnected.height - 1;
      dsp.drawLine(disconnected.left, disconnected.top, right, bottom, WHITE);
      dsp.drawLine(right, disconnected.top, disconnected.left, bottom, WHITE);
    }

    drawn_ = true;
    playback_ = view.playback;
    source_ = view.source;
    volume_ = view.userVolume;
    muted_ = view.muted;
    btConnected_ = view.btConnected;
    wifiConnected_ = wifiConnected;
    wifiBars_ = wifiBars;
  }
};

static Oled128x32PlayerStatusWidget* oled128x32PlayerStatus = nullptr;

class Oled128x32BtTransportWidget : public Widget {
 public:
  Oled128x32BtTransportWidget() {
    Widget::init({0, voxone::oled128x32::kBtTransportControls.top, 1, WA_LEFT},
                 WHITE, BLACK);
  }

  void setPlayback(DisplayPlaybackState playback) {
    if (playback_ == playback) return;
    playback_ = playback;
    if (_active) render();
  }

 private:
  DisplayPlaybackState playback_ = DisplayPlaybackState::None;

  void _draw() override { if (_active) render(); }

  void render() {
    using namespace voxone::oled128x32;
    dsp.fillRect(kBtTransportControls.left, kBtTransportControls.top,
                 kBtTransportControls.width, kBtTransportControls.height,
                 BLACK);
    dsp.setFont();
    dsp.setTextSize(1);
    dsp.setTextColor(WHITE, BLACK);
    dsp.setCursor(kBtTransportPrevious.left, kBtTransportPrevious.top);
    dsp.print("<");
    const PlaybackIcon icon = playbackIcon(
        playback_ == DisplayPlaybackState::Playing,
        playback_ == DisplayPlaybackState::Paused,
        playback_ == DisplayPlaybackState::Stopped);
    const char* playback = btTransportPlaybackLabel(icon);
    dsp.setCursor(centeredTextLeft(kBtTransportPlayback, strlen(playback)),
                  kBtTransportPlayback.top);
    dsp.print(playback);
    dsp.setCursor(kBtTransportNext.left, kBtTransportNext.top);
    dsp.print(">");
    dsp.setTextColor(WHITE, BLACK);
  }
};

class Oled128x64PlayerStatusWidget : public Widget {
 public:
  Oled128x64PlayerStatusWidget() { Widget::init({0, 30, 1, WA_LEFT}, WHITE, BLACK); }

  void loop() override {
    if (!_active || static_cast<uint32_t>(millis() - lastCheck_) < 100) return;
    lastCheck_ = millis();
    render(false);
  }

 private:
  bool drawn_ = false;
  uint32_t lastCheck_ = 0;
  uint8_t hour_ = 255, minute_ = 255, volume_ = 255;
  bool colonVisible_ = false;
  uint16_t bitrate_ = 0;
  uint32_t sampleRate_ = 0;
  BitrateFormat format_ = BF_UNKNOWN;
  DisplaySourceKind source_ = DisplaySourceKind::Radio;
  DisplayPlaybackState playback_ = DisplayPlaybackState::None;
  bool muted_ = false, btConnected_ = false;

  void _draw() override { if (_active) render(true); }

  void render(bool force) {
    PlayerDisplayView view{};
    capturePlayerDisplayView(view, WiFi.RSSI());
    const uint8_t hour = network.timeinfo.tm_hour;
    const uint8_t minute = network.timeinfo.tm_min;
    const uint8_t volume = view.userVolume;
    const bool muted = view.muted;
    const bool connected = view.btConnected;
    const DisplayAudioInfo& audioInfo = view.audioInfo;
    const BitrateFormat format = audioInfo.radioFormat;
    const uint16_t bitrate = audioInfo.radioBitrate;
    const bool colonVisible = (millis() / 500) % 2 == 0;
    const bool middleChanged = force || !drawn_ || hour != hour_ ||
                               minute != minute_ || format != format_ ||
                               bitrate != bitrate_ || view.sampleRate != sampleRate_ ||
                               view.source != source_;

    if (middleChanged) {
      dsp.fillRect(0, 30, 128, 24, BLACK);
      dsp.setFont();
      dsp.setTextColor(WHITE, BLACK);
      dsp.setTextSize(2);
      char digits[3];
      snprintf(digits, sizeof(digits), "%02u", static_cast<unsigned>(hour));
      dsp.setCursor(34, player128x64::clockDigitTop);
      dsp.print(digits);
      snprintf(digits, sizeof(digits), "%02u", static_cast<unsigned>(minute));
      dsp.setCursor(70, player128x64::clockDigitTop);
      dsp.print(digits);
      dsp.setTextSize(1);
      const char* codec = view.audioCodecLabel;
      dsp.setCursor(128 - strlen(codec) * 6, 32);
      dsp.print(codec);
      if (audioInfo.bluetooth) {
        dsp.setCursor(128 - strlen(audioInfo.bottom) * 6, 42);
        dsp.print(audioInfo.bottom);
      } else if (bitrate) {
        char rateText[6];
        snprintf(rateText, sizeof(rateText), "%u",
                 static_cast<unsigned>(bitrate));
        dsp.setCursor(128 - strlen(rateText) * 6, 42);
        dsp.print(rateText);
      }
    }
    if (middleChanged || colonVisible != colonVisible_) {
      dsp.fillRect(58, player128x64::clockDigitTop, 12, 16, BLACK);
      if (colonVisible) {
        dsp.setFont();
        // Transparent background avoids the +1 px shift erasing MM at x=70.
        dsp.setTextColor(WHITE);
        dsp.setTextSize(2);
        dsp.setCursor(player128x64::clockColonLeft,
                      player128x64::clockColonTop);
        dsp.print(':');
      }
    }

    if (force || !drawn_ || source_ != view.source || playback_ != view.playback ||
        volume_ != volume || muted_ != muted || btConnected_ != connected) {
      // x=116..127 belongs to the existing graphical Wi-Fi widget.
      dsp.fillRect(0, 56, 116, 8, BLACK);
      namespace oled64 = voxone::oled128x64;
      switch (oled64::playbackIcon(
          view.playback == DisplayPlaybackState::Playing,
          view.playback == DisplayPlaybackState::Paused,
          view.playback == DisplayPlaybackState::Stopped)) {
        case oled64::PlaybackIcon::Play:
          dsp.fillTriangle(0, 56, 0, 63, 7, 59, WHITE);
          break;
        case oled64::PlaybackIcon::Pause:
          dsp.fillRect(1, 56, 2, 8, WHITE);
          dsp.fillRect(5, 56, 2, 8, WHITE);
          break;
        case oled64::PlaybackIcon::Stop:
          dsp.fillRect(1, 57, 6, 6, WHITE);
          break;
        case oled64::PlaybackIcon::None:
          break;
      }
      dsp.setFont();
      dsp.setTextSize(1);
      dsp.setTextColor(WHITE, BLACK);
      char source[9];
      oled64::formatSource(source, sizeof(source),
                           displaySourceLabel(view.source));
      dsp.setCursor(oled64::centeredTextLeft(oled64::kSource,
                                             strlen(source)),
                    oled64::kSource.top);
      dsp.print(source);

      const uint16_t speakerLeft = oled64::kSpeaker.left;
      const uint16_t top = oled64::kSpeaker.top;
      // Keep the exact body/cone geometry used by OLED 128x32.
      dsp.fillRect(speakerLeft, top + 2, 2, 4, WHITE);
      dsp.fillTriangle(speakerLeft + 1, top + 2, speakerLeft + 6, top,
                       speakerLeft + 6, top + 7, WHITE);
      dsp.fillTriangle(speakerLeft + 1, top + 2, speakerLeft + 1, top + 5,
                       speakerLeft + 6, top + 7, WHITE);
      if (muted) {
        dsp.drawLine(speakerLeft + 4, top + 1, speakerLeft + 6, top + 6,
                     BLACK);
        dsp.drawLine(speakerLeft + 6, top + 1, speakerLeft + 4, top + 6,
                     BLACK);
      }

      char level[4];
      oled64::formatStatusVolume(level, sizeof(level), volume, muted);
      const uint16_t volumeLeft = oled64::centeredTextLeft(
          oled64::kVolume, strlen(level));
      dsp.setTextColor(WHITE, BLACK);
      dsp.setCursor(volumeLeft, oled64::kVolume.top);
      dsp.print(level);
      if (oled64::bluetoothVisible(connected)) {
        const uint16_t volumeRight =
            volumeLeft + oled64::textWidth(strlen(level));
        const uint16_t btLeft = oled64::bluetoothTextLeft(volumeRight);
        dsp.setTextColor(WHITE, BLACK);
        dsp.setCursor(btLeft, oled64::kBluetoothBand.top);
        dsp.print("BT");
      }
    }

    drawn_ = true;
    hour_ = hour;
    minute_ = minute;
    volume_ = volume;
    muted_ = muted;
    source_ = view.source;
    playback_ = view.playback;
    btConnected_ = connected;
    format_ = format;
    bitrate_ = bitrate;
    sampleRate_ = view.sampleRate;
    colonVisible_ = colonVisible;
  }
};

class Oled128x64UpdateProgressWidget : public Widget {
 public:
  Oled128x64UpdateProgressWidget()
      : bar_(update128x64::barInteriorWidth,
             update128x64::indeterminateWidth,
             update128x64::indeterminateStep) {
    Widget::init({update128x64::barLeft, update128x64::barTop, 1, WA_LEFT},
                 WHITE, BLACK);
  }

  void setProgress(UpdateDisplayProgress progress, uint32_t acquisition) {
    if (acquisition != acquisition_) {
      acquisition_ = acquisition;
      bar_.reset();
    } else if (progress.determinate == progress_.determinate &&
               progress.percent == progress_.percent) return;
    progress_ = progress;
    if (_active) render(bar_.apply(progress_));
  }

  void loop() override {
    if (!_active || progress_.determinate) return;
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - lastStepMs_) < 100) return;
    lastStepMs_ = now;
    render(bar_.step());
  }

 private:
  UpdateDisplayProgress progress_{};
  UpdateBarRenderState bar_;
  uint32_t acquisition_ = 0;
  uint32_t lastStepMs_ = 0;

  void _draw() override {
    if (!_active) return;
    bar_.reset();
    render(bar_.apply(progress_));
  }

  void render(const UpdateBarRenderDelta& delta) {
    const uint16_t innerLeft = update128x64::barLeft + update128x64::barInset;
    const uint16_t innerTop = update128x64::barTop + update128x64::barInset;
    if (delta.reset) {
      dsp.fillRect(update128x64::barLeft, update128x64::barTop,
                   update128x64::barWidth, update128x64::barHeight, BLACK);
      dsp.drawRect(update128x64::barLeft, update128x64::barTop,
                   update128x64::barWidth, update128x64::barHeight, WHITE);
    }
    if (delta.clearWidth)
      dsp.fillRect(innerLeft + delta.clearX, innerTop, delta.clearWidth,
                   update128x64::barInteriorHeight, BLACK);
    if (delta.fillWidth)
      dsp.fillRect(innerLeft + delta.fillX, innerTop, delta.fillWidth,
                   update128x64::barInteriorHeight, WHITE);
  }
};
#else
static bool oled128x32Active() { return false; }
static bool smallOledActive() { return false; }
#endif

#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
class St7789PlayerStatusWidget : public Widget {
 public:
  St7789PlayerStatusWidget() {
    Widget::init({0, st7789_284x76::statusTop, 1, WA_LEFT},
                 config.theme.meta, config.theme.background);
  }

  void loop() override {
    if (!_active || static_cast<uint32_t>(millis() - lastCheck_) < 100) return;
    lastCheck_ = millis();
    render(false);
  }

 private:
  static constexpr uint16_t kMuteColor = 0xF800;
  bool drawn_ = false;
  uint32_t lastCheck_ = 0;
  DisplaySourceKind source_ = DisplaySourceKind::Radio;
  DisplayPlaybackState playback_ = DisplayPlaybackState::None;
  uint8_t volume_ = 255;
  uint8_t wifiLevel_ = 255;
  bool muted_ = false;
  bool btConnected_ = false;
  char audioText_[20] = {};

  void _draw() override { if (_active) render(true); }

  static void drawCenteredText(const st7789_284x76::Rect& slot,
                               const char* text, uint16_t color) {
    dsp.setFont();
    dsp.setTextSize(1);
    dsp.setTextColor(color, config.theme.background);
    dsp.setCursor(st7789_284x76::centeredTextLeft(slot, strlen(text)),
                  slot.top + 2);
    dsp.print(text);
  }

  static void drawPlayback(DisplayPlaybackState state) {
    const auto& slot = st7789_284x76::playback;
    const int16_t centerX = slot.left + slot.width / 2;
    const int16_t centerY = slot.top + slot.height / 2;
    switch (state) {
      case DisplayPlaybackState::Playing:
        dsp.fillTriangle(slot.left + 1, slot.top + 1, slot.left + 1,
                         slot.top + slot.height - 2, slot.left + slot.width - 1,
                         centerY, config.theme.meta);
        break;
      case DisplayPlaybackState::Paused:
        dsp.fillRect(centerX - 4, slot.top + 1, 3, slot.height - 2,
                     config.theme.meta);
        dsp.fillRect(centerX + 1, slot.top + 1, 3, slot.height - 2,
                     config.theme.meta);
        break;
      case DisplayPlaybackState::Stopped:
        dsp.fillRect(centerX - 3, centerY - 3, 7, 7, config.theme.meta);
        break;
      case DisplayPlaybackState::None:
        break;
    }
  }

  static void drawBluetooth() {
    const auto& slot = st7789_284x76::bluetooth;
    const int16_t x = slot.left + 3;
    const int16_t y = slot.top;
    const uint16_t color = config.theme.meta;
    dsp.drawLine(x + 3, y, x + 3, y + 11, color);
    dsp.drawLine(x + 3, y, x + 7, y + 3, color);
    dsp.drawLine(x + 7, y + 3, x, y + 9, color);
    dsp.drawLine(x, y + 2, x + 7, y + 8, color);
    dsp.drawLine(x + 7, y + 8, x + 3, y + 11, color);
  }

  static void drawWifi(uint8_t level) {
    const auto& slot = st7789_284x76::wifi;
    constexpr uint8_t heights[4] = {3, 5, 8, 11};
    constexpr uint8_t barWidth = 5;
    constexpr uint8_t gap = 3;
    const uint16_t bottom = slot.top + slot.height - 1;
    for (uint8_t i = 0; i < 4; ++i) {
      const uint16_t color = i < level ? config.theme.rssi : config.theme.div;
      dsp.fillRect(slot.left + 1 + i * (barWidth + gap),
                   bottom - heights[i] + 1, barWidth, heights[i], color);
    }
  }

  void render(bool force) {
    PlayerDisplayView view{};
    capturePlayerDisplayView(view, WiFi.RSSI());
    if (!force && drawn_ && source_ == view.source && playback_ == view.playback &&
        volume_ == view.userVolume && muted_ == view.muted &&
        btConnected_ == view.btConnected && wifiLevel_ == view.wifiLevel &&
        strcmp(audioText_, view.audioText) == 0) return;

    dsp.fillRect(0, st7789_284x76::statusTop, st7789_284x76::width,
                 st7789_284x76::statusHeight, config.theme.background);
    drawPlayback(view.playback);

    char sourceText[10];
    snprintf(sourceText, sizeof(sourceText), "[%s]", displaySourceLabel(view.source));
    drawCenteredText(st7789_284x76::source, sourceText, config.theme.meta);

    char volumeBuffer[12];
    const char* volumeText = displaySt7789VolumeText(
        view.userVolume, view.muted, volumeBuffer, sizeof(volumeBuffer));
    drawCenteredText(st7789_284x76::volume, volumeText,
                     view.muted ? kMuteColor : config.theme.vol);
    drawCenteredText(st7789_284x76::audio, view.audioText, config.theme.bitrate);
    if (view.btConnected) drawBluetooth();
    drawWifi(view.wifiLevel);

    drawn_ = true;
    source_ = view.source;
    playback_ = view.playback;
    volume_ = view.userVolume;
    muted_ = view.muted;
    btConnected_ = view.btConnected;
    wifiLevel_ = view.wifiLevel;
    strlcpy(audioText_, view.audioText, sizeof(audioText_));
  }
};

class St7789UpdateProgressWidget : public Widget {
 public:
  St7789UpdateProgressWidget()
      : bar_(st7789_284x76::updateBarInteriorWidth,
             st7789_284x76::updateIndeterminateWidth,
             st7789_284x76::updateIndeterminateStep) {
    Widget::init({st7789_284x76::updateBarLeft,
                  st7789_284x76::updateBarTop, 1, WA_LEFT},
                 config.theme.volbarin, config.theme.background);
  }

  void setProgress(UpdateDisplayProgress progress, uint32_t acquisition) {
    if (acquisition != acquisition_) {
      acquisition_ = acquisition;
      bar_.reset();
    } else if (progress.determinate == progress_.determinate &&
               progress.percent == progress_.percent) return;
    progress_ = progress;
    if (_active) render(bar_.apply(progress_));
  }

  void loop() override {
    if (!_active || progress_.determinate) return;
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - lastStepMs_) < 100) return;
    lastStepMs_ = now;
    render(bar_.step());
  }

 private:
  UpdateDisplayProgress progress_{};
  UpdateBarRenderState bar_;
  uint32_t acquisition_ = 0;
  uint32_t lastStepMs_ = 0;

  void _draw() override {
    if (!_active) return;
    bar_.reset();
    render(bar_.apply(progress_));
  }

  void render(const UpdateBarRenderDelta& delta) {
    const uint16_t innerLeft = st7789_284x76::updateBarLeft +
                               st7789_284x76::updateBarInset;
    const uint16_t innerTop = st7789_284x76::updateBarTop +
                              st7789_284x76::updateBarInset;
    if (delta.reset) {
      dsp.fillRect(st7789_284x76::updateBarLeft, st7789_284x76::updateBarTop,
                   st7789_284x76::updateBarWidth,
                   st7789_284x76::updateBarHeight, config.theme.background);
      dsp.drawRect(st7789_284x76::updateBarLeft, st7789_284x76::updateBarTop,
                   st7789_284x76::updateBarWidth,
                   st7789_284x76::updateBarHeight, config.theme.div);
    }
    if (delta.clearWidth)
      dsp.fillRect(innerLeft + delta.clearX, innerTop, delta.clearWidth,
                   st7789_284x76::updateBarInteriorHeight,
                   config.theme.background);
    if (delta.fillWidth)
      dsp.fillRect(innerLeft + delta.fillX, innerTop, delta.fillWidth,
                   st7789_284x76::updateBarInteriorHeight,
                   config.theme.volbarin);
  }
};
#endif

#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
class St7796UpdateProgressWidget : public Widget {
 public:
  St7796UpdateProgressWidget() {
    Widget::init({60, 190, 1, WA_LEFT}, 0xFFFF, 0x0000);
  }

  void setProgress(UpdateDisplayProgress progress, uint32_t acquisition) {
    if (acquisition != acquisition_) {
      acquisition_ = acquisition;
      bar_.reset();
    } else if (progress.determinate == progress_.determinate &&
               progress.percent == progress_.percent) return;
    progress_ = progress;
    if (_active) render(bar_.apply(progress_));
  }

  void loop() override {
    if (!_active || progress_.determinate) return;
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - lastStepMs_) < 100) return;
    lastStepMs_ = now;
    render(bar_.step());
  }

 private:
  UpdateDisplayProgress progress_{};
  UpdateBarRenderState bar_{};
  uint32_t acquisition_ = 0;
  uint32_t lastStepMs_ = 0;

  void _draw() override {
    if (!_active) return;
    bar_.reset();
    render(bar_.apply(progress_));
  }

  void render(const UpdateBarRenderDelta& delta) {
    if (delta.reset) {
      dsp.fillRect(60, 190, 360, 22, 0x0000);
      dsp.drawRect(60, 190, 360, 22, 0xFFFF);
    }
    if (delta.clearWidth)
      dsp.fillRect(62 + delta.clearX, 192, delta.clearWidth, 18, 0x0000);
    if (delta.fillWidth)
      dsp.fillRect(62 + delta.fillX, 192, delta.fillWidth, 18, 0x07FF);
  }
};

class St7796VolumeWidget : public Widget {
 public:
  St7796VolumeWidget(WidgetConfig position, uint16_t color, uint16_t background) {
    Widget::init(position, color, background);
  }

  void setVolume(uint8_t value, bool muted) {
    if (_value == value && _muted == muted) return;
    _value = value;
    _muted = muted;
    if (_active) _draw();
  }

  void setDacMuted(bool muted) {
    if (_dacMuted == muted) return;
    _dacMuted = muted;
    if (_active) _draw();
  }

 private:
  static constexpr uint16_t kWidth = 68;
  static constexpr uint16_t kHeight = 24;
  static constexpr uint16_t kMuteColor = 0xF800;
  uint8_t _value = 0;
  bool _muted = false;
  bool _dacMuted = true;

  void _draw() override {
    if (!_active) return;
    _clear();
    const bool muted = displayVolumeMuted(_value, _muted);
    const uint16_t color = muted ? kMuteColor : _fgcolor;
    const uint16_t frameColor =
        displayVolumeFrameRed(_value, _muted, _dacMuted) ? kMuteColor : _fgcolor;
    dsp.drawRoundRect(_config.left, _config.top, kWidth, kHeight, 3, frameColor);
    dsp.setFont();
    dsp.setTextSize(2);
    dsp.setTextColor(color, _bgcolor);
    if (muted) {
      dsp.setCursor(_config.left + (kWidth - 4 * 12) / 2, _config.top + 4);
      dsp.print("MUTE");
      return;
    }
    const uint8_t digits = _value >= 100 ? 3 : (_value >= 10 ? 2 : 1);
    const uint16_t groupWidth = 12 + 8 + digits * 12;
    const uint16_t left = _config.left + (kWidth - groupWidth) / 2;
    dsp.setCursor(left, _config.top + 4);
    dsp.print("\023");  // Existing yoFont speaker glyph.
    dsp.setCursor(left + 20, _config.top + 4);
    dsp.print(_value);
  }

  void _clear() override {
    dsp.fillRect(_config.left, _config.top, kWidth, kHeight, _bgcolor);
  }
};

class St7796LabelFrameWidget : public Widget {
 public:
  St7796LabelFrameWidget(FillConfig frame, uint16_t color, uint16_t background,
                        bool rounded = false)
      : width_(frame.width), height_(frame.height), rounded_(rounded) {
    Widget::init(frame.widget, color, background);
  }

  void setLabel(const char* label) {
    if (strcmp(label_, label) == 0) return;
    label_ = label;
    if (_active) _draw();
  }

 private:
  uint16_t width_, height_;
  bool rounded_;
  const char* label_ = "";

  void _draw() override {
    if (!_active) return;
    _clear();
    if (!displaySlotVisible(label_)) return;
    if (rounded_)
      dsp.drawRoundRect(_config.left, _config.top, width_, height_, 3, _fgcolor);
    else
      dsp.drawRect(_config.left, _config.top, width_, height_, _fgcolor);
    dsp.setFont();
    dsp.setTextSize(2);
    dsp.setTextColor(_fgcolor, _bgcolor);
    const uint16_t textWidth = strlen(label_) * 12;
    dsp.setCursor(_config.left + (width_ - textWidth) / 2,
                  _config.top + (height_ - 16) / 2);
    dsp.print(label_);
  }

  void _clear() override {
    dsp.fillRect(_config.left, _config.top, width_, height_, _bgcolor);
  }
};

class St7796PlaybackIconWidget : public Widget {
 public:
  St7796PlaybackIconWidget(FillConfig frame, uint16_t color, uint16_t background)
      : width_(frame.width), height_(frame.height) {
    Widget::init(frame.widget, color, background);
  }

  void setState(DisplayPlaybackState state) {
    if (state_ == state) return;
    state_ = state;
    if (_active) _draw();
  }

 private:
  uint16_t width_, height_;
  DisplayPlaybackState state_ = DisplayPlaybackState::None;

  void _draw() override {
    if (!_active) return;
    _clear();
    if (state_ == DisplayPlaybackState::None) return;
    dsp.drawRoundRect(_config.left, _config.top, width_, height_, 3, _fgcolor);
    const int16_t centerX = _config.left + width_ / 2;
    const int16_t centerY = _config.top + height_ / 2;
    switch (state_) {
      case DisplayPlaybackState::Playing:
        dsp.fillTriangle(centerX - 5, centerY - 7, centerX - 5, centerY + 7,
                         centerX + 7, centerY, _fgcolor);
        break;
      case DisplayPlaybackState::Paused:
        dsp.fillRect(centerX - 7, centerY - 7, 5, 14, _fgcolor);
        dsp.fillRect(centerX + 2, centerY - 7, 5, 14, _fgcolor);
        break;
      case DisplayPlaybackState::Stopped:
        dsp.fillRect(centerX - 6, centerY - 6, 12, 12, _fgcolor);
        break;
      case DisplayPlaybackState::None:
        break;
    }
  }

  void _clear() override {
    dsp.fillRect(_config.left, _config.top, width_, height_, _bgcolor);
  }
};

class St7796BluetoothWidget : public Widget {
 public:
  St7796BluetoothWidget(WidgetConfig position, uint16_t color, uint16_t background) {
    Widget::init(position, color, background);
  }

  void setConnected(bool connected) {
    if (connected_ == connected) return;
    connected_ = connected;
    if (_active) {
      _clear();
      _draw();
    }
  }

 private:
  bool connected_ = false;

  void _draw() override {
    if (!_active || !connected_) return;
    const int16_t x = _config.left;
    const int16_t y = _config.top;
    dsp.drawLine(x + 9, y, x + 9, y + 17, _fgcolor);
    dsp.drawLine(x + 9, y, x + 15, y + 5, _fgcolor);
    dsp.drawLine(x + 15, y + 5, x + 3, y + 14, _fgcolor);
    dsp.drawLine(x + 3, y + 4, x + 15, y + 13, _fgcolor);
    dsp.drawLine(x + 15, y + 13, x + 9, y + 17, _fgcolor);
  }

  void _clear() override {
    dsp.fillRect(_config.left, _config.top, 18, 18, _bgcolor);
  }
};
#endif

Page playerPage(false);
Page dialogPage(false);
Page playlistPage(false);
Page screensaverPage(false);
Page *pages[] = { &playerPage, &dialogPage, &playlistPage, &screensaverPage };

#if !(DSP_MODEL==DSP_ST7789 || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320)
  #undef  BITRATE_FULL
  #define BITRATE_FULL     false
#endif


void returnPlayer(){
  transitionUiMode(PLAYER);
}

Display::~Display() {
  delete _pager;
  delete _footer;
  delete _plwidget;
  delete _nums;
  delete _clock;
  delete _meta;
  delete _title1;
  delete _title2;
  delete _plcurrent;
}

void Display::init() {
  Serial.print("##[BOOT]#\tdisplay.init\t");
  if (!_runtimeEnabled) {
    _bootStep = 2;
    _createDspTask();
    Serial.println("disabled (runtime NONE)");
    return;
  }
#if LIGHT_SENSOR!=255
  analogSetAttenuation(ADC_0db);
#endif
  _bootStep = 0;
  _playerScrollReady = false;
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  _st7796PlayerReady = false;
#endif
  _volumePending = false;
  _volumeModePending = false;
  _lastVolumeDraw = millis() - DISPLAY_VOLUME_INTERVAL_MS;
  dsp.initDisplay();
#if defined(VOXONE_RUNTIME_L0_DISPLAY_BACKENDS)
  if (!dsp.ready()) {
    _runtimeEnabled = false;
    _bootStep = 2;
    _createDspTask();
    Serial.println("disabled (selected backend unavailable)");
    return;
  }
#endif
  displayQueue=NULL;
  displayQueue = xQueueCreate( 5, sizeof( requestParams_t ) );
  if (displayQueue == nullptr) {
    _runtimeEnabled = false;
    _bootStep = 2;
    Serial.println("disabled (display queue allocation failed)");
    return;
  }
  _pager = new (std::nothrow) Pager();
  if (!_pager) {
    _runtimeEnabled = false;
    _bootStep = 2;
    Serial.println("disabled (display pager allocation failed)");
    return;
  }
  _createDspTask();
  while(_bootStep == 0) { delay(10); }
  _footer = new (std::nothrow) Page();
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76 || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320 || VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
  _plwidget = nullptr;
#else
  _plwidget = new (std::nothrow) PlayListWidget();
#endif
  _nums = new (std::nothrow) NumWidget();
  _clock = new (std::nothrow) ClockWidget();
  _meta = new (std::nothrow) ScrollWidget();
  _title1 = new (std::nothrow) ScrollWidget();
  _plcurrent = new (std::nothrow) ScrollWidget();
  if (!_footer || !_nums || !_clock || !_meta || !_title1 || !_plcurrent
#if !(VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76 || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320 || VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED)
      || !_plwidget
#endif
  ) {
    _runtimeEnabled = false;
    _bootStep = 2;
    Serial.println("disabled (display widget allocation failed)");
    return;
  }
  Serial.println("done");
}

bool Display::initSecurityRecovery() {
  _securityRecoveryOnly = true;
  if (!_runtimeEnabled) return false;

  config.loadTheme();
  const auto backlight =
      voxone::hardware::currentHardware().display.backlight;
  bool enableBacklight = voxone::hardware::hasPin(backlight);
#if defined(VOXONE_RUNTIME_L0_DISPLAY_BACKENDS)
  enableBacklight = enableBacklight &&
      voxone::hardware::runtimeHardwareConfig().display ==
          voxone::hardware::RuntimeDisplay::St7796s_480x320;
#endif
  if (enableBacklight) {
    pinMode(backlight, OUTPUT);
    analogWrite(backlight, 255);
  }
  dsp.initDisplay();
#if defined(VOXONE_RUNTIME_OLED_128X64_BACKENDS) || \
    defined(VOXONE_RUNTIME_L0_DISPLAY_BACKENDS)
  if (!dsp.ready()) {
    _runtimeEnabled = false;
    Serial.println("##[WARN]# SECURITY Recovery display backend unavailable");
    return false;
  }
#endif
  _pager = new (std::nothrow) Pager();
  if (!_pager) {
    _runtimeEnabled = false;
    Serial.println("##[WARN]# SECURITY Recovery display allocation failed");
    return false;
  }
  _pager->begin();
  _bootStep = 1;
  _apScreen();
  _bootStep = 2;
  dsp.loop(true);
  return true;
}

void Display::serviceSecurityRecovery() {
  if (!_securityRecoveryOnly || !_runtimeEnabled || !_pager) return;
  _pager->loop();
  dsp.loop();
}

uint16_t Display::width(){ return _runtimeEnabled ? dsp.width() : 0; }
uint16_t Display::height(){ return _runtimeEnabled ? dsp.height() : 0; }
bool Display::supportsBluetoothTransportView() const {
#if VOXONE_DISPLAY_COMPILES_ST7796 || VOXONE_DISPLAY_COMPILES_SMALL_OLED
  return displayBluetoothTransportViewAvailable(_runtimeEnabled,
                                                 dsp.activeProfile());
#else
  return false;
#endif
}

#if TIME_SIZE>19
  #if DSP_MODEL==DSP_SSD1322
    #define BOOT_PRG_COLOR    WHITE
    #define BOOT_TXT_COLOR    WHITE
    #define PINK              WHITE
  #else
    #define BOOT_PRG_COLOR    0xE68B
    #define BOOT_TXT_COLOR    0xFFFF
    #define PINK              0xF97F
  #endif
#endif

void Display::_bootScreen(){
  _boot = new Page();
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (smallOledActive() && oled128x32Active()) {
    _boot->addWidget(new ProgressWidget(startup128x32::progress, bootPrgConf,
                                        WHITE, BLACK));
    // Page::loop() preserves insertion order: repaint the logo after every
    // animation update so the y=0..15 band remains visible for every frame.
    _boot->addWidget(new Oled128x32StartupLogoWidget());
    _bootstring = (TextWidget*) &_boot->addWidget(new TextWidget(
        startup128x32::ssid, startup128x32::ssidBufferSize, WHITE, BLACK));
    char ssid[startup128x32::ssidBufferSize];
    voxone::oled128x32::formatStartupSsid(ssid, sizeof(ssid), "");
    _bootstring->setText(ssid);
  } else if (smallOledActive()) {
    _boot->addWidget(new Oled128x64StartupLogoWidget());
    _boot->addWidget(new ProgressWidget(startup128x64::progress,
                                        bootPrgConf, WHITE, BLACK));
    _bootstring = (TextWidget*) &_boot->addWidget(new TextWidget(
        startup128x64::ssid, startup128x64::ssidBufferSize, WHITE, BLACK));
    char ssid[startup128x64::ssidBufferSize];
    voxone::oled128x64::formatStartupSsid(ssid, sizeof(ssid), "");
    _bootstring->setText(ssid);
  } else
#endif
  {
  _boot->addWidget(new ProgressWidget(bootWdtConf, bootPrgConf, config.theme.title2, config.theme.background));
  _bootstring = (TextWidget*) &_boot->addWidget(new TextWidget(bootstrConf, 50, config.theme.meta, config.theme.background));
  }
  _pager->addPage(_boot);
  _pager->setPage(_boot, true);
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (!smallOledActive()) dsp.drawLogo(bootLogoTop);
#else
  dsp.drawLogo(bootLogoTop);
#endif
  _bootStep = 1;
}

void Display::_buildPager(){
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (smallOledActive() && oled128x32Active()) {
    _meta->init("*", player128x32::station, WHITE, BLACK);
    _meta->setTextScale(voxone::oled128x32::kStationScaleX,
                        voxone::oled128x32::kStationScaleY);
    _title1->init("*", player128x32::artist, WHITE, BLACK);
  } else if (smallOledActive()) {
    _meta->init("*", player128x64::station, WHITE, BLACK);
    _title1->init("*", player128x64::artist, WHITE, BLACK);
  } else
#endif
  {
  _meta->init("*", metaConf, config.theme.meta, config.theme.metabg);
  #if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  _st7789Station = new ScrollWidget("*", st7789StationConf, config.theme.meta, config.theme.metabg);
#endif
  _title1->init("*", title1Conf, config.theme.title1, config.theme.background);
  }
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (smallOledActive())
    _clock->init(oled128x32Active() ? player128x32::screensaverClock
                                    : player128x64::screensaverClock,
                 0, 0);
  else
#endif
    _clock->init(clockConf, 0, 0);
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (smallOledActive())
    _plcurrent->init("*", oled128x32Active() ? stations128x32::selected
                                              : stations128x64::selected,
                     WHITE, BLACK);
  else
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  _plcurrent->init("*", st7796StationConf, config.theme.plcurrent, config.theme.plcurrentbg);
#else
    _plcurrent->init("*", playlistConf, config.theme.plcurrent, config.theme.plcurrentbg);
#endif
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (smallOledActive()) {
    _plheader = new TextWidget(oled128x32Active() ? stations128x32::header
                                                  : stations128x64::header,
                               24, WHITE, BLACK);
    _plcounter = new TextWidget(oled128x32Active() ? stations128x32::counter
                                                   : stations128x64::counter,
                                16, WHITE, BLACK);
    _plplaying = new TextWidget(oled128x32Active() ? stations128x32::playing
                                                   : stations128x64::playing,
                                8, WHITE, BLACK);
    if (!oled128x32Active()) _plheader->setText("WEB - STACJA");
  } else
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  {
  _plheader = new TextWidget(playlistHeaderConf, 30, config.theme.meta, config.theme.metabg);
  _plcounter = new TextWidget(playlistCounterConf, 16, config.theme.meta, config.theme.background);
  _plplaying = new TextWidget(playlistPlayingConf, 8, config.theme.meta, config.theme.background);
  _plheader->setText("WEB - STACJA");
  }
#elif VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  {
  _plheader = new TextWidget(st7796PlaylistHeaderConf, 30, config.theme.meta, config.theme.metabg);
  _plcounter = new TextWidget(st7796PlaylistCounterConf, 16, config.theme.meta, config.theme.background);
  _plplaying = new TextWidget(st7796PlaylistPlayingConf, 8, config.theme.meta, config.theme.background);
  _plheader->setText("WEB - STACJA");
  }
#else
  {
  _plwidget->init(_plcurrent);
    _plcurrent->moveTo({TFT_FRAMEWDT, (uint16_t)(_plwidget->currentTop()), (int16_t)playlistConf.width});
  }
#endif
  #ifndef HIDE_TITLE2
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
    if (smallOledActive() && !oled128x32Active())
      _title2 = new ScrollWidget("*", player128x64::title, WHITE, BLACK);
    else if (!smallOledActive())
#endif
    _title2 = new ScrollWidget("*", title2Conf, config.theme.title2, config.theme.background);
  #endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if (!smallOledActive()) {
    const auto onTextChanged = [](void* context, uint8_t row) {
      static_cast<Display*>(context)->_st7796ScrollTextChanged(row);
    };
    _meta->setChangeObserver(this, 0, onTextChanged);
    _title1->setChangeObserver(this, 1, onTextChanged);
    if (_title2) _title2->setChangeObserver(this, 2, onTextChanged);
  }
#endif
  _playerScrollBind();
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
    _plbackground = new FillWidget(playlBGConf, config.theme.metabg);
#else
    if (!smallOledActive())
      _plbackground = new FillWidget(playlBGConf, config.theme.plcurrentfill);
#endif
    if (!smallOledActive()) {
      #if DSP_INVERT_TITLE || defined(DSP_OLED)
        _metabackground = new FillWidget(metaBGConf, config.theme.metafill);
      #else
        _metabackground = new FillWidget(metaBGConfInv, config.theme.metafill);
      #endif
    }
  #ifndef HIDE_VU
    if (!smallOledActive())
      _vuwidget = new VuWidget(vuConf, bandsConf, config.theme.vumax, config.theme.vumin, config.theme.background);
  #endif
  #if !defined(HIDE_VOLBAR)
    if (!smallOledActive())
      _volbar = new SliderWidget(volbarConf, config.theme.volbarin, config.theme.background, kDisplayVolumeMax, config.theme.volbarout);
  #endif
  #ifndef HIDE_VOL
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    if (!smallOledActive()) {
      _st7796Volume = new St7796VolumeWidget(voltxtConf, config.theme.meta, config.theme.background);
      PlayerDisplayView initialView{};
      capturePlayerDisplayView(initialView, WiFi.RSSI());
      _st7796Volume->setVolume(initialView.userVolume, initialView.muted);
    }
#else
    _voltxt = new TextWidget(voltxtConf, 10, config.theme.vol, config.theme.background);
#endif
  #endif
  #ifndef HIDE_IP
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
    if (smallOledActive())
      _volip = new TextWidget(oled128x32Active() ? player128x32::volumeIp
                                                 : player128x64::volumeIp,
                              30, WHITE, BLACK);
    else
#endif
      _volip = new TextWidget(iptxtConf, 30, config.theme.ip, config.theme.background);
  #endif
  #ifndef HIDE_RSSI
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
    if (smallOledActive() && !oled128x32Active())
      _rssi = new TextWidget(player128x64::wifi, 20, WHITE, BLACK);
    else if (!smallOledActive())
#endif
      _rssi = new TextWidget(rssiConf, 20, config.theme.rssi, config.theme.background);
  #endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if (!smallOledActive()) {
    _st7796Playback = new St7796PlaybackIconWidget(st7796PlaybackFrameConf, config.theme.meta,
                                                 config.theme.background);
    _st7796Loud = new St7796LabelFrameWidget(st7796LoudFrameConf, config.theme.meta,
                                           config.theme.background, true);
    _st7796Loud->setLabel(displayLoudLabel(false));
    _st7796Source = new St7796LabelFrameWidget(st7796SourceFrameConf, config.theme.meta,
                                             config.theme.background, true);
    _st7796Source->setLabel("WEB");
    _st7796Eq = new St7796LabelFrameWidget(st7796EqFrameConf, config.theme.meta,
                                         config.theme.background, true);
    _st7796Eq->setLabel(runtimeEqPresetLabel());
    _st7796Mode = new St7796LabelFrameWidget(st7796ModeFrameConf, config.theme.meta,
                                           config.theme.background, true);
    _st7796Mode->setLabel(displayDlnaModeLabel(DisplayDlnaMode::Unavailable));
    _st7796BluetoothIcon = new St7796BluetoothWidget(st7796BluetoothIconConf, config.theme.meta, config.theme.background);
  }
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  _st7789PlayerStatus = new St7789PlayerStatusWidget();
#endif
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (smallOledActive())
    _nums->init(oled128x32Active() ? player128x32::volumeNumber
                                   : player128x64::volumeNumber,
                10, WHITE, BLACK);
  else
#endif
    _nums->init(numConf, 10, config.theme.digit, config.theme.background);
  
  if (smallOledActive()) {
    if(_volbar) _footer->addWidget(_volbar);
    if(_voltxt) _footer->addWidget(_voltxt);
    if(_volip) _footer->addWidget(_volip);
  }
  
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  pages[PG_PLAYER]->addWidget(new FillWidget(st7789StationBandConf, config.theme.metabg));
  pages[PG_PLAYER]->addWidget(_st7789Station);
#else
  if (!smallOledActive() && _metabackground)
    pages[PG_PLAYER]->addWidget(_metabackground);
  pages[PG_PLAYER]->addWidget(_meta);
#endif
  pages[PG_PLAYER]->addWidget(_title1);
  if(_title2) pages[PG_PLAYER]->addWidget(_title2);
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if (!smallOledActive())
    pages[PG_PLAYER]->addWidget(new FillWidget(st7796LowerDividerConf, config.theme.div));
#endif
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (smallOledActive() && oled128x32Active()) {
    oled128x32PlayerStatus = new Oled128x32PlayerStatusWidget();
    pages[PG_PLAYER]->addWidget(oled128x32PlayerStatus);
  } else if (smallOledActive()) {
    pages[PG_PLAYER]->addWidget(new Oled128x64PlayerStatusWidget());
    if (_rssi) pages[PG_PLAYER]->addWidget(_rssi);
  } else
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  {
  pages[PG_PLAYER]->addWidget(new FillWidget(st7789DividerConf, config.theme.div));
  pages[PG_PLAYER]->addWidget(_st7789PlayerStatus);
  }
#elif BITRATE_FULL
  {
    _fullbitrate = new BitrateWidget(fullbitrateConf, config.theme.bitrate, config.theme.background);
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    _fullbitrate->setFrameWidth(fullbitrateWidth);
#endif
    pages[PG_PLAYER]->addWidget( _fullbitrate);
  }

  #else
  {
    _bitrate = new TextWidget(bitrateConf, 30, config.theme.bitrate, config.theme.background);
    pages[PG_PLAYER]->addWidget( _bitrate);
  }
  #endif
  if (!smallOledActive()) {
    if(_vuwidget) pages[PG_PLAYER]->addWidget(_vuwidget);
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
#else
    pages[PG_PLAYER]->addWidget(_clock);
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    if(_st7796Volume) pages[PG_PLAYER]->addWidget(_st7796Volume);
    pages[PG_PLAYER]->addWidget(_st7796Playback);
    pages[PG_PLAYER]->addWidget(_st7796Loud);
    pages[PG_PLAYER]->addWidget(_st7796Eq);
    pages[PG_PLAYER]->addWidget(_st7796Source);
    pages[PG_PLAYER]->addWidget(_st7796Mode);
    pages[PG_PLAYER]->addWidget(_st7796BluetoothIcon);
    if(_rssi) pages[PG_PLAYER]->addWidget(_rssi);
#else
    pages[PG_PLAYER]->addPage(_footer);
#endif
#endif
  }
  pages[PG_SCREENSAVER]->addWidget(_clock);

  if (!smallOledActive() && _metabackground)
    pages[PG_DIALOG]->addWidget(_metabackground);
  pages[PG_DIALOG]->addWidget(_meta);
  pages[PG_DIALOG]->addWidget(_nums);
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if (!smallOledActive()) {
    if(_volip) pages[PG_DIALOG]->addWidget(_volip);
    if(_volip) _volip->lock();
  }
#endif
  if (smallOledActive())
    pages[PG_DIALOG]->addPage(_footer);
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (smallOledActive()) {
    if (!oled128x32Active()) pages[PG_PLAYLIST]->addWidget(_plheader);
    pages[PG_PLAYLIST]->addWidget(_plcurrent);
    pages[PG_PLAYLIST]->addWidget(_plplaying);
    pages[PG_PLAYLIST]->addWidget(_plcounter);
  } else
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76 || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  {
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  if(_plbackground) pages[PG_PLAYLIST]->addWidget(_plbackground);
#endif
  pages[PG_PLAYLIST]->addWidget(_plheader);
  pages[PG_PLAYLIST]->addWidget(_plcurrent);
  pages[PG_PLAYLIST]->addWidget(_plcounter);
  pages[PG_PLAYLIST]->addWidget(_plplaying);
  }
#else
  {
  if(_plbackground) {
    pages[PG_PLAYLIST]->addWidget( _plbackground);
    _plbackground->setHeight(_plwidget->itemHeight());
    _plbackground->moveTo({0,(uint16_t)(_plwidget->currentTop()-playlistConf.widget.textsize*2), (int16_t)playlBGConf.width});
  }
  pages[PG_PLAYLIST]->addWidget(_plcurrent);
  pages[PG_PLAYLIST]->addWidget(_plwidget);
  }
#endif
  #if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if (!smallOledActive()) {
    _btTransportPage = new Page();
    _btTransportArtist = new ScrollWidget("*", metaConf, config.theme.meta, config.theme.background);
    _btTransportTitle = new ScrollWidget("*", title1Conf, config.theme.title1, config.theme.background);
    _btTransportPage->addWidget(_btTransportArtist);
    _btTransportPage->addWidget(_btTransportTitle);
    TextWidget* btPrev = new TextWidget(btTransportPrevConf, 4, config.theme.meta, config.theme.background);
    btPrev->setText("<");
    _btTransportPage->addWidget(btPrev);
    _btTransportPage->addWidget(new FillWidget(btNoteHeadConf, config.theme.meta));
    _btTransportPage->addWidget(new FillWidget(btNoteStemConf, config.theme.meta));
    _btTransportPage->addWidget(new FillWidget(btNoteFlagConf, config.theme.meta));
    _btTransportPage->addWidget(new FillWidget(btNoteFlagEndConf, config.theme.meta));
    TextWidget* btNext = new TextWidget(btTransportNextConf, 4, config.theme.meta, config.theme.background);
    btNext->setText(">");
    _btTransportPage->addWidget(btNext);
    _btTransportPlayback = new TextWidget(btTransportPlaybackConf, 8, config.theme.meta, config.theme.background);
    _btTransportPage->addWidget(_btTransportPlayback);
  }
  #endif
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (smallOledActive() && oled128x32Active()) {
    _oledBtTransportPage = new Page();
    _oledBtTransportArtist = new ScrollWidget(
        "*", btTransport128x32::artist, WHITE, BLACK);
    _oledBtTransportArtist->setTextScale(
        voxone::oled128x32::kStationScaleX,
        voxone::oled128x32::kStationScaleY);
    _oledBtTransportTitle = new ScrollWidget(
        "*", btTransport128x32::title, WHITE, BLACK);
    _oledBtTransportPage->addWidget(_oledBtTransportArtist);
    _oledBtTransportPage->addWidget(_oledBtTransportTitle);
    _oled128x32BtTransport = new Oled128x32BtTransportWidget();
    _oledBtTransportPage->addWidget(_oled128x32BtTransport);
  } else if (smallOledActive()) {
    _oledBtTransportPage = new Page();
    TextWidget* header = new TextWidget(btTransport128x64::header, 16,
                                        WHITE, BLACK);
    header->setText("BT TRANSPORT");
    _oledBtTransportPage->addWidget(header);
    _oledBtTransportArtist = new ScrollWidget(
        "*", btTransport128x64::artist, WHITE, BLACK);
    _oledBtTransportTitle = new ScrollWidget(
        "*", btTransport128x64::title, WHITE, BLACK);
    _oledBtTransportPage->addWidget(_oledBtTransportArtist);
    _oledBtTransportPage->addWidget(_oledBtTransportTitle);
    TextWidget* previous = new TextWidget(btTransport128x64::previous, 4,
                                          WHITE, BLACK);
    previous->setText("<");
    _oledBtTransportPage->addWidget(previous);
    _oledBtTransportPlayback = new TextWidget(
        btTransport128x64::playback, 8, WHITE, BLACK);
    _oledBtTransportPage->addWidget(_oledBtTransportPlayback);
    TextWidget* next = new TextWidget(btTransport128x64::next, 4,
                                      WHITE, BLACK);
    next->setText(">");
    _oledBtTransportPage->addWidget(next);
    TextWidget* hint = new TextWidget(btTransport128x64::hint, 20,
                                      WHITE, BLACK);
    hint->setText("LONG: PLAYER");
    _oledBtTransportPage->addWidget(hint);
  }
#endif
  for(const auto& p: pages) _pager->addPage(p);
  #if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if (!smallOledActive()) {
    _pager->addPage(_btTransportPage);
    _st7796UpdatePage = new Page();
    TextWidget* updateTitle = new TextWidget({0, 48, 3, WA_CENTER}, 32, 0xFFFF, 0x0000);
    updateTitle->setText("AKTUALIZACJA");
    _st7796UpdatePage->addWidget(updateTitle);
    _st7796UpdateTarget = new TextWidget({0, 112, 2, WA_CENTER}, 48, 0xFFFF, 0x0000);
    _st7796UpdatePage->addWidget(_st7796UpdateTarget);
    _st7796UpdateBar = new St7796UpdateProgressWidget();
    _st7796UpdatePage->addWidget(_st7796UpdateBar);
    _st7796UpdatePercent = new TextWidget({0, 232, 3, WA_CENTER}, 12, 0xFFFF, 0x0000);
    _st7796UpdatePage->addWidget(_st7796UpdatePercent);
    _st7796UpdateActivity = new TextWidget({0, 302, 1, WA_CENTER}, 80, 0xFFFF, 0x0000);
    _st7796UpdatePage->addWidget(_st7796UpdateActivity);
    _pager->addPage(_st7796UpdatePage);
  }
  #endif
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (_oledBtTransportPage) _pager->addPage(_oledBtTransportPage);
#endif
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (smallOledActive()) {
    _oled128x64UpdatePage = new Page();
    TextWidget* updateTitle = new TextWidget(
        oled128x32Active() ? update128x32::title : update128x64::title,
        16, WHITE, BLACK);
    updateTitle->setText("UPDATE");
    _oled128x64UpdatePage->addWidget(updateTitle);
    _oled128x64UpdateTarget = new TextWidget(
        oled128x32Active() ? update128x32::target : update128x64::target,
        16, WHITE, BLACK);
    _oled128x64UpdatePage->addWidget(_oled128x64UpdateTarget);
    _oled128x64UpdatePercent = new TextWidget(
        oled128x32Active() ? update128x32::percent : update128x64::percent,
        8, WHITE, BLACK);
    _oled128x64UpdatePage->addWidget(_oled128x64UpdatePercent);
    if (!oled128x32Active()) {
      _oled128x64UpdateBar = new Oled128x64UpdateProgressWidget();
      _oled128x64UpdatePage->addWidget(_oled128x64UpdateBar);
    }
    _oled128x64UpdateActivity = new TextWidget(
        oled128x32Active() ? update128x32::activity : update128x64::activity,
        24, WHITE, BLACK);
    _oled128x64UpdatePage->addWidget(_oled128x64UpdateActivity);
    _pager->addPage(_oled128x64UpdatePage);
  }
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  _st7789UpdatePage = new Page();
  TextWidget* updateTitle = new TextWidget(
      {0, st7789_284x76::updateTitleTop, 1, WA_CENTER}, 16,
      config.theme.meta, config.theme.background);
  updateTitle->setText("UPDATE");
  _st7789UpdatePage->addWidget(updateTitle);
  _st7789UpdateTarget = new TextWidget(
      {0, st7789_284x76::updateTargetTop, 1, WA_CENTER}, 16,
      config.theme.title1, config.theme.background);
  _st7789UpdatePage->addWidget(_st7789UpdateTarget);
  _st7789UpdatePercent = new TextWidget(
      {0, st7789_284x76::updatePercentTop, 2, WA_CENTER}, 8,
      config.theme.digit, config.theme.background);
  _st7789UpdatePage->addWidget(_st7789UpdatePercent);
  _st7789UpdateBar = new St7789UpdateProgressWidget();
  _st7789UpdatePage->addWidget(_st7789UpdateBar);
  _st7789UpdateActivity = new TextWidget(
      {0, st7789_284x76::updateActivityTop, 1, WA_CENTER}, 24,
      config.theme.title2, config.theme.background);
  _st7789UpdatePage->addWidget(_st7789UpdateActivity);
  _pager->addPage(_st7789UpdatePage);
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  _st7796PlayerReady = !smallOledActive();
#endif
}

void Display::_apScreen() {
  if(_boot) _pager->removePage(_boot);
    _boot = new Page();
    const auto presentation = network.presentationSnapshot();
    const bool presentationReady = presentation.initialized;
    const bool activeAp = presentationReady && presentation.apActive;
    const bool firstBootSetup =
        activeAp &&
        presentation.mode == voxone::first_boot::Mode::FirstBootSetup;
    const bool securityRecovery =
        activeAp &&
        presentation.screen == voxone::first_boot::Screen::SecurityRecovery;
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
    if (smallOledActive() && oled128x32Active()) {
      ScrollWidget* title = (ScrollWidget*) &_boot->addWidget(
          new ScrollWidget("*", setup128x32::title, WHITE, BLACK));
      title->setText(presentationReady ? presentation.title : "VoxOne");

      ScrollWidget* ssid = (ScrollWidget*) &_boot->addWidget(
          new ScrollWidget("*", setup128x32::ssid, WHITE, BLACK));
      ssid->setText(activeAp ? presentation.apSsid : "");

      TextWidget* detail = (TextWidget*) &_boot->addWidget(
          new TextWidget(setup128x32::detail, 30, WHITE, BLACK));
      if (activeAp)
        detail->setText(presentation.apAddress, "IP %s");
      else
        detail->setText("");

      ScrollWidget* footer = (ScrollWidget*) &_boot->addWidget(
          new ScrollWidget("*", setup128x32::footer, WHITE, BLACK));
      footer->setText(presentationReady ? presentation.statusText : "");
      _pager->addPage(_boot);
      _pager->setPage(_boot);
      return;
    }
#endif
#if defined(VOXONE_RUNTIME_L0_DISPLAY_BACKENDS)
    if (smallOledActive()) {
      ScrollWidget* title = (ScrollWidget*) &_boot->addWidget(
          new ScrollWidget("*", setup128x64::title, WHITE, BLACK));
      title->setText(presentationReady ? presentation.title : "VoxOne");

      if (!activeAp) {
        ScrollWidget* status = (ScrollWidget*) &_boot->addWidget(
            new ScrollWidget("*", setup128x64::footer, WHITE, BLACK));
        status->setText(presentationReady ? presentation.statusText : "");
        _pager->addPage(_boot);
        _pager->setPage(_boot);
        return;
      }

      TextWidget* product = (TextWidget*) &_boot->addWidget(
          new TextWidget(setup128x64::product, 30, WHITE, BLACK));
      product->setText(securityRecovery ? presentation.product : "VoxOne");
      ScrollWidget* ssid = (ScrollWidget*) &_boot->addWidget(
          new ScrollWidget("*", setup128x64::ssid, WHITE, BLACK));
      ssid->setText(presentation.apSsid);
      TextWidget* detailLabel = (TextWidget*) &_boot->addWidget(
          new TextWidget(setup128x64::detailLabel, 30, WHITE, BLACK));
      TextWidget* detailValue = (TextWidget*) &_boot->addWidget(
          new TextWidget(setup128x64::detailValue, 30, WHITE, BLACK));
      if (securityRecovery || firstBootSetup) {
        detailLabel->setText("IP");
        detailValue->setText(presentation.apAddress);
      } else {
        detailLabel->setText(presentation.presentApCredential
                                 ? LANG::apPassTxt : "");
        detailValue->setText(presentation.presentApCredential
                                 ? apPassword : "");
      }
      ScrollWidget* footer = (ScrollWidget*) &_boot->addWidget(
          new ScrollWidget("*", setup128x64::footer, WHITE, BLACK));
      if (firstBootSetup) footer->setText(presentation.statusText);
      else footer->setText(presentation.apAddress, "IP %s");
      _pager->addPage(_boot);
      _pager->setPage(_boot);
      return;
    }
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    const bool wifiRecovery =
        activeAp &&
        presentation.mode == voxone::first_boot::Mode::WifiRecovery;
#endif
      #if DSP_INVERT_TITLE || defined(DSP_OLED)
      _boot->addWidget(new FillWidget(metaBGConf, config.theme.metafill));
      #else
      _boot->addWidget(new FillWidget(metaBGConfInv, config.theme.metafill));
      #endif
    ScrollWidget *bootTitle = (ScrollWidget*) &_boot->addWidget(new ScrollWidget("*",
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
        setup128x64::title,
#else
        apTitleConf,
#endif
        config.theme.meta, config.theme.metabg));
    bootTitle->setText(
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
        presentationReady ? presentation.title : "VoxOne");
#else
        securityRecovery ? presentation.product :
        (presentationReady ? presentation.title : "VoxOne"));
#endif

    if (!activeAp) {
      ScrollWidget *bootStatus = (ScrollWidget*) &_boot->addWidget(
          new ScrollWidget("*", apSettConf, config.theme.title2,
                           config.theme.background));
      bootStatus->setText(presentationReady ? presentation.statusText : "");
      _pager->addPage(_boot);
      _pager->setPage(_boot);
      return;
    }
    if (securityRecovery) {
      TextWidget* recoveryTitle = (TextWidget*) &_boot->addWidget(
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
          new TextWidget(setup128x64::product, 30, config.theme.title1,
#else
          new TextWidget(apNameConf, 30, config.theme.title1,
#endif
                         config.theme.background));
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
      recoveryTitle->setText(presentation.product);
      ScrollWidget* recoverySsid = (ScrollWidget*) &_boot->addWidget(
          new ScrollWidget("*", setup128x64::ssid, config.theme.clock,
                           config.theme.background));
      recoverySsid->setText(presentation.apSsid);
      TextWidget* recoveryAddressLabel = (TextWidget*) &_boot->addWidget(
          new TextWidget(setup128x64::detailLabel, 30, config.theme.title1,
                         config.theme.background));
      recoveryAddressLabel->setText("IP");
#else
      recoveryTitle->setText(presentation.title);
      TextWidget* recoverySsid = (TextWidget*) &_boot->addWidget(
          new TextWidget(apName2Conf, 30, config.theme.clock,
                         config.theme.background));
      recoverySsid->setText(presentation.apSsid);
#endif
      TextWidget* recoveryAddress = (TextWidget*) &_boot->addWidget(
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
          new TextWidget(setup128x64::detailValue, 30, config.theme.clock,
#else
          new TextWidget(apPass2Conf, 30, config.theme.clock,
#endif
                         config.theme.background));
      recoveryAddress->setText(presentation.apAddress);
      _pager->addPage(_boot);
      _pager->setPage(_boot);
      return;
    }
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    if (firstBootSetup) {
      TextWidget* firstBootSsid = (TextWidget*) &_boot->addWidget(
          new TextWidget(firstBootSsidConf, 30, config.theme.clock,
                         config.theme.background));
      firstBootSsid->setText(presentation.apSsid, "SSID %s");

      TextWidget* firstBootStatus = (TextWidget*) &_boot->addWidget(
          new TextWidget(firstBootStatusConf, 30, config.theme.title2,
                         config.theme.background));
      firstBootStatus->setText(presentation.statusText);

      TextWidget* firstBootIp = (TextWidget*) &_boot->addWidget(
          new TextWidget(firstBootIpConf, 30, config.theme.clock,
                         config.theme.background));
      firstBootIp->setText(presentation.apAddress, "IP %s");

      _pager->addPage(_boot);
      _pager->setPage(_boot);
      dsp.drawFirstBootLogo(firstBootLogoTop);
      return;
    }
    if (wifiRecovery) {
      TextWidget* recoverySsid = (TextWidget*) &_boot->addWidget(
          new TextWidget(wifiRecoverySsidConf, 30, config.theme.clock,
                         config.theme.background));
      recoverySsid->setText(presentation.apSsid, "SSID %s");

      TextWidget* recoveryPassword = (TextWidget*) &_boot->addWidget(
          new TextWidget(wifiRecoveryPasswordConf, 30, config.theme.title2,
                         config.theme.background));
      if (presentation.presentApCredential)
        recoveryPassword->setText(apPassword, "HASŁO %s");

      TextWidget* recoveryIp = (TextWidget*) &_boot->addWidget(
          new TextWidget(wifiRecoveryIpConf, 30, config.theme.clock,
                         config.theme.background));
      recoveryIp->setText(presentation.apAddress, "IP %s");

      _pager->addPage(_boot);
      _pager->setPage(_boot);
      dsp.drawFirstBootLogo(firstBootLogoTop);
      return;
    }
#endif
    TextWidget *apname = (TextWidget*) &_boot->addWidget(new TextWidget(
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
      apNameConf,
#elif VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
      setup128x64::product,
#else
      apNameConf,
#endif
      30, config.theme.title1, config.theme.background));
    apname->setText("VoxOne");
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
    ScrollWidget *apname2 = (ScrollWidget*) &_boot->addWidget(
        new ScrollWidget("*", setup128x64::ssid, config.theme.clock,
                         config.theme.background));
#else
    TextWidget *apname2 = (TextWidget*) &_boot->addWidget(new TextWidget(
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
      apName2Conf,
#else
      apName2Conf,
#endif
      30, config.theme.clock, config.theme.background));
#endif
    apname2->setText(presentation.apSsid);
    TextWidget *appass = (TextWidget*) &_boot->addWidget(new TextWidget(
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
      apPassConf,
#elif VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
      setup128x64::detailLabel,
#else
      apPassConf,
#endif
      30, config.theme.title1, config.theme.background));
    appass->setText(firstBootSetup ? "IP" :
        (presentation.presentApCredential ? LANG::apPassTxt : ""));
    TextWidget *appass2 = (TextWidget*) &_boot->addWidget(new TextWidget(
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
      apPass2Conf,
#elif VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
      setup128x64::detailValue,
#else
      apPass2Conf,
#endif
      30, config.theme.clock, config.theme.background));
    appass2->setText(firstBootSetup ? presentation.apAddress :
        (presentation.presentApCredential ? apPassword : ""));
    ScrollWidget *bootSett = (ScrollWidget*) &_boot->addWidget(new ScrollWidget("*",
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
        setup128x64::footer,
#else
        apSettConf,
#endif
        config.theme.title2, config.theme.background));
    if (firstBootSetup) bootSett->setText(presentation.statusText);
    else bootSett->setText(presentation.apAddress, "IP %s");
    _pager->addPage(_boot);
    _pager->setPage(_boot);
}

void Display::_start() {
  if(_boot) _pager->removePage(_boot);
  if (network.status != CONNECTED) {
    _apScreen();
    _bootStep = 2;
    return;
  }
  _buildPager();
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if (!smallOledActive() && !_playerScrollCoreEnabled())
    _st7796ScrollMode(true);
#endif
  _renderedMode = PLAYER;
  config.setTitle(LANG::const_PlReady);

  if(_vuwidget) _vuwidget->lock();
  if(_rssi)     _setRSSI(WiFi.RSSI());
  #ifndef HIDE_IP
    if(_volip) {
      if (smallOledActive()) _volip->setText(config.ipToStr(WiFi.localIP()));
      else _volip->setText(config.ipToStr(WiFi.localIP()), iptxtFmt);
    }
  #endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  if(_volip) _volip->lock();
#endif
  _pager->setPage(pages[PG_PLAYER]);
  _playerScrollEnter(millis());
  _volume();
  _station();
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if (!smallOledActive()) _updatePlaybackStatus();
#endif
  _time(false);
  _bootStep = 2;
}

void Display::_showDialog(const char *title){
  dsp.setScrollId(NULL);
  _pager->setPage( pages[PG_DIALOG]);
  #ifdef META_MOVE
    _meta->moveTo(metaMove);
  #endif
  _meta->setAlign(WA_CENTER);
  _meta->setText(title);
}

static bool activeSourceVuVisible() {
  if (systemUpdateAudioBlocked()) return false;
  DisplaySourceView source{};
  return getDisplaySourceView && getDisplaySourceView(source)
      ? displaySourceVuVisible(source) : player.isRunning();
}

void Display::_updateUpdateScreen(const UpdateProgressSnapshot& snapshot) {
  const UpdateDisplayProgress progress = _updateDisplayProgress.observe(snapshot);
  if (_updateDisplayRevision == snapshot.revision &&
      _updateDisplayAcquisition == snapshot.acquisition) return;
  _updateDisplayRevision = snapshot.revision;
  _updateDisplayAcquisition = snapshot.acquisition;
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if (smallOledActive()) {
    if (_oled128x64UpdateTarget)
      _oled128x64UpdateTarget->setText(updateTargetCompactDisplayName(snapshot.target));
    if (_oled128x64UpdateActivity)
      _oled128x64UpdateActivity->setText(updateActivityCompactDisplayText(snapshot.activity));
    if (_oled128x64UpdateBar)
      _oled128x64UpdateBar->setProgress(progress, snapshot.acquisition);
    if (_oled128x64UpdatePercent) {
      if (progress.determinate)
        _oled128x64UpdatePercent->setText(progress.percent, "%d%%");
      else _oled128x64UpdatePercent->setText("...");
    }
  } else {
    if (_st7796UpdateTarget) _st7796UpdateTarget->setText(updateTargetDisplayName(snapshot.target));
    if (_st7796UpdateActivity)
      _st7796UpdateActivity->setText(updateActivityDisplayText(snapshot.activity));
    if (_st7796UpdateBar) _st7796UpdateBar->setProgress(progress, snapshot.acquisition);
    if (_st7796UpdatePercent) {
      if (progress.determinate) _st7796UpdatePercent->setText(progress.percent, "%d%%");
      else _st7796UpdatePercent->setText("");
    }
  }
#elif VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
  if (_oled128x64UpdateTarget)
    _oled128x64UpdateTarget->setText(updateTargetCompactDisplayName(snapshot.target));
  if (_oled128x64UpdateActivity)
    _oled128x64UpdateActivity->setText(updateActivityCompactDisplayText(snapshot.activity));
  if (_oled128x64UpdateBar) _oled128x64UpdateBar->setProgress(progress, snapshot.acquisition);
  if (_oled128x64UpdatePercent) {
    if (progress.determinate) _oled128x64UpdatePercent->setText(progress.percent, "%d%%");
    else _oled128x64UpdatePercent->setText("...");
  }
#elif VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  if (_st7789UpdateTarget)
    _st7789UpdateTarget->setText(updateTargetCompactDisplayName(snapshot.target));
  if (_st7789UpdateActivity)
    _st7789UpdateActivity->setText(updateActivityCompactDisplayText(snapshot.activity));
  if (_st7789UpdateBar)
    _st7789UpdateBar->setProgress(progress, snapshot.acquisition);
  if (_st7789UpdatePercent) {
    if (progress.determinate) _st7789UpdatePercent->setText(progress.percent, "%d%%");
    else _st7789UpdatePercent->setText("...");
  }
#else
  (void)progress;
#endif
}

void Display::_renderMode(displayMode_e newmode) {
  if (newmode == _renderedMode) return;
  if (_playerScrollCoreEnabled() && _renderedMode == PLAYER &&
      newmode != PLAYER)
    _playerScrollLeave();
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if (!smallOledActive() && !_playerScrollCoreEnabled()) {
    if (_renderedMode == PLAYER && newmode != PLAYER) _st7796ScrollMode(false);
    else if (_renderedMode != PLAYER && newmode == PLAYER) _st7796ScrollMode(true);
  }
#endif
  _renderedMode = newmode;
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  if(_volip) _volip->lock(newmode == PLAYER);
  if(_rssi) _rssi->lock(newmode == VOL);
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if(!smallOledActive() && _volip) _volip->lock(newmode != VOL);
#endif
  dsp.setScrollId(NULL);
  if (newmode == PLAYER) {
    if(activeSourceVuVisible())
      if(clockMove.width<0) _clock->moveBack(); else _clock->moveTo(clockMove);
    else
      _clock->moveBack();
    #ifdef META_MOVE
      _meta->moveBack();
    #endif
    _meta->setAlign(metaConf.widget.align);
    _station();
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
    _st7789Station->setText(config.station.name);
#endif
    _nums->setText("");
    config.isScreensaver = false;
    _pager->setPage(pages[PG_PLAYER]);
    _playerScrollEnter(millis());
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    if (!smallOledActive()) _updatePlaybackStatus();
#endif
    config.setDspOn(config.runtimeSnapshot().dspOn, false);
  }
  if (newmode == SCREENSAVER || newmode == SCREENBLANK) {
    config.isScreensaver = true;
    _pager->setPage( pages[PG_SCREENSAVER]);
    if (newmode == SCREENBLANK) {
      //dsp.clearClock();
      _clock->clear();
      config.setDspOn(false, false);
    }
  }else{
    config.screensaverTicks=SCREENSAVERSTARTUPDELAY;
    config.screensaverPlayingTicks=SCREENSAVERSTARTUPDELAY;
    config.isScreensaver = false;
  }
  if (newmode == VOL) {
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    PlayerDisplayView view{};
    capturePlayerDisplayView(view, WiFi.RSSI());
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
    if (smallOledActive() && oled128x32Active()) {
      char level[5];
      voxone::oled128x32::formatVolumePercent(
          level, sizeof(level), view.userVolume, view.muted);
      _showDialog("");
      _nums->setText(level);
    } else
#endif
    {
      #ifndef HIDE_IP
        _showDialog(LANG::const_DlgVolume);
      #else
        _showDialog(config.ipToStr(WiFi.localIP()));
      #endif
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
      if (oled128x64Active()) {
        char level[5];
        voxone::oled128x32::formatVolumePercent(
            level, sizeof(level), view.userVolume, view.muted);
        _nums->setText(level);
      } else
#endif
      {
        if (view.muted) _nums->setText("MUTE");
        else _nums->setText(view.userVolume, numtxtFmt);
      }
    }
#else
    #ifndef HIDE_IP
      _showDialog(LANG::const_DlgVolume);
    #else
      _showDialog(config.ipToStr(WiFi.localIP()));
    #endif
    const MuteOutputSnapshot output = player.outputSnapshot();
    if (output.muted) _nums->setText("MUTE");
    else _nums->setText(output.userVolume, numtxtFmt);
#endif
  }
  if (newmode == LOST)      _showDialog(LANG::const_DlgLost);
  if (newmode == UPDATING) {
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    _pager->setPage(smallOledActive() ? _oled128x64UpdatePage
                                      : _st7796UpdatePage, true);
#elif VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
    _pager->setPage(_oled128x64UpdatePage, true);
#elif VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
    _pager->setPage(_st7789UpdatePage, true);
#else
    _showDialog(LANG::const_DlgUpdate);
#endif
  }
  if (newmode == SLEEPING)  _showDialog("SLEEPING");
  if (newmode == NUMBERS) _showDialog("");
  if (newmode == STATIONS) {
    _pager->setPage( pages[PG_PLAYLIST]);
    _plcurrent->setText("");
    _drawPlaylist();
  }
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if (!smallOledActive() && newmode == BT_TRANSPORT) {
    _title();
    _pager->setPage(_btTransportPage);
  }
#endif
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (smallOledActive() && newmode == BT_TRANSPORT) {
    _title();
    _pager->setPage(_oledBtTransportPage);
  }
#endif
  
}

#if VOXONE_DISPLAY_COMPILES_ST7796
bool Display::_yamahaTftActive() const {
  return _renderedMode == PLAYER && !smallOledActive() &&
         config.runtimeSnapshot().tftTheme == THEME_YAMAHA;
}

namespace {
uint32_t yamahaHashBytes(uint32_t hash, const void* data, size_t length) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  while (length--) {
    hash ^= *bytes++;
    hash *= 16777619u;
  }
  return hash;
}

uint32_t yamahaViewSignature(const PlayerDisplayView& view) {
  uint32_t hash = 2166136261u;
  hash = yamahaHashBytes(hash, view.station, strlen(view.station));
  hash = yamahaHashBytes(hash, view.artist, strlen(view.artist));
  hash = yamahaHashBytes(hash, view.title, strlen(view.title));
  hash = yamahaHashBytes(hash, &view.source, sizeof(view.source));
  hash = yamahaHashBytes(hash, &view.playback, sizeof(view.playback));
  hash = yamahaHashBytes(hash, &view.userVolume, sizeof(view.userVolume));
  hash = yamahaHashBytes(hash, &view.muted, sizeof(view.muted));
  hash = yamahaHashBytes(hash, &view.btConnected, sizeof(view.btConnected));
  return yamahaHashBytes(hash, &view.wifiLevel, sizeof(view.wifiLevel));
}
}  // namespace

void Display::render_yamaha_tft() {
  if (!_yamahaTftActive()) return;
  PlayerDisplayView view{};
  capturePlayerDisplayView(view, WiFi.RSSI());
  const uint32_t signature = yamahaViewSignature(view);
  if (_yamahaDrawn && signature == _yamahaSignature) return;

  using namespace voxone::yamaha_tft;
  dsp.clearClipping();
  dsp.fillScreen(kBackground);
  dsp.setFont();
  dsp.setTextWrap(false);
  dsp.setTextColor(kCyan, kBackground);

  auto printLine = [](const char* text, int16_t left, int16_t top,
                      int16_t width, int16_t height, uint8_t size) {
    dsp.setClipping({static_cast<uint16_t>(left), static_cast<uint16_t>(top),
                     static_cast<uint16_t>(width),
                     static_cast<uint16_t>(height)});
    dsp.setTextSize(size);
    dsp.setCursor(left, top);
    dsp.print(utf8Rus(text && text[0] ? text : "—"));
    dsp.clearClipping();
  };

  dsp.drawFastHLine(0, kTopBarBottom, kWidth, kCyan);
  dsp.setTextSize(3);
  dsp.setCursor(14, 16);
  dsp.print(view.source == DisplaySourceKind::Bluetooth ? "BT" : "RADIO");
  dsp.setCursor(190, 16);
  dsp.print(displayPlaybackLabel(view.playback));

  const uint8_t signal = boundedSignalLevel(view.wifiLevel);
  for (uint8_t index = 0; index < kSignalBars; ++index) {
    const int16_t barHeight = 8 + index * 8;
    const int16_t left = 402 + index * 17;
    const int16_t top = 45 - barHeight;
    if (index < signal)
      dsp.fillRect(left, top, 11, barHeight, kCyan);
    else
      dsp.drawRect(left, top, 11, barHeight, kCyan);
  }

  printLine(view.station, 16, kContentTop, 448, 42, 4);
  const char* middle = view.title[0] ? view.title :
                       (view.artist[0] ? view.artist : "Brak informacji");
  printLine(middle, 16, 136, 448, 62, 4);
  if (view.artist[0] && view.title[0])
    printLine(view.artist, 18, 202, 444, 28, 2);

  dsp.drawRect(0, kVolumeTop, kWidth, kHeight - kVolumeTop, kCyan);
  dsp.setTextSize(5);
  dsp.setCursor(18, 262);
  if (view.muted) {
    dsp.print("MUTE");
  } else {
    char volume[16];
    snprintf(volume, sizeof(volume), "VOL %3u",
             static_cast<unsigned>(view.userVolume));
    dsp.print(volume);
  }

  _yamahaSignature = signature;
  _yamahaDrawn = true;
}
#endif

void Display::resetQueue(){
  if(displayQueue!=NULL) xQueueReset(displayQueue);
}

void Display::_drawPlaylist() {
  const uint16_t total = config.playlistLength();
  const StationSelectionView view = stationSelectionView(
      total, uiState.selectedStation(), config.lastStation(), player.isRunning());
  if(view.empty()) {
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76 || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320 || VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
    _plcurrent->setText("BRAK STACJI");
    _plcounter->setText("0/0");
    _plplaying->setText("");
#else
    _plwidget->drawPlaylist(view.selected);
#endif
  } else {
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
    _plcurrent->setText(config.stationByNum(view.selected));
#elif VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76 || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    _plcurrent->setText(config.stationByNum(view.selected));
#else
    _plwidget->drawPlaylist(view.selected);
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76 || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320 || VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
    char counter[16];
    snprintf(counter, sizeof(counter), "%u/%u", view.selected, view.total);
    _plcounter->setText(counter);
    _plplaying->setText(view.selectedIsPlaying() ? "GRA" : "");
#endif
  }
}

void Display::_drawNextStationNum(uint16_t num) {
  _meta->setText(config.stationByNum(num));
  _nums->setText(num, "%d");
}

void Display::putRequest(displayRequestType_e type, int payload){
  if (type == NEWMODE) {
    transitionUiMode(static_cast<displayMode_e>(payload));
    return;
  }
  if (type == RESETIDLE) {
    resetUiReturnTimeout();
    return;
  }
  if (type == DRAWPLAYLIST && uiState.mode() == STATIONS) {
    normalizeUiStationSelection();
    resetUiReturnTimeout();
  }
  if (type == DRAWVOL && uiState.mode() == VOL)
    resetUiReturnTimeout();
  if (type == NEXTSTATION) armUiReturnTimeout(30);
  if(displayQueue==NULL) return;
  if (updateLockActive()) return;
  requestParams_t request;
  request.type = type;
  request.payload = payload;
  if(type == DRAWVOL){
    portENTER_CRITICAL(&displayVolumeMux);
    _volumePending = true;
    portEXIT_CRITICAL(&displayVolumeMux);
    return;
  }
  xQueueSend(displayQueue, &request, DSQ_SEND_DELAY);
}

void Display::queueModeRender(displayMode_e mode) {
  if(displayQueue==NULL) return;
  const bool volumeModeRequest = mode == VOL;
  if (volumeModeRequest) {
    bool skipRequest;
    portENTER_CRITICAL(&displayVolumeMux);
    skipRequest = _volumeModePending;
    if(!skipRequest) _volumeModePending = true;
    portEXIT_CRITICAL(&displayVolumeMux);
    if(skipRequest) return;
  }
  const requestParams_t request{NEWMODE, static_cast<int>(mode)};
  if(xQueueSend(displayQueue, &request, DSQ_SEND_DELAY) != pdPASS && volumeModeRequest){
    portENTER_CRITICAL(&displayVolumeMux);
    _volumeModePending = false;
    portEXIT_CRITICAL(&displayVolumeMux);
    Serial.println("##ERROR#:\tdisplay queue full for volume mode");
  }
}

void Display::_setVuVisibility(bool sourceVisible) {
  const auto runtime = config.runtimeSnapshot();
  const MuteOutputSnapshot output = player.outputSnapshot();
  const bool lockVu = !displayVuUnlocked(
      runtime.vuMeter, sourceVisible, output.userVolume);
  if(_vuwidget && _vuwidget->locked() != lockVu) _vuwidget->lock(lockVu);
}

void Display::_layoutChange(bool played){
  _setVuVisibility(played);
  if(played){
    if(clockMove.width<0) _clock->moveBack(); else _clock->moveTo(clockMove);
  }else{
    _clock->moveBack();
  }
}

#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
void Display::_updatePlaybackStatus() {
  if (!_st7796Playback || !_btTransportPlayback) return;
  PlayerDisplayView view{};
  capturePlayerDisplayView(view, WiFi.RSSI());
  const char* label = displayPlaybackLabel(view.playback);
  _st7796Playback->setState(view.playback);
  _btTransportPlayback->setText(label);
}
#endif

#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
void Display::_st7796ScrollMode(bool playerMode) {
  ScrollWidget* rows[3] = {_meta, _title1, _title2};
  if (!_st7796PlayerReady || !rows[0] || !rows[1] || !rows[2]) {
    _st7796Scroll.leave();
    return;
  }
  if (playerMode) _st7796Scroll.enter();
  else _st7796Scroll.leave();
  for (ScrollWidget* row : rows) row->setExternallyScheduled(playerMode);
}

void Display::_st7796ScrollTextChanged(uint8_t row) {
  ScrollWidget* rows[3] = {_meta, _title1, _title2};
  if (!st7796PlayerScrollReady(_st7796PlayerReady, _renderedMode == PLAYER,
                              _st7796Scroll.enabled(), rows[0], rows[1], rows[2])) return;
  for (ScrollWidget* widget : rows)
    if (dsp.getScrollId() == widget) dsp.setScrollId(NULL);
  _st7796Scroll.textChanged(row);
}

void Display::_st7796ScrollTick() {
  ScrollWidget* rows[3] = {_meta, _title1, _title2};
  if (!st7796PlayerScrollReady(_st7796PlayerReady, _renderedMode == PLAYER,
                              _st7796Scroll.enabled(), rows[0], rows[1], rows[2])) return;
  const bool needsScroll[3] = {
      rows[0]->scrollNeeded(), rows[1]->scrollNeeded(), rows[2]->scrollNeeded()};
  const St7796PlayerScroll::Event event = _st7796Scroll.tick(millis(), needsScroll);
  if (event.action == St7796PlayerScroll::Action::Start) {
    rows[event.row]->startScheduledTurn();
  } else if (event.action == St7796PlayerScroll::Action::Step &&
             rows[event.row]->stepScheduledTurn(St7796PlayerScroll::kStepPixels)) {
    _st7796Scroll.cycleFinished();
  }
}
#endif

bool Display::_playerScrollCoreEnabled() const {
  return voxone::display_scroll::playerScrollCoreBuildEnabled();
}

void Display::_playerScrollBind() {
  using namespace voxone::display_scroll;
  const ScrollSettingsRuntimeSnapshot snapshot =
      scrollSettingsRuntimeConfig().snapshot();
  (void)_playerScrollRuntime.configure(_playerScrollCoreEnabled(),
                                       activePlayerScrollProfile(),
                                       snapshot.settings, snapshot.source);
  _playerScrollConfigRevision = snapshot.revision;
  PlayerScrollWidgetBinding rows[PlayerScrollRuntime::kMaximumRows]{};
  uint8_t count = 0;
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  if (_st7789Station) rows[count++] = playerScrollBinding(_st7789Station);
#else
  if (_meta) rows[count++] = playerScrollBinding(_meta);
#endif
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (_title1) rows[count++] = playerScrollBinding(_title1);
#else
  if (_title1) rows[count++] = playerScrollBinding(_title1);
#endif
  if (_title2) rows[count++] = playerScrollBinding(_title2);
  _playerScrollReady = _playerScrollRuntime.setRows(rows, count);
}

void Display::_playerScrollEnter(uint32_t nowMs) {
  if (_playerScrollReady && _playerScrollCoreEnabled()) {
    _playerScrollRefresh(nowMs);
    _playerScrollRuntime.enter(nowMs);
  }
}

void Display::_playerScrollLeave() {
  if (_playerScrollReady) _playerScrollRuntime.leave();
}

void Display::_playerScrollTick(uint32_t nowMs) {
  if (_playerScrollReady && _playerScrollRuntime.active()) {
    _playerScrollRefresh(nowMs);
    (void)_playerScrollRuntime.tick(nowMs);
  }
}

void Display::_playerScrollRefresh(uint32_t nowMs) {
  using namespace voxone::display_scroll;
  const ScrollSettingsRuntimeSnapshot snapshot =
      scrollSettingsRuntimeConfig().snapshot();
  if (snapshot.revision == _playerScrollConfigRevision) return;

  const bool wasActive = _playerScrollRuntime.active();
  const bool changed = _playerScrollRuntime.configure(
      _playerScrollCoreEnabled(), activePlayerScrollProfile(),
      snapshot.settings, snapshot.source);
  _playerScrollConfigRevision = snapshot.revision;
  if (changed && wasActive) _playerScrollRuntime.enter(nowMs);
}

void Display::loop() {
  if(_bootStep==0) {
    _pager->begin();
    _bootScreen();
    return;
  }
  if(displayQueue==NULL || (_locked && !updateLockActive())) return;
  const UpdateProgressSnapshot update = updateProgress();
  if (_bootStep == 2 && updateScreenOwnsDisplay(update)) {
    if (_renderedMode != UPDATING) {
      resetQueue();
      _renderMode(UPDATING);
    }
    _updateUpdateScreen(update);
    requestParams_t ignored;
    xQueueReceive(displayQueue, &ignored, 0);
    _pager->loop();
    dsp.loop();
    return;
  }
  const bool yamahaTft =
#if VOXONE_DISPLAY_COMPILES_ST7796
      _yamahaTftActive();
#else
      false;
#endif
#if VOXONE_DISPLAY_COMPILES_ST7796
  if (_yamahaWasActive && !yamahaTft) {
    _yamahaDrawn = false;
    if (_renderedMode == PLAYER) {
      dsp.fillScreen(config.theme.background);
      _pager->setPage(pages[PG_PLAYER], true);
    }
  }
  _yamahaWasActive = yamahaTft;
#endif
  if (_playerScrollCoreEnabled()) {
    if (!yamahaTft && _renderedMode == PLAYER && !systemUpdateAudioBlocked())
      _playerScrollTick(millis());
  } else if (!yamahaTft) {
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
    if(_renderedMode == PLAYER && !systemUpdateAudioBlocked()) ScrollWidget::nextSt7789ScrollFrame();
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    if (_st7796PlayerReady && _renderedMode == PLAYER && !systemUpdateAudioBlocked()) _st7796ScrollTick();
#endif
  }
  if (!yamahaTft) _pager->loop();
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if (_bootStep == 2) {
    PlayerDisplayView view{};
    capturePlayerDisplayView(view, WiFi.RSSI());
    if (_st7796Volume) _st7796Volume->setDacMuted(dacMute.logicalMuted());
    if (_st7796Eq)
      _st7796Eq->setLabel(runtimeEqPresetLabel());
    if (_st7796Source) _st7796Source->setLabel(displaySourceLabel(view.source));
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
    if (_st7796BluetoothIcon) _st7796BluetoothIcon->setConnected(view.btConnected);
#endif
  }
#endif
  requestParams_t request;
  if(xQueueReceive(displayQueue, &request, DSP_QUEUE_TICKS)){
#if VOXONE_DISPLAY_COMPILES_ST7796
    if (yamahaTft) _yamahaDrawn = false;
#endif
    switch (request.type){
        case NEWMODE: {
          _renderMode((displayMode_e)request.payload);
          if(request.payload == VOL){
            portENTER_CRITICAL(&displayVolumeMux);
            _volumeModePending = false;
            portEXIT_CRITICAL(&displayVolumeMux);
          }
          break;
        }
        case RESETIDLE: break;
        case CLOSEPLAYLIST: player.sendCommand({PR_PLAY, request.payload}); break;
        case CLOCK: 
          if(_renderedMode==PLAYER || _renderedMode==SCREENSAVER) _time(request.payload==1);
          break;
        case NEWTITLE:
          if (!systemUpdateAudioBlocked()) { _title(); _layoutChange(activeSourceVuVisible()); }
          break;
        case NEWSTATION:
          if (systemUpdateAudioBlocked()) break;
          _station();
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76 || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320 || VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
          if(_renderedMode==STATIONS && _plplaying) _plplaying->setText(player.isRunning() && uiState.selectedStation() == config.lastStation() ? "GRA" : "");
#endif
          break;
        case NEXTSTATION: _drawNextStationNum(request.payload); break;
        case DRAWPLAYLIST: _drawPlaylist(); break;
        case DRAWVOL: _volume(); break;
        case DBITRATE: {
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76 || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320 || VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
            PlayerDisplayView view{};
            capturePlayerDisplayView(view, WiFi.RSSI());
            const DisplayAudioInfo& info = view.audioInfo;
#else
            DisplaySourceView source{};
            if (getDisplaySourceView) getDisplaySourceView(source);
            const DisplayAudioInfo info = selectDisplayAudioInfo(
                source, config.station.bitrate, config.configFmt);
#endif
            if (_fullbitrate) {
              if (info.bluetooth)
                _fullbitrate->setCustomText(info.top, info.bottom);
              else {
                _fullbitrate->setBitrate(info.radioBitrate);
                _fullbitrate->setFormat(info.radioFormat);
              }
            } else if (_bitrate) {
                char buf[20];
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
              snprintf(buf, sizeof(buf), "%s", view.audioText);
#else
              snprintf(buf, sizeof(buf), bitrateFmt, config.station.bitrate);
#endif
              _bitrate->setText(config.station.bitrate == 0 ? "" : buf);
            }
          }
          break;
        case SHOWVUMETER: {
          if(_vuwidget){
            _layoutChange(activeSourceVuVisible());
          }
          break;
        }
        case BOOTSTRING: {
          if(_bootstring) {
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
            if (oled128x64Active()) {
              char ssid[startup128x64::ssidBufferSize];
              voxone::oled128x64::formatStartupSsid(
                  ssid, sizeof(ssid), config.ssids[request.payload].ssid);
              _bootstring->setText(ssid);
            } else if (smallOledActive() && oled128x32Active()) {
              char ssid[startup128x32::ssidBufferSize];
              voxone::oled128x32::formatStartupSsid(
                  ssid, sizeof(ssid), config.ssids[request.payload].ssid);
              _bootstring->setText(ssid);
            } else if (smallOledActive())
              _bootstring->setText(config.ssids[request.payload].ssid);
            else
#endif
              _bootstring->setText(config.ssids[request.payload].ssid,
                                   LANG::bootstrFmt);
          }
          break;
        }
        case DSPRSSI:
          if(_rssi) _setRSSI(request.payload);
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
          if(smallOledActive() && oled128x32PlayerStatus)
            oled128x32PlayerStatus->setWifiRssi(request.payload);
#endif
          break;
        case PSTART:
          _layoutChange(activeSourceVuVisible());
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
          if (_renderedMode == PLAYER) _station();
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
          _station();
          if (!smallOledActive()) _updatePlaybackStatus();
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76 || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320 || VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
          if(_renderedMode==STATIONS && _plplaying) _plplaying->setText(uiState.selectedStation() == config.lastStation() ? "GRA" : "");
#endif
          break;
        case PSTOP:
          _layoutChange(activeSourceVuVisible());
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
          if (_renderedMode == PLAYER) _station();
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
          _station();
          if (!smallOledActive()) _updatePlaybackStatus();
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76 || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320 || VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
          if(_renderedMode==STATIONS && _plplaying) _plplaying->setText("");
#endif
          break;
        case DSP_START: _start();  break;
        case NEWIP: {
          #ifndef HIDE_IP
            if(_volip) {
              if (smallOledActive())
                _volip->setText(config.ipToStr(WiFi.localIP()));
              else
                _volip->setText(config.ipToStr(WiFi.localIP()), iptxtFmt);
            }
          #endif
          break;
        }
        default: break;

        // check if there are more messages waiting in the Q, in this case break the loop() and go
        // for another round to evict next message, do not waste time to redraw the screen, etc...
        if (uxQueueMessagesWaiting(displayQueue))
          return;
      }
  }

  uint32_t now = millis();
  if((uint32_t)(now - _lastVolumeDraw) >= DISPLAY_VOLUME_INTERVAL_MS){
    bool drawVolume = false;
    portENTER_CRITICAL(&displayVolumeMux);
    if(_volumePending){
      _volumePending = false;
      drawVolume = true;
    }
    portEXIT_CRITICAL(&displayVolumeMux);
    if(drawVolume){
      _lastVolumeDraw = now;
      _volume();
    }
  }

#if VOXONE_DISPLAY_COMPILES_ST7796
  if (_yamahaTftActive()) render_yamaha_tft();
#endif
  dsp.loop();
/*
  #if I2S_DOUT==255
  player.computeVUlevel();
  #endif
*/
}

void Display::_setRSSI(int rssi) {
  if(!_rssi) return;
#if RSSI_DIGIT
  _rssi->setText(rssi, rssiFmt);
  return;
#endif
  char rssiG[3];
  uint8_t level = playerWifiLevel(rssi);
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  PlayerDisplayView view{};
  capturePlayerDisplayView(view, rssi);
  level = view.wifiLevel;
#endif
  switch (level) {
    case 4: strlcpy(rssiG, "\004\006", sizeof(rssiG)); break;
    case 3: strlcpy(rssiG, "\004\005", sizeof(rssiG)); break;
    case 2: strlcpy(rssiG, "\004\002", sizeof(rssiG)); break;
    case 1: strlcpy(rssiG, "\003\002", sizeof(rssiG)); break;
    default: strlcpy(rssiG, "\001\002", sizeof(rssiG)); break;
  }
  _rssi->setText(rssiG);
}

void Display::_station() {
  _meta->setAlign(metaConf.widget.align);
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  PlayerDisplayView view{};
  capturePlayerDisplayView(view, WiFi.RSSI());
  _meta->setText(view.station);
#else
  _meta->setText(config.station.name);
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  if(_st7789Station) _st7789Station->setText(config.station.name);
#endif
}

char *split(char *str, const char *delim) {
  char *dmp = strstr(str, delim);
  if (dmp == NULL) return NULL;
  *dmp = '\0'; 
  return dmp + strlen(delim);
}

void Display::_title() {
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76 || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320 || VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if (!smallOledActive()) _updatePlaybackStatus();
#endif
  PlayerDisplayView view{};
  capturePlayerDisplayView(view, WiFi.RSSI());
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (smallOledActive() && oled128x32Active()) {
    char metadata[voxone::oled128x32::kMetadataBufferSize];
    voxone::oled128x32::formatMetadata(
        metadata, sizeof(metadata), view.artist, view.title);
    _title1->setText(metadata);
  } else
#endif
  {
    _title1->setText(view.artist);
    if (_title2) _title2->setText(view.title);
  }
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
  if (smallOledActive() && oled128x32Active() &&
      view.source == DisplaySourceKind::Bluetooth &&
      _oledBtTransportArtist && _oledBtTransportTitle &&
      _oled128x32BtTransport) {
    _oledBtTransportArtist->setText(view.artist);
    _oledBtTransportTitle->setText(view.title);
    _oled128x32BtTransport->setPlayback(view.playback);
  } else if (smallOledActive() && !oled128x32Active() &&
      view.source == DisplaySourceKind::Bluetooth &&
      _oledBtTransportArtist && _oledBtTransportTitle &&
      _oledBtTransportPlayback) {
    _oledBtTransportArtist->setText(view.artist);
    _oledBtTransportTitle->setText(view.title);
    _oledBtTransportPlayback->setText(displayPlaybackLabel(view.playback));
  }
#endif
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  // BT transport is a separate screen and keeps the original raw fields.
  if (!smallOledActive() && view.source == DisplaySourceKind::Bluetooth &&
      _btTransportArtist && _btTransportTitle) {
    DisplaySourceView source{};
    if (getDisplaySourceView && getDisplaySourceView(source)) {
      _btTransportArtist->setText(source.artist ? source.artist : "");
      _btTransportTitle->setText(source.title ? source.title : "");
    }
  }
#endif
  if (view.source != DisplaySourceKind::Bluetooth && player_on_track_change)
    player_on_track_change();
#else
  if (strlen(config.station.title) > 0) {
    const StationMetadataParts parts = parseStationMetadata(
        config.station.title, config.station.metadataMode == STATION_META_SWAP);
    char artist[BUFLEN];
    char title[BUFLEN];
    stationMetaCopy(artist, sizeof(artist), parts.artist, parts.artistLength);
    stationMetaCopy(title, sizeof(title), parts.title, parts.titleLength);
    const auto shown = normalizePlayerMetadataForDisplay(artist, title);
    if (_title2) {
      _title1->setText(shown.displayArtist);
      _title2->setText(shown.displayTitle);
    } else {
      char whole[BUFLEN + 1];
      stationMetaDisplay(config.station.title, config.station.metadataMode == STATION_META_SWAP,
                         whole, sizeof(whole));
      _title1->setText(whole);
    }
  } else {
    _title1->setText("");
    if (_title2) _title2->setText("");
  }
  if (player_on_track_change) player_on_track_change();
#endif
}

void Display::_time(bool redraw) {
  
#if LIGHT_SENSOR!=255
  if(config.runtimeSnapshot().dspOn) {
    config.setRuntimeBrightness(AUTOBACKLIGHT(analogRead(LIGHT_SENSOR)));
    config.setBrightness();
  }
#endif
  if(config.isScreensaver && network.timeinfo.tm_sec % 60 == 0){
#if defined(VOXONE_RUNTIME_L0_DISPLAY_BACKENDS)
    if (smallOledActive()) {
      const uint16_t clockHeight = 2 * CHARHEIGHT;
      const uint16_t maxTop = dsp.height() > clockHeight + 2
                                  ? dsp.height() - clockHeight - 1 : 1;
      const uint16_t ft = static_cast<uint16_t>(random(1, maxTop));
      const uint16_t maxLeft = dsp.width() > _clock->clockWidth() + 2
                                   ? dsp.width() - _clock->clockWidth() - 1 : 1;
      const uint16_t lt = static_cast<uint16_t>(random(1, maxLeft));
      _clock->moveTo({lt, ft, 0});
    } else {
#endif
    #if TIME_SIZE<19
      uint16_t ft=static_cast<uint16_t>(random(TFT_FRAMEWDT, (dsp.height()-TIME_SIZE*CHARHEIGHT-TFT_FRAMEWDT)));
    #else
      uint16_t ft=static_cast<uint16_t>(random(TFT_FRAMEWDT+TIME_SIZE, (dsp.height()-_clock->dateSize()-TFT_FRAMEWDT*2)));
    #endif
    uint16_t lt=static_cast<uint16_t>(random(TFT_FRAMEWDT, (dsp.width()-_clock->clockWidth()-TFT_FRAMEWDT)));
    if(clockConf.align==WA_CENTER) lt-=(dsp.width()-_clock->clockWidth())/2;
    //_clock->moveTo({clockConf.left, ft, 0});
    _clock->moveTo({lt, ft, 0});
#if defined(VOXONE_RUNTIME_L0_DISPLAY_BACKENDS)
    }
#endif
  }
  _clock->draw(redraw);
}

void Display::_volume() {
  const MuteOutputSnapshot output = player.outputSnapshot();
  _setVuVisibility(activeSourceVuVisible());
  if(_volbar) _volbar->setValue(output.userVolume);
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  if(_st7796Volume) {
    PlayerDisplayView view{};
    capturePlayerDisplayView(view, WiFi.RSSI());
    _st7796Volume->setVolume(view.userVolume, view.muted);
  }
#else
  #ifndef HIDE_VOL
    if(_voltxt) _voltxt->setText(output.userVolume, voltxtFmt);
  #endif
#endif
  if(_renderedMode==VOL) {
#if VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED || VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    PlayerDisplayView view{};
    capturePlayerDisplayView(view, WiFi.RSSI());
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
    if (smallOledActive() && oled128x32Active()) {
      char level[5];
      voxone::oled128x32::formatVolumePercent(
          level, sizeof(level), view.userVolume, view.muted);
      _meta->setText("");
      _nums->setText(level);
    } else
#endif
    {
#if VOXONE_DISPLAY_COMPILES_SMALL_OLED
      if (oled128x64Active()) {
        char level[5];
        voxone::oled128x32::formatVolumePercent(
            level, sizeof(level), view.userVolume, view.muted);
        _nums->setText(level);
      } else
#endif
      {
        if (view.muted) _nums->setText("MUTE");
        else _nums->setText(view.userVolume, numtxtFmt);
      }
    }
#else
    if (output.muted) _nums->setText("MUTE");
    else _nums->setText(output.userVolume, numtxtFmt);
#endif
  }
}

void Display::flip(){ if (_runtimeEnabled) dsp.flip(); }

void Display::invert(){ if (_runtimeEnabled) dsp.invert(); }

void  Display::setContrast(){}

bool Display::deepsleep(){
  if (!_runtimeEnabled) return true;
#if defined(DSP_OLED) || BRIGHTNESS_PIN!=255
  dsp.sleep();
  return true;
#endif
  return false;
}

void Display::wakeup(){
  if (!_runtimeEnabled) return;
#if defined(DSP_OLED) || BRIGHTNESS_PIN!=255
  dsp.wake();
#endif
}
//============================================================================================================================
#else // !DUMMYDISPLAY
//============================================================================================================================
void Display::init(){
  _createDspTask();
}
bool Display::supportsBluetoothTransportView() const { return false; }
void Display::_start(){
  config.setTitle(LANG::const_PlReady);
}

void Display::putRequest(displayRequestType_e type, int payload){
  if(type==DSP_START) _start();
  if(type==NEWMODE) transitionUiMode((displayMode_e)payload);
  if(type==RESETIDLE) resetUiReturnTimeout();
}
//============================================================================================================================
#endif // DUMMYDISPLAY
