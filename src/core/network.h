#ifndef network_h
#define network_h
#include <WiFi.h>
#include "network_ready_state.h"
#include "wifi_reassoc_policy.h"

enum n_Status_e { CONNECTED, SOFT_AP, FAILED, DISCONNECTED };

class MyNetwork {
  public:
    n_Status_e status = FAILED;
    struct tm timeinfo;
    bool lostPlaying = false, beginReconnect = false;
    bool runtimeCallbacksEnabled = false;
  public:
    MyNetwork() {};
    void begin();
    void loop();
    void requestTimeSync(bool withSerialOutput=false);
    void setWifiParams();
    bool wifiBegin(bool silent=false);
    bool ready() const { return readyState.ready(); }
  private:
    voxone::network::NetworkReadyState readyState;
    voxone::wifi::ReassocPolicy reassocPolicy;
    portMUX_TYPE reassocMux = portMUX_INITIALIZER_UNLOCKED;
    bool staReconnectEnabled = false;
    void raiseSoftAP();
    void armWifiReassociation(uint32_t nowMs);
    void resetWifiReassociation();
    static void WiFiLostConnection(WiFiEvent_t event, WiFiEventInfo_t info);
    static void WiFiReconnected(WiFiEvent_t event, WiFiEventInfo_t info);
};

extern MyNetwork network;

extern __attribute__((weak)) void network_on_connect();

#endif
