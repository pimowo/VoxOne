#include "Arduino.h"
#include <ctype.h>
#include "options.h"
#include "WiFi.h"
#include "time.h"
#include "config.h"
#include "display.h"
#include "display_audio_info.h"
#include "dac_mute.h"
#include "eq_preset_label.h"
#include "player.h"
#include "volume_map.h"
#include "station_metadata.h"
#include "network.h"
#include "netserver.h"
#include "system_operation_state.h"
#include "update_progress.h"
#include "update_bar_render_state.h"
#include "timekeeper.h"
#include "ui_timeout_config.h"
#if DSP_MODEL==DSP_ST7796 && VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
#include "source_manager.h"
#endif
#include "../displays/dspcore.h"
#include "../displays/widgets/widgets.h"
#include "../displays/widgets/pages.h"
#include "../displays/tools/l10n.h"

Display display;
static TaskHandle_t displayTaskHandle = nullptr;

#ifndef CORE_STACK_SIZE
  #define CORE_STACK_SIZE  1024*4
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

#if DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7796
constexpr uint32_t X0_UI_RETURN_TIMEOUT_S = 10;
#endif

static void loopDspTask(void * pvParameters){
  while(true){
  #ifndef DUMMYDISPLAY
    if(displayQueue==NULL) break;
    if(timekeeper.loop0()){
      display.loop();
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
#if defined(VOXONE_PROFILE_X0) || defined(VOXONE_PROFILE_A0)
constexpr uint16_t kDisplayVolumeMax = 100;
static uint8_t displayedVolume() { return config.userVolume; }
#else
constexpr uint16_t kDisplayVolumeMax = 254;
static uint8_t displayedVolume() { return config.store.volume; }
#endif

DspCore dsp;

#if DSP_MODEL==DSP_ST7796
class A0UpdateProgressWidget : public Widget {
 public:
  A0UpdateProgressWidget() {
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

class A0VolumeWidget : public Widget {
 public:
  A0VolumeWidget(WidgetConfig position, uint16_t color, uint16_t background) {
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

class A0LabelFrameWidget : public Widget {
 public:
  A0LabelFrameWidget(FillConfig frame, uint16_t color, uint16_t background,
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

class A0PlaybackIconWidget : public Widget {
 public:
  A0PlaybackIconWidget(FillConfig frame, uint16_t color, uint16_t background)
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

class A0BluetoothWidget : public Widget {
 public:
  A0BluetoothWidget(WidgetConfig position, uint16_t color, uint16_t background) {
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

Page *pages[] = { new Page(), new Page(), new Page(), new Page() };

#if !(DSP_MODEL==DSP_ST7789 || DSP_MODEL==DSP_ST7796)
  #undef  BITRATE_FULL
  #define BITRATE_FULL     false
#endif


void returnPlayer(){
  display.putRequest(NEWMODE, PLAYER);
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
#if LIGHT_SENSOR!=255
  analogSetAttenuation(ADC_0db);
#endif
  _bootStep = 0;
#if defined(VOXONE_PROFILE_A0) && DSP_MODEL==DSP_ST7796
  _a0PlayerReady = false;
#endif
  _volumePending = false;
  _volumeModePending = false;
  _lastVolumeDraw = millis() - DISPLAY_VOLUME_INTERVAL_MS;
  dsp.initDisplay();
  displayQueue=NULL;
  displayQueue = xQueueCreate( 5, sizeof( requestParams_t ) );
  while(displayQueue==NULL){;}
  _createDspTask();
  while(!_bootStep==0) { delay(10); }
  //_pager.begin();
  //_bootScreen();
  _pager = new Pager();
  _footer = new Page();
#if DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7796
  _plwidget = nullptr;
#else
  _plwidget = new PlayListWidget();
#endif
  _nums = new NumWidget();
  _clock = new ClockWidget();
  _meta = new ScrollWidget();
  _title1 = new ScrollWidget();
  _plcurrent = new ScrollWidget();
  Serial.println("done");
}

uint16_t Display::width(){ return dsp.width(); }
uint16_t Display::height(){ return dsp.height(); }
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
  _boot->addWidget(new ProgressWidget(bootWdtConf, bootPrgConf, config.theme.title2, config.theme.background));
  _bootstring = (TextWidget*) &_boot->addWidget(new TextWidget(bootstrConf, 50, config.theme.meta, config.theme.background));
  _pager->addPage(_boot);
  _pager->setPage(_boot, true);
  dsp.drawLogo(bootLogoTop);
  _bootStep = 1;
}

void Display::_buildPager(){
  _meta->init("*", metaConf, config.theme.meta, config.theme.metabg);
  #if DSP_MODEL==DSP_ST7789_76
  _x0Station = new ScrollWidget("*", x0StationConf, config.theme.meta, config.theme.metabg);
#endif
  _title1->init("*", title1Conf, config.theme.title1, config.theme.background);
  _clock->init(clockConf, 0, 0);
#if DSP_MODEL==DSP_ST7796
  _plcurrent->init("*", a0StationConf, config.theme.plcurrent, config.theme.plcurrentbg);
  #else
    _plcurrent->init("*", playlistConf, config.theme.plcurrent, config.theme.plcurrentbg);
  #endif
#if DSP_MODEL==DSP_ST7789_76
  _plheader = new TextWidget(playlistHeaderConf, 30, config.theme.meta, config.theme.metabg);
  _plcounter = new TextWidget(playlistCounterConf, 16, config.theme.meta, config.theme.background);
  _plplaying = new TextWidget(playlistPlayingConf, 8, config.theme.meta, config.theme.background);
  _plheader->setText("WEB - STACJA");
#elif DSP_MODEL==DSP_ST7796
  _plheader = new TextWidget(a0PlaylistHeaderConf, 30, config.theme.meta, config.theme.metabg);
  _plcounter = new TextWidget(a0PlaylistCounterConf, 16, config.theme.meta, config.theme.background);
  _plplaying = new TextWidget(a0PlaylistPlayingConf, 8, config.theme.meta, config.theme.background);
  _plheader->setText("WEB - STACJA");
#else
  _plwidget->init(_plcurrent);
    _plcurrent->moveTo({TFT_FRAMEWDT, (uint16_t)(_plwidget->currentTop()), (int16_t)playlistConf.width});
#endif
  #ifndef HIDE_TITLE2
    _title2 = new ScrollWidget("*", title2Conf, config.theme.title2, config.theme.background);
  #endif
#if defined(VOXONE_PROFILE_A0) && DSP_MODEL==DSP_ST7796
  const auto onTextChanged = [](void* context, uint8_t row) {
    static_cast<Display*>(context)->_a0ScrollTextChanged(row);
  };
  _meta->setChangeObserver(this, 0, onTextChanged);
  _title1->setChangeObserver(this, 1, onTextChanged);
  _title2->setChangeObserver(this, 2, onTextChanged);
#endif
#if DSP_MODEL==DSP_ST7789_76
    _plbackground = new FillWidget(playlBGConf, config.theme.metabg);
#else
    _plbackground = new FillWidget(playlBGConf, config.theme.plcurrentfill);
#endif
    #if DSP_INVERT_TITLE || defined(DSP_OLED)
      _metabackground = new FillWidget(metaBGConf, config.theme.metafill);
    #else
      _metabackground = new FillWidget(metaBGConfInv, config.theme.metafill);
    #endif
  #ifndef HIDE_VU
    _vuwidget = new VuWidget(vuConf, bandsConf, config.theme.vumax, config.theme.vumin, config.theme.background);
  #endif
  #ifndef HIDE_VOLBAR
    _volbar = new SliderWidget(volbarConf, config.theme.volbarin, config.theme.background, kDisplayVolumeMax, config.theme.volbarout);
  #endif
  #ifndef HIDE_VOL
#if DSP_MODEL==DSP_ST7796
    _a0Volume = new A0VolumeWidget(voltxtConf, config.theme.meta, config.theme.background);
    _a0Volume->setVolume(displayedVolume(), player.isMuted());
#else
    _voltxt = new TextWidget(voltxtConf, 10, config.theme.vol, config.theme.background);
#endif
  #endif
  #ifndef HIDE_IP
    _volip = new TextWidget(iptxtConf, 30, config.theme.ip, config.theme.background);
  #endif
  #ifndef HIDE_RSSI
    _rssi = new TextWidget(rssiConf, 20, config.theme.rssi, config.theme.background);
  #endif
#if DSP_MODEL==DSP_ST7796
  _a0Playback = new A0PlaybackIconWidget(a0PlaybackFrameConf, config.theme.meta,
                                               config.theme.background);
  _a0Loud = new A0LabelFrameWidget(a0LoudFrameConf, config.theme.meta,
                                         config.theme.background, true);
  _a0Loud->setLabel(displayLoudLabel(false));
  _a0Source = new A0LabelFrameWidget(a0SourceFrameConf, config.theme.meta,
                                           config.theme.background, true);
  _a0Source->setLabel("WEB");
  _a0Eq = new A0LabelFrameWidget(a0EqFrameConf, config.theme.meta,
                                       config.theme.background, true);
  _a0Eq->setLabel(eqPresetLabel(config.store.bass, config.store.middle, config.store.trebble));
  _a0Mode = new A0LabelFrameWidget(a0ModeFrameConf, config.theme.meta,
                                         config.theme.background, true);
  _a0Mode->setLabel(displayDlnaModeLabel(DisplayDlnaMode::Unavailable));
  _a0BluetoothIcon = new A0BluetoothWidget(a0BluetoothIconConf, config.theme.meta, config.theme.background);
#endif
#if DSP_MODEL==DSP_ST7789_76
  _x0Rssi = new TextWidget(x0RssiConf, 16, config.theme.rssi, config.theme.background);
  _x0Volume = new TextWidget(x0VolumeConf, 12, config.theme.vol, config.theme.background);
  _x0Clock = new TextWidget(x0ClockConf, 8, config.theme.clock, config.theme.background);
#endif
  _nums->init(numConf, 10, config.theme.digit, config.theme.background);
  
#if DSP_MODEL!=DSP_ST7796
  if(_volbar)   _footer->addWidget( _volbar);
  if(_voltxt)   _footer->addWidget( _voltxt);
  if(_volip)    _footer->addWidget( _volip);
  if(_rssi)     _footer->addWidget( _rssi);
#endif
  
#if DSP_MODEL==DSP_ST7789_76
  pages[PG_PLAYER]->addWidget(new FillWidget(x0StationBandConf, config.theme.metabg));
  pages[PG_PLAYER]->addWidget(_x0Station);
#else
  if(_metabackground) pages[PG_PLAYER]->addWidget( _metabackground);
  pages[PG_PLAYER]->addWidget(_meta);
#endif
  pages[PG_PLAYER]->addWidget(_title1);
  if(_title2) pages[PG_PLAYER]->addWidget(_title2);
#if DSP_MODEL==DSP_ST7796
  pages[PG_PLAYER]->addWidget(new FillWidget(a0LowerDividerConf, config.theme.div));
#endif
  #if BITRATE_FULL
    _fullbitrate = new BitrateWidget(fullbitrateConf, config.theme.bitrate, config.theme.background);
#if DSP_MODEL==DSP_ST7796
    _fullbitrate->setFrameWidth(fullbitrateWidth);
#endif
    pages[PG_PLAYER]->addWidget( _fullbitrate);

  #else
    _bitrate = new TextWidget(bitrateConf, 30, config.theme.bitrate, config.theme.background);
    pages[PG_PLAYER]->addWidget( _bitrate);
  #endif
  if(_vuwidget) pages[PG_PLAYER]->addWidget( _vuwidget);
#if DSP_MODEL==DSP_ST7789_76
  pages[PG_PLAYER]->addWidget(new FillWidget(x0DividerConf, config.theme.div));
  pages[PG_PLAYER]->addWidget(_x0Rssi);
  pages[PG_PLAYER]->addWidget(_x0Volume);
  pages[PG_PLAYER]->addWidget(_x0Clock);
#else
  pages[PG_PLAYER]->addWidget(_clock);
#if DSP_MODEL==DSP_ST7796
  if(_a0Volume) pages[PG_PLAYER]->addWidget(_a0Volume);
  pages[PG_PLAYER]->addWidget(_a0Playback);
  pages[PG_PLAYER]->addWidget(_a0Loud);
  pages[PG_PLAYER]->addWidget(_a0Eq);
  pages[PG_PLAYER]->addWidget(_a0Source);
  pages[PG_PLAYER]->addWidget(_a0Mode);
  pages[PG_PLAYER]->addWidget(_a0BluetoothIcon);
  if(_rssi) pages[PG_PLAYER]->addWidget(_rssi);
#else
  pages[PG_PLAYER]->addPage(_footer);
#endif
#endif
  pages[PG_SCREENSAVER]->addWidget(_clock);

  if(_metabackground) pages[PG_DIALOG]->addWidget( _metabackground);
  pages[PG_DIALOG]->addWidget(_meta);
  pages[PG_DIALOG]->addWidget(_nums);
#if DSP_MODEL==DSP_ST7796
  if(_volip) pages[PG_DIALOG]->addWidget(_volip);
  if(_volip) _volip->lock();
#endif
  
  #if DSP_MODEL!=DSP_ST7796
    pages[PG_DIALOG]->addPage(_footer);
  #endif
#if DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7796
#if DSP_MODEL==DSP_ST7789_76
  if(_plbackground) pages[PG_PLAYLIST]->addWidget(_plbackground);
#endif
  pages[PG_PLAYLIST]->addWidget(_plheader);
  pages[PG_PLAYLIST]->addWidget(_plcurrent);
  pages[PG_PLAYLIST]->addWidget(_plcounter);
  pages[PG_PLAYLIST]->addWidget(_plplaying);
#else
  if(_plbackground) {
    pages[PG_PLAYLIST]->addWidget( _plbackground);
    _plbackground->setHeight(_plwidget->itemHeight());
    _plbackground->moveTo({0,(uint16_t)(_plwidget->currentTop()-playlistConf.widget.textsize*2), (int16_t)playlBGConf.width});
  }
  pages[PG_PLAYLIST]->addWidget(_plcurrent);
  pages[PG_PLAYLIST]->addWidget(_plwidget);
#endif
  #if DSP_MODEL==DSP_ST7796
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
  #endif
  for(const auto& p: pages) _pager->addPage(p);
  #if DSP_MODEL==DSP_ST7796
  _pager->addPage(_btTransportPage);
  _a0UpdatePage = new Page();
  TextWidget* updateTitle = new TextWidget({0, 48, 3, WA_CENTER}, 32, 0xFFFF, 0x0000);
  updateTitle->setText("AKTUALIZACJA");
  _a0UpdatePage->addWidget(updateTitle);
  _a0UpdateTarget = new TextWidget({0, 112, 2, WA_CENTER}, 48, 0xFFFF, 0x0000);
  _a0UpdatePage->addWidget(_a0UpdateTarget);
  _a0UpdateBar = new A0UpdateProgressWidget();
  _a0UpdatePage->addWidget(_a0UpdateBar);
  _a0UpdatePercent = new TextWidget({0, 232, 3, WA_CENTER}, 12, 0xFFFF, 0x0000);
  _a0UpdatePage->addWidget(_a0UpdatePercent);
  _a0UpdateActivity = new TextWidget({0, 302, 1, WA_CENTER}, 80, 0xFFFF, 0x0000);
  _a0UpdatePage->addWidget(_a0UpdateActivity);
  _pager->addPage(_a0UpdatePage);
  #endif
#if defined(VOXONE_PROFILE_A0) && DSP_MODEL==DSP_ST7796
  _a0PlayerReady = true;
#endif
}

void Display::_apScreen() {
  if(_boot) _pager->removePage(_boot);
    _boot = new Page();
      #if DSP_INVERT_TITLE || defined(DSP_OLED)
      _boot->addWidget(new FillWidget(metaBGConf, config.theme.metafill));
      #else
      _boot->addWidget(new FillWidget(metaBGConfInv, config.theme.metafill));
      #endif
    ScrollWidget *bootTitle = (ScrollWidget*) &_boot->addWidget(new ScrollWidget("*", apTitleConf, config.theme.meta, config.theme.metabg));
    bootTitle->setText("VoxOne tryb AP");
    TextWidget *apname = (TextWidget*) &_boot->addWidget(new TextWidget(apNameConf, 30, config.theme.title1, config.theme.background));
    apname->setText(LANG::apNameTxt);
    TextWidget *apname2 = (TextWidget*) &_boot->addWidget(new TextWidget(apName2Conf, 30, config.theme.clock, config.theme.background));
    apname2->setText(apSsid);
    TextWidget *appass = (TextWidget*) &_boot->addWidget(new TextWidget(apPassConf, 30, config.theme.title1, config.theme.background));
    appass->setText(LANG::apPassTxt);
    TextWidget *appass2 = (TextWidget*) &_boot->addWidget(new TextWidget(apPass2Conf, 30, config.theme.clock, config.theme.background));
    appass2->setText(apPassword);
    ScrollWidget *bootSett = (ScrollWidget*) &_boot->addWidget(new ScrollWidget("*", apSettConf, config.theme.title2, config.theme.background));
    bootSett->setText(config.ipToStr(WiFi.softAPIP()), LANG::apSettFmt);
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
#if defined(VOXONE_PROFILE_A0) && DSP_MODEL==DSP_ST7796
  _a0ScrollMode(true);
#endif
  _mode = PLAYER;
  config.setTitle(LANG::const_PlReady);

  if(_vuwidget) _vuwidget->lock();
  if(_rssi)     _setRSSI(WiFi.RSSI());
  #ifndef HIDE_IP
    if(_volip) _volip->setText(config.ipToStr(WiFi.localIP()), iptxtFmt);
  #endif
#if DSP_MODEL==DSP_ST7789_76
  if(_volip) _volip->lock();
#endif
  _pager->setPage(pages[PG_PLAYER]);
  _volume();
  _station();
#if DSP_MODEL==DSP_ST7796
  _updatePlaybackStatus();
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
#if DSP_MODEL==DSP_ST7796
  if (_a0UpdateTarget) _a0UpdateTarget->setText(updateTargetDisplayName(snapshot.target));
  if (_a0UpdateActivity)
    _a0UpdateActivity->setText(updateActivityDisplayText(snapshot.activity));
  if (_a0UpdateBar) _a0UpdateBar->setProgress(progress, snapshot.acquisition);
  if (_a0UpdatePercent) {
    if (progress.determinate) _a0UpdatePercent->setText(progress.percent, "%d%%");
    else _a0UpdatePercent->setText("");
  }
#elif DSP_MODEL==DSP_ST7789_76
  if (_nums) {
    if (progress.determinate) _nums->setText(progress.percent, "%d%%");
    else _nums->setText("...");
  }
#else
  (void)progress;
#endif
}

void Display::_swichMode(displayMode_e newmode) {
  if (!updateScreenAllowsMode(updateProgress(), newmode == UPDATING) ||
      newmode == _mode || (network.status != CONNECTED && newmode != UPDATING)) return;
#if DSP_MODEL==DSP_ST7796
  if (newmode == BT_TRANSPORT) {
    DisplaySourceView source{};
    if (!getDisplaySourceView || !getDisplaySourceView(source) ||
        source.kind != DisplaySourceKind::Bluetooth || !source.connected) return;
  }
#endif
#if defined(VOXONE_PROFILE_A0) && DSP_MODEL==DSP_ST7796
  if (_mode == PLAYER && newmode != PLAYER) _a0ScrollMode(false);
  else if (_mode != PLAYER && newmode == PLAYER) _a0ScrollMode(true);
#endif
  if (_mode == STATIONS || _mode == BT_TRANSPORT || newmode == UPDATING)
    timekeeper.cancelReturnPlayer();
  _mode = newmode;
#if DSP_MODEL==DSP_ST7789_76
  if(_volip) _volip->lock(newmode == PLAYER);
  if(_rssi) _rssi->lock(newmode == VOL);
#endif
#if DSP_MODEL==DSP_ST7796
  if(_volip) _volip->lock(newmode != VOL);
#endif
  dsp.setScrollId(NULL);
  if (newmode == PLAYER) {
    if(activeSourceVuVisible())
      if(clockMove.width<0) _clock->moveBack(); else _clock->moveTo(clockMove);
    else
      _clock->moveBack();
    numOfNextStation = 0;
    #ifdef META_MOVE
      _meta->moveBack();
    #endif
    _meta->setAlign(metaConf.widget.align);
    _station();
#if DSP_MODEL==DSP_ST7789_76
    _x0Station->setText(config.station.name);
#endif
    _nums->setText("");
    config.isScreensaver = false;
    _pager->setPage(pages[PG_PLAYER]);
#if DSP_MODEL==DSP_ST7796
    _updatePlaybackStatus();
#endif
    config.setDspOn(config.store.dspon, false);
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
#if DSP_MODEL==DSP_ST7789_76
    timekeeper.waitAndReturnPlayer(X0_UI_RETURN_TIMEOUT_S);
#elif DSP_MODEL==DSP_ST7796
    timekeeper.waitAndReturnPlayer(3);
#endif
    #ifndef HIDE_IP
      _showDialog(LANG::const_DlgVolume);
    #else
      _showDialog(config.ipToStr(WiFi.localIP()));
    #endif
    if (player.isMuted()) _nums->setText("MUTE");
    else _nums->setText(displayedVolume(), numtxtFmt);
  }
  if (newmode == LOST)      _showDialog(LANG::const_DlgLost);
  if (newmode == UPDATING) {
#if DSP_MODEL==DSP_ST7796
    _pager->setPage(_a0UpdatePage, true);
#else
    _showDialog(LANG::const_DlgUpdate);
#endif
  }
  if (newmode == SLEEPING)  _showDialog("SLEEPING");
  if (newmode == NUMBERS) _showDialog("");
  if (newmode == STATIONS) {
    _pager->setPage( pages[PG_PLAYLIST]);
    _plcurrent->setText("");
    currentPlItem = config.lastStation();
    _drawPlaylist();
  }
#if DSP_MODEL==DSP_ST7796
  if (newmode == BT_TRANSPORT) {
    timekeeper.waitAndReturnPlayerForMode(BT_TRANSPORT, uiTimeoutConfig().btTransportSeconds);
    _title();
    _pager->setPage(_btTransportPage);
  }
#endif
  
}

void Display::resetQueue(){
  if(displayQueue!=NULL) xQueueReset(displayQueue);
}

void Display::_drawPlaylist() {
#if DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7796
  const uint16_t total = config.playlistLength();
  if(total == 0) {
    _plcurrent->setText("BRAK STACJI");
    _plcounter->setText("0/0");
    _plplaying->setText("");
  } else {
    if(currentPlItem < 1 || currentPlItem > total) currentPlItem = 1;
    _plcurrent->setText(config.stationByNum(currentPlItem));
    char counter[16];
    snprintf(counter, sizeof(counter), "%u/%u", currentPlItem, total);
    _plcounter->setText(counter);
    _plplaying->setText(player.isRunning() && currentPlItem == config.lastStation() ? "GRA" : "");
  }
#else
  _plwidget->drawPlaylist(currentPlItem);
#endif
#if DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7796
  timekeeper.waitAndReturnPlayerForMode(STATIONS, uiTimeoutConfig().stationListSeconds);
#else
  timekeeper.waitAndReturnPlayer(30);
#endif
}

void Display::_drawNextStationNum(uint16_t num) {
  timekeeper.waitAndReturnPlayer(30);
  _meta->setText(config.stationByNum(num));
  _nums->setText(num, "%d");
}

void Display::putRequest(displayRequestType_e type, int payload){
  if(displayQueue==NULL) return;
  if (updateLockActive() && !(type == NEWMODE && payload == UPDATING)) return;
  requestParams_t request;
  request.type = type;
  request.payload = payload;
  if(type == DRAWVOL){
    portENTER_CRITICAL(&displayVolumeMux);
    _volumePending = true;
    portEXIT_CRITICAL(&displayVolumeMux);
    return;
  }
  bool volumeModeRequest = type == NEWMODE && payload == VOL;
  if(volumeModeRequest){
    bool skipRequest;
    portENTER_CRITICAL(&displayVolumeMux);
    skipRequest = _volumeModePending || _mode == VOL;
    if(!skipRequest) _volumeModePending = true;
    portEXIT_CRITICAL(&displayVolumeMux);
    if(skipRequest) return;
  }
  if(xQueueSend(displayQueue, &request, DSQ_SEND_DELAY) != pdPASS && volumeModeRequest){
    portENTER_CRITICAL(&displayVolumeMux);
    _volumeModePending = false;
    portEXIT_CRITICAL(&displayVolumeMux);
    Serial.println("##ERROR#:\tdisplay queue full for volume mode");
    return;
  }
}

void Display::_setVuVisibility(bool sourceVisible) {
  const bool lockVu = !displayVuUnlocked(
      config.store.vumeter, sourceVisible, config.userVolume);
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

#if DSP_MODEL==DSP_ST7796
void Display::_updatePlaybackStatus() {
  DisplaySourceView source{};
  const DisplayPlaybackState playback =
      getDisplaySourceView && getDisplaySourceView(source)
          ? source.playback
          : (player.isRunning() ? DisplayPlaybackState::Playing
                                : DisplayPlaybackState::Stopped);
  const char* label = displayPlaybackLabel(playback);
  _a0Playback->setState(playback);
  _btTransportPlayback->setText(label);
}
#endif

#if defined(VOXONE_PROFILE_A0) && DSP_MODEL==DSP_ST7796
void Display::_a0ScrollMode(bool playerMode) {
  ScrollWidget* rows[3] = {_meta, _title1, _title2};
  if (!_a0PlayerReady || !rows[0] || !rows[1] || !rows[2]) {
    _a0Scroll.leave();
    return;
  }
  if (playerMode) _a0Scroll.enter();
  else _a0Scroll.leave();
  for (ScrollWidget* row : rows) row->setExternallyScheduled(playerMode);
}

void Display::_a0ScrollTextChanged(uint8_t row) {
  ScrollWidget* rows[3] = {_meta, _title1, _title2};
  if (!a0PlayerScrollReady(_a0PlayerReady, _mode == PLAYER,
                              _a0Scroll.enabled(), rows[0], rows[1], rows[2])) return;
  for (ScrollWidget* widget : rows)
    if (dsp.getScrollId() == widget) dsp.setScrollId(NULL);
  _a0Scroll.textChanged(row);
}

void Display::_a0ScrollTick() {
  ScrollWidget* rows[3] = {_meta, _title1, _title2};
  if (!a0PlayerScrollReady(_a0PlayerReady, _mode == PLAYER,
                              _a0Scroll.enabled(), rows[0], rows[1], rows[2])) return;
  const bool needsScroll[3] = {
      rows[0]->scrollNeeded(), rows[1]->scrollNeeded(), rows[2]->scrollNeeded()};
  const A0PlayerScroll::Event event = _a0Scroll.tick(millis(), needsScroll);
  if (event.action == A0PlayerScroll::Action::Start) {
    rows[event.row]->startScheduledTurn();
  } else if (event.action == A0PlayerScroll::Action::Step &&
             rows[event.row]->stepScheduledTurn(A0PlayerScroll::kStepPixels)) {
    _a0Scroll.cycleFinished();
  }
}
#endif

void Display::loop() {
  if(_bootStep==0) {
    _pager->begin();
    _bootScreen();
    return;
  }
  if(displayQueue==NULL || (_locked && !updateLockActive())) return;
  const UpdateProgressSnapshot update = updateProgress();
  if (_bootStep == 2 && updateScreenOwnsDisplay(update)) {
    if (_mode != UPDATING) {
      resetQueue();
      _swichMode(UPDATING);
    }
    _updateUpdateScreen(update);
    requestParams_t ignored;
    xQueueReceive(displayQueue, &ignored, 0);
    _pager->loop();
    dsp.loop();
    return;
  }
  if (_bootStep == 2 && _mode == UPDATING && updateScreenReturnsToPlayer(update)) {
    resetQueue();
    _swichMode(PLAYER);
  }
#if DSP_MODEL==DSP_ST7789_76
  if(_mode == PLAYER && !systemUpdateAudioBlocked()) ScrollWidget::nextX0ScrollFrame();
#endif
#if defined(VOXONE_PROFILE_A0) && DSP_MODEL==DSP_ST7796
  if (_a0PlayerReady && _mode == PLAYER && !systemUpdateAudioBlocked()) _a0ScrollTick();
#endif
  _pager->loop();
#if DSP_MODEL==DSP_ST7796
  if (_bootStep == 2) {
    if (_a0Volume) _a0Volume->setDacMuted(dacMute.logicalMuted());
    if (_a0Eq)
      _a0Eq->setLabel(eqPresetLabel(config.store.bass, config.store.middle, config.store.trebble));
    if (_a0Source) {
      DisplaySourceView source{};
      _a0Source->setLabel(displaySourceLabel(
          getDisplaySourceView && getDisplaySourceView(source)
              ? source.kind : DisplaySourceKind::Radio));
    }
  }
#endif
#if DSP_MODEL==DSP_ST7796 && VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  if (_a0BluetoothIcon)
    _a0BluetoothIcon->setConnected(bluetoothPhysicallyConnected());
#endif
  requestParams_t request;
  if(xQueueReceive(displayQueue, &request, DSP_QUEUE_TICKS)){
    switch (request.type){
        case NEWMODE: {
          _swichMode((displayMode_e)request.payload);
          if(request.payload == VOL){
            portENTER_CRITICAL(&displayVolumeMux);
            _volumeModePending = false;
            portEXIT_CRITICAL(&displayVolumeMux);
          }
          break;
        }
        case RESETIDLE:
          if (_mode == BT_TRANSPORT)
            timekeeper.waitAndReturnPlayerForMode(BT_TRANSPORT, uiTimeoutConfig().btTransportSeconds);
          else if (_mode == STATIONS)
            timekeeper.waitAndReturnPlayerForMode(STATIONS, uiTimeoutConfig().stationListSeconds);
          break;
        case CLOSEPLAYLIST: player.sendCommand({PR_PLAY, request.payload}); break;
        case CLOCK: 
          if(_mode==PLAYER || _mode==SCREENSAVER) _time(request.payload==1); 
          break;
        case NEWTITLE:
          if (!systemUpdateAudioBlocked()) { _title(); _layoutChange(activeSourceVuVisible()); }
          break;
        case NEWSTATION:
          if (systemUpdateAudioBlocked()) break;
          _station();
#if DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7796
          if(_mode==STATIONS && _plplaying) _plplaying->setText(player.isRunning() && currentPlItem == config.lastStation() ? "GRA" : "");
#endif
          break;
        case NEXTSTATION: _drawNextStationNum(request.payload); break;
        case DRAWPLAYLIST: _drawPlaylist(); break;
        case DRAWVOL: _volume(); break;
        case DBITRATE: {
            DisplaySourceView source{};
            if (getDisplaySourceView) getDisplaySourceView(source);
            const DisplayAudioInfo info = selectDisplayAudioInfo(
                source, config.station.bitrate, config.configFmt);
            if (_fullbitrate) {
              if (info.bluetooth)
                _fullbitrate->setCustomText(info.top, info.bottom);
              else {
                _fullbitrate->setBitrate(info.radioBitrate);
                _fullbitrate->setFormat(info.radioFormat);
              }
            } else if (_bitrate) {
                char buf[20];
#if DSP_MODEL==DSP_ST7789_76
              formatDisplayAudioInfo(buf, sizeof(buf), source,
                                     config.station.bitrate, config.configFmt);
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
          if(_bootstring) _bootstring->setText(config.ssids[request.payload].ssid, LANG::bootstrFmt);
          break;
        }
        case DSPRSSI: if(_rssi){ _setRSSI(request.payload); } break;
        case PSTART:
          _layoutChange(activeSourceVuVisible());
#if DSP_MODEL==DSP_ST7796
          _station();
          _updatePlaybackStatus();
#endif
#if DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7796
          if(_mode==STATIONS && _plplaying) _plplaying->setText(currentPlItem == config.lastStation() ? "GRA" : "");
#endif
          break;
        case PSTOP:
          _layoutChange(activeSourceVuVisible());
#if DSP_MODEL==DSP_ST7796
          _station();
          _updatePlaybackStatus();
#endif
#if DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7796
          if(_mode==STATIONS && _plplaying) _plplaying->setText("");
#endif
          break;
        case DSP_START: _start();  break;
        case NEWIP: {
          #ifndef HIDE_IP
            if(_volip) _volip->setText(config.ipToStr(WiFi.localIP()), iptxtFmt);
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

  dsp.loop();
/*
  #if I2S_DOUT==255
  player.computeVUlevel();
  #endif
*/
}

void Display::_setRSSI(int rssi) {
#if DSP_MODEL==DSP_ST7789_76
  static bool x0RssiDisplayed = false;
  static int lastDisplayedRssi = 0;
  static uint32_t lastRssiDisplayMs = 0;
  const uint32_t now = millis();
  const int rssiDelta = rssi - lastDisplayedRssi;
  if(_x0Rssi && (!x0RssiDisplayed ||
      ((uint32_t)(now - lastRssiDisplayMs) >= 10000 &&
       (rssiDelta >= 2 || rssiDelta <= -2)))) {
    _x0Rssi->setText(rssi, "RSSI %ddBm");
    lastDisplayedRssi = rssi;
    lastRssiDisplayMs = now;
    x0RssiDisplayed = true;
  }
  if(_mode == VOL) return;
#endif
  if(!_rssi) return;
#if RSSI_DIGIT
  _rssi->setText(rssi, rssiFmt);
  return;
#endif
  char rssiG[3];
  int rssi_steps[] = {RSSI_STEPS};
  if(rssi >= rssi_steps[0]) strlcpy(rssiG, "\004\006", 3);
  if(rssi >= rssi_steps[1] && rssi < rssi_steps[0]) strlcpy(rssiG, "\004\005", 3);
  if(rssi >= rssi_steps[2] && rssi < rssi_steps[1]) strlcpy(rssiG, "\004\002", 3);
  if(rssi >= rssi_steps[3] && rssi < rssi_steps[2]) strlcpy(rssiG, "\003\002", 3);
  if(rssi <  rssi_steps[3] || rssi >=  0) strlcpy(rssiG, "\001\002", 3);
  _rssi->setText(rssiG);
}

void Display::_station() {
  _meta->setAlign(metaConf.widget.align);
#if DSP_MODEL==DSP_ST7796
  DisplaySourceView source{};
  if(getDisplaySourceView && getDisplaySourceView(source) && source.kind == DisplaySourceKind::Bluetooth) {
    _meta->setText(source.peerName ? source.peerName : "Bluetooth");
  } else {
    _meta->setText(player.isRunning() ? config.station.name : "WEB Radio");
  }
#else
  _meta->setText(config.station.name);
#endif
#if DSP_MODEL==DSP_ST7789_76
  if(_x0Station) _x0Station->setText(config.station.name);
#endif
}

char *split(char *str, const char *delim) {
  char *dmp = strstr(str, delim);
  if (dmp == NULL) return NULL;
  *dmp = '\0'; 
  return dmp + strlen(delim);
}

#if DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7796
static uint32_t displayNextCodepoint(const char*& text, const char* end) {
  const uint8_t first = static_cast<uint8_t>(*text++);
  if(first < 0x80) return first;
  const uint8_t extra = (first & 0xE0) == 0xC0 ? 1 :
                        (first & 0xF0) == 0xE0 ? 2 :
                        (first & 0xF8) == 0xF0 ? 3 : 0;
  if(extra == 0 || end - text < extra) return first;
  uint32_t codepoint = first & (0x7F >> extra);
  for(uint8_t i = 0; i < extra; ++i) {
    const uint8_t next = static_cast<uint8_t>(text[i]);
    if((next & 0xC0) != 0x80) return first;
    codepoint = (codepoint << 6) | (next & 0x3F);
  }
  text += extra;
  return codepoint;
}

static uint32_t displayFoldCodepoint(uint32_t value) {
  if(value >= 'A' && value <= 'Z') return value + ('a' - 'A');
  if((value >= 0xC0 && value <= 0xD6) || (value >= 0xD8 && value <= 0xDE) ||
     (value >= 0x410 && value <= 0x42F)) return value + 0x20;
  switch(value) {
    case 0x104: case 0x106: case 0x118: case 0x141:
    case 0x143: case 0x15A: case 0x179: case 0x17B:
      return value + 1;
    case 0x401: return 0x451;
    default: return value;
  }
}

static bool displayArtistIsStation(const char* artist, const char* station) {
  const char* artistEnd = artist + strlen(artist);
  const char* stationEnd = station + strlen(station);
  while(artist < artistEnd && isspace(static_cast<unsigned char>(*artist))) ++artist;
  while(station < stationEnd && isspace(static_cast<unsigned char>(*station))) ++station;
  while(artistEnd > artist && isspace(static_cast<unsigned char>(artistEnd[-1]))) --artistEnd;
  while(stationEnd > station && isspace(static_cast<unsigned char>(stationEnd[-1]))) --stationEnd;
  while(artist < artistEnd && station < stationEnd) {
    if(displayFoldCodepoint(displayNextCodepoint(artist, artistEnd)) !=
       displayFoldCodepoint(displayNextCodepoint(station, stationEnd))) return false;
  }
  return artist == artistEnd && station == stationEnd;
}
#endif

void Display::_title() {
#if DSP_MODEL==DSP_ST7796
  _updatePlaybackStatus();
  DisplaySourceView source{};
  if(getDisplaySourceView && getDisplaySourceView(source) && source.kind == DisplaySourceKind::Bluetooth) {
    _title1->setText(source.artist ? source.artist : "");
    if(_title2) _title2->setText(source.title ? source.title : "");
    _btTransportArtist->setText(source.artist ? source.artist : "");
    _btTransportTitle->setText(source.title ? source.title : "");
    return;
  }
#endif
  if (strlen(config.station.title) > 0) {
#if DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7796
    const StationMetadataParts parts = playerStationMetadata(
        config.station.title, config.station.metadataMode == STATION_META_SWAP);
#else
    const StationMetadataParts parts = parseStationMetadata(
        config.station.title, config.station.metadataMode == STATION_META_SWAP);
#endif
    char artist[BUFLEN];
    char title[BUFLEN];
    stationMetaCopy(artist, sizeof(artist), parts.artist, parts.artistLength);
    stationMetaCopy(title, sizeof(title), parts.title, parts.titleLength);
    if(parts.split && _title2){
#if DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7796
      _title1->setText(displayArtistIsStation(artist, config.station.name) ? "" : artist);
#else
      _title1->setText(artist);
#endif
      _title2->setText(title);
    }else{
#if DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7796
      if (_title2) {
        _title1->setText(artist);
        _title2->setText(displayArtistIsStation(title, config.station.name) ? "" : title);
      } else {
        _title1->setText(displayArtistIsStation(title, config.station.name) ? "" : title);
      }
#else
      char whole[BUFLEN + 1];
      stationMetaDisplay(config.station.title, config.station.metadataMode == STATION_META_SWAP,
                         whole, sizeof(whole));
      _title1->setText(whole);
      if(_title2) _title2->setText("");
#endif
    }
    
  }else{
    _title1->setText("");
    if(_title2) _title2->setText("");
  }
  if (player_on_track_change) player_on_track_change();
}

void Display::_time(bool redraw) {
  
#if LIGHT_SENSOR!=255
  if(config.store.dspon) {
    config.store.brightness = AUTOBACKLIGHT(analogRead(LIGHT_SENSOR));
    config.setBrightness();
  }
#endif
  if(config.isScreensaver && network.timeinfo.tm_sec % 60 == 0){
    #if TIME_SIZE<19
      uint16_t ft=static_cast<uint16_t>(random(TFT_FRAMEWDT, (dsp.height()-TIME_SIZE*CHARHEIGHT-TFT_FRAMEWDT)));
    #else
      uint16_t ft=static_cast<uint16_t>(random(TFT_FRAMEWDT+TIME_SIZE, (dsp.height()-_clock->dateSize()-TFT_FRAMEWDT*2)));
    #endif
    uint16_t lt=static_cast<uint16_t>(random(TFT_FRAMEWDT, (dsp.width()-_clock->clockWidth()-TFT_FRAMEWDT)));
    if(clockConf.align==WA_CENTER) lt-=(dsp.width()-_clock->clockWidth())/2;
    //_clock->moveTo({clockConf.left, ft, 0});
    _clock->moveTo({lt, ft, 0});
  }
  _clock->draw(redraw);
#if DSP_MODEL==DSP_ST7789_76
  if(_mode==PLAYER && _x0Clock) {
    char timeText[6];
    strftime(timeText, sizeof(timeText), "%H:%M", &network.timeinfo);
    _x0Clock->setText(timeText);
  }
#endif
}

void Display::_volume() {
  _setVuVisibility(activeSourceVuVisible());
  if(_volbar) _volbar->setValue(displayedVolume());
#if DSP_MODEL==DSP_ST7789_76
  if(_x0Volume) {
    char volumeText[12];
    _x0Volume->setText(displayX0VolumeText(displayedVolume(), player.isMuted(),
                                               volumeText, sizeof(volumeText)));
  }
#endif
#if DSP_MODEL==DSP_ST7796
  if(_a0Volume) _a0Volume->setVolume(displayedVolume(), player.isMuted());
#else
  #ifndef HIDE_VOL
    if(_voltxt) _voltxt->setText(displayedVolume(), voltxtFmt);
  #endif
#endif
  if(_mode==VOL) {
#if DSP_MODEL==DSP_ST7789_76
    timekeeper.waitAndReturnPlayer(X0_UI_RETURN_TIMEOUT_S);
#else
    timekeeper.waitAndReturnPlayer(3);
#endif
    if (player.isMuted()) _nums->setText("MUTE");
    else _nums->setText(displayedVolume(), numtxtFmt);
  }
}

void Display::flip(){ dsp.flip(); }

void Display::invert(){ dsp.invert(); }

void  Display::setContrast(){}

bool Display::deepsleep(){
#if defined(DSP_OLED) || BRIGHTNESS_PIN!=255
  dsp.sleep();
  return true;
#endif
  return false;
}

void Display::wakeup(){
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
void Display::_start(){
  config.setTitle(LANG::const_PlReady);
}

void Display::putRequest(displayRequestType_e type, int payload){
  if(type==DSP_START) _start();
  if(type==NEWMODE) mode((displayMode_e)payload);
}
//============================================================================================================================
#endif // DUMMYDISPLAY
