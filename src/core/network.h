#ifndef network_h
#define network_h
#include <WiFi.h>
#include "first_boot_runtime_snapshot.h"
#include "network_ready_state.h"
#include "wifi_reassoc_policy.h"
#include <atomic>

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
    voxone::first_boot::RuntimeSnapshot presentationSnapshot() const {
      return presentationState.read();
    }
    void publishSecurityRecoveryAp(const char* ssid, const char* address,
                                   bool started) {
      presentationState.activateSecurityRecoveryAp(ssid, address, started);
    }
  private:
    voxone::first_boot::RuntimeSnapshotStore presentationState;
    voxone::network::NetworkReadyState readyState;
    voxone::wifi::ReassocPolicy reassocPolicy;
    portMUX_TYPE reassocMux = portMUX_INITIALIZER_UNLOCKED;
    bool staReconnectEnabled = false;
    std::atomic<bool> staEventsAccepted{false};
    std::atomic<uint8_t> activeStaProfile{0};
    std::atomic<uint8_t> activeStaAttempt{0};
    std::atomic<uint32_t> activeStaGeneration{0};
    void raiseSoftAP();
    void activateStaAttempt(uint8_t profileIndex, uint8_t attempt,
                            uint32_t generation);
    void deactivateStaAttempt();
    bool activeStaIdentity(uint8_t& profileIndex, uint8_t& attempt,
                           uint32_t& generation) const;
    bool activeStaIdentityStillMatches(uint8_t profileIndex,
                                       uint32_t generation) const;
    bool activeStaSsidMatches(uint8_t profileIndex) const;
    void armWifiReassociation(uint32_t nowMs);
    void resetWifiReassociation();
    static void WiFiLostConnection(WiFiEvent_t event, WiFiEventInfo_t info);
    static void WiFiReconnected(WiFiEvent_t event, WiFiEventInfo_t info);
};

extern MyNetwork network;

extern __attribute__((weak)) void network_on_connect();

#endif
