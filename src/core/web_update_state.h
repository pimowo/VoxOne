#ifndef VOXONE_WEB_UPDATE_STATE_H
#define VOXONE_WEB_UPDATE_STATE_H

#include <atomic>
#include <stdint.h>

// Shared by the HTTP and Wi-Fi event tasks during an update/restart.
class WebUpdateState {
public:
  enum Phase : uint8_t { Ready, FilesystemUnavailable, RestartPending };

  void filesystemUnmounting() { phase_.store(FilesystemUnavailable); }
  void restartScheduled() { phase_.store(RestartPending); }
  void updateFailed() { phase_.store(Ready); }

  bool blocksRequests() const { return phase_.load() != Ready; }
  bool restartPending() const { return phase_.load() == RestartPending; }
  bool filesystemUnavailable() const { return phase_.load() == FilesystemUnavailable; }

private:
  std::atomic<uint8_t> phase_{Ready};
};

#endif
