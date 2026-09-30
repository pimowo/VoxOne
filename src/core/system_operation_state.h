#ifndef VOXONE_SYSTEM_OPERATION_STATE_H
#define VOXONE_SYSTEM_OPERATION_STATE_H

#include <atomic>
#include <stdint.h>

// Shared between system restart requests and Web Update filesystem handling.
class SystemOperationState {
public:
  enum Phase : uint8_t { Ready, Updating, FilesystemUnavailable, RestartPending };

  bool updateStarted() {
    if (audioBlocked_.load()) return false;
    uint8_t expected = Ready;
    if (!phase_.compare_exchange_strong(expected, Updating)) return false;
    audioBlocked_.store(true);
    radioStopped_.store(false);
    return true;
  }
  void filesystemUnmounting() { phase_.store(FilesystemUnavailable); }
  void restartRequested() { phase_.store(RestartPending); }
  void updateFailed() { phase_.store(Ready); }
  void awaitRadioStop() { radioStopped_.store(false); }
  void radioStopped() { radioStopped_.store(true); }
  bool isRadioStopped() const { return radioStopped_.load(); }
  bool releaseFailedUpdateAudio() {
    if (phase_.load() != Ready || !radioStopped_.load()) return false;
    audioBlocked_.store(false);
    return true;
  }
  bool audioBlocked() const { return audioBlocked_.load(); }

  bool blocksRequests() const { return phase_.load() != Ready; }
  bool restartPending() const { return phase_.load() == RestartPending; }
  bool filesystemUnavailable() const { return phase_.load() == FilesystemUnavailable; }

private:
  std::atomic<uint8_t> phase_{Ready};
  std::atomic<bool> audioBlocked_{false};
  std::atomic<bool> radioStopped_{false};
};

// Read from the RADIO decoder and BT I2S task without touching the web server.
bool systemUpdateAudioBlocked();
void systemUpdateRadioStopped();

inline bool wifiRecoveryAllowed(bool restartPending) { return !restartPending; }

#endif
