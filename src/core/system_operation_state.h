#ifndef VOXONE_SYSTEM_OPERATION_STATE_H
#define VOXONE_SYSTEM_OPERATION_STATE_H

#include <atomic>
#include <stdint.h>

// Shared between system restart requests and Web Update filesystem handling.
class SystemOperationState {
public:
  enum Phase : uint8_t { Ready, FilesystemUnavailable, RestartPending };

  void filesystemUnmounting() { phase_.store(FilesystemUnavailable); }
  void restartRequested() { phase_.store(RestartPending); }
  void updateFailed() { phase_.store(Ready); }

  bool blocksRequests() const { return phase_.load() != Ready; }
  bool restartPending() const { return phase_.load() == RestartPending; }
  bool filesystemUnavailable() const { return phase_.load() == FilesystemUnavailable; }

private:
  std::atomic<uint8_t> phase_{Ready};
};

inline bool wifiRecoveryAllowed(bool restartPending) { return !restartPending; }

#endif
