#ifndef player_h
#define player_h

#include "mute_state.h"
#include "radio_source_policy.h"
#include "temporary_audio_state.h"

#include "../audioI2S/AudioEx.h"

#ifndef MQTT_BURL_SIZE
  #define MQTT_BURL_SIZE  512
#endif

#ifndef PLQ_SEND_DELAY
  #define PLQ_SEND_DELAY pdMS_TO_TICKS(1000) //portMAX_DELAY
#endif

enum playerRequestType_e : uint8_t { PR_PLAY = 1, PR_STOP = 2, PR_PREV = 3, PR_NEXT = 4, PR_VOL = 5, PR_VUTONUS = 7, PR_BURL = 8, PR_TOGGLE = 9, PR_RADIO_SUSPEND = 10, PR_RADIO_RESUME = 11 };
struct playerRequestParams_t
{
  playerRequestType_e type;
  int payload;
};

enum plStatus_e : uint8_t{ PLAYING = 1, STOPPED = 2 };

class Player: public Audio {
  private:
    uint32_t    _volTicks;   /* delayed volume save  */
    bool        _volTimer;   /* delayed volume save  */
    volatile uint8_t _pendingVolume;
    volatile uint8_t _pendingMode; // 0 RAW, 1 USER, 2 committed state, 3 output-only refresh
    volatile bool    _volumePending;
    volatile uint8_t _pendingMaximum;
    volatile bool _maximumPending;
    plStatus_e  _status;
    MuteState _mute;
    TemporaryAudioState _temporary;
    uint32_t _callbackToken = 0;
    QueueHandle_t _temporaryUrls = nullptr;
    //char        _plError[PLERR_LN];
  private:
    void _stop(bool alreadyStopped = false,
               RadioStopReason reason = RadioStopReason::Normal);
    void _play(uint16_t stationId, bool sourceResume = false);
    void _loadVol(uint8_t volume);
    bool _hasError;
    void browseUrl(const char* url);
    void finishTemporary();
    void restoreTemporaryBase();
  public:
    bool lockOutput = true;
    volatile bool connproc = true;
  public:
    Player();
    void init();
    void loop();
    void setError();
    void setError(const char *e);
    //bool hasError() { return strlen(_plError)>0; }
    void sendCommand(playerRequestParams_t request);
    void resetQueue();
    void requestTemporaryUrl(const char* url, size_t length);
    bool temporaryActive() const { return _temporary.active(); }
    bool temporaryBusy() const { return _temporary.busy(); }
    bool temporaryEof();
    bool interruptTemporaryForNetwork();
    bool remoteStationName = false;
    plStatus_e status() { return _status; }
    void prev();
    void next();
    void toggle();
    void stepVol(bool up);
    void setVol(uint8_t volume);
    void setUserVol(uint8_t user);
    void stepUserVol(int8_t direction);
    bool isMuted() const { return _mute.active(); }
    bool outputSilent() const;
    void setMuted(bool muted);
    void toggleMute() { setMuted(!isMuted()); }
    void applyCurrentVolume();
    uint8_t pendingRawAtMax(uint8_t maximum);
    uint8_t rawNotLouderThan(uint8_t candidate, uint8_t maximum, uint8_t output);
    void requestMaximumVolume(uint8_t maximum);
    uint8_t volToI2S(uint8_t volume);
    void stopInfo();
    void setOutputPins(bool isPlaying);
};

extern Player player;

extern __attribute__((weak)) void player_on_start_play();
extern __attribute__((weak)) void player_on_stop_play();
extern __attribute__((weak)) void player_on_track_change();
extern __attribute__((weak)) void player_on_station_change();

#endif
