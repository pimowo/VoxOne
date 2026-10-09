#ifndef VOXONE_MQTT_UPDATE_SHUTDOWN_STATE_H
#define VOXONE_MQTT_UPDATE_SHUTDOWN_STATE_H

#include <atomic>

// RAM-only shutdown latch: timer, Wi-Fi and MQTT callbacks must not reconnect.
class MqttUpdateShutdownState {
public:
  bool begin() { return !requested_.exchange(true); }
  bool requested() const { return requested_.load(); }
  bool reconnectAllowed() const { return !requested(); }
  bool complete(bool tcpActive) const { return requested() && !tcpActive; }

private:
  std::atomic<bool> requested_{false};
};

#endif
