#ifndef VOXONE_NETWORK_READY_STATE_H
#define VOXONE_NETWORK_READY_STATE_H

#include <atomic>
#include <stdint.h>

namespace voxone::network {

struct Ipv4ReadinessEvidence {
  constexpr Ipv4ReadinessEvidence(bool gotIpEventValue = false,
                                  uint32_t eventIpValue = 0,
                                  uint32_t localIpValue = 0,
                                  bool netifAvailableValue = false,
                                  bool netifUpValue = false,
                                  bool netifIpReadableValue = false,
                                  uint32_t netifIpValue = 0)
      : gotIpEvent(gotIpEventValue), eventIp(eventIpValue),
        localIp(localIpValue), netifAvailable(netifAvailableValue),
        netifUp(netifUpValue), netifIpReadable(netifIpReadableValue),
        netifIp(netifIpValue) {}

  bool gotIpEvent = false;
  uint32_t eventIp = 0;
  uint32_t localIp = 0;
  bool netifAvailable = false;
  bool netifUp = false;
  bool netifIpReadable = false;
  uint32_t netifIp = 0;
};

inline bool validIpv4Readiness(const Ipv4ReadinessEvidence& evidence) {
  if (!evidence.gotIpEvent || evidence.eventIp == 0 ||
      evidence.localIp == 0 || evidence.eventIp != evidence.localIp)
    return false;
  if (!evidence.netifAvailable) return true;
  return evidence.netifUp && evidence.netifIpReadable &&
         evidence.netifIp != 0 && evidence.eventIp == evidence.netifIp;
}

class NetworkReadyState {
 public:
  void disconnected() { ready_.store(false, std::memory_order_release); }

  bool gotIp(const Ipv4ReadinessEvidence& evidence) {
    const bool ready = validIpv4Readiness(evidence);
    ready_.store(ready, std::memory_order_release);
    return ready;
  }

  bool ready() const { return ready_.load(std::memory_order_acquire); }

 private:
  std::atomic<bool> ready_{false};
};

}  // namespace voxone::network

#endif
