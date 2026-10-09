#ifndef VOXONE_BT_LINK_PROTOCOL_H
#define VOXONE_BT_LINK_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "bt_ota_state.h"

enum class BtPlayback : uint8_t { Stopped, Playing, Paused };
enum class BtLinkEvent : uint8_t {
  Online, Offline, Diagnostics, UnsupportedProtocol, UpdateIdentity
};

struct BtLinkState {
  bool runtimeAvailable = false;
  uint8_t protocolVersion = 0;
  BtOtaState otaState = BtOtaState::Missing;
  char firmwareVersion[48]{};
  char btName[97]{};
  char capabilities[129]{};
  bool connected = false;
  char peerName[97]{};
  BtPlayback playback = BtPlayback::Stopped;
  char artist[193]{};
  char title[193]{};
  char album[193]{};
  int16_t volume = -1;
  uint32_t volumeRevision = 0;
  uint32_t sampleRate = 0;
  uint16_t rawVuLeft = 0;
  uint16_t rawVuRight = 0;
  uint32_t rawVuLastMs = 0;
};

struct BtLinkDiagnostics {
  char resetReason[48]{};
  uint32_t uptime = 0;
  uint32_t heap = 0;
  uint32_t minHeap = 0;
};

// No Arduino dependency: the same bounded parser runs on the MCU and in native tests.
class BtLinkProtocol {
 public:
  using SendCommand = void (*)(void* context, const char* command);
  using OnEvent = void (*)(void* context, BtLinkEvent event);
  using OnLine = bool (*)(void* context, const char* line, uint32_t nowMs);

  static constexpr size_t MaxLineLength = 256;
  static constexpr uint32_t ProbeIntervalMs = 2000;
  static constexpr uint32_t PingIntervalMs = 5000;
  static constexpr uint32_t OfflineTimeoutMs = 15000;

  BtLinkProtocol(SendCommand sendCommand, OnEvent onEvent, void* context);

  void begin(uint32_t nowMs);
  void setLineObserver(OnLine observer) { lineObserver_ = observer; }
  void setUpdateExclusive(bool exclusive) { updateExclusive_ = exclusive; }
  void suspend();
  void feed(char byte, uint32_t nowMs);
  void tick(uint32_t nowMs);
  void requestStatus(uint32_t nowMs);
  void requestDiag();
  void ping(uint32_t nowMs);
  bool play();
  bool pause();
  bool next();
  bool prev();
  bool setVolume(uint8_t absoluteVolume);

  const BtLinkState& state() const { return state_; }
  const BtLinkDiagnostics& diagnostics() const { return diagnostics_; }
  bool hasIncompleteOnlineSnapshot() const {
    return statusOpen_ && state_.runtimeAvailable;
  }

 private:
  bool handleLine(uint32_t nowMs);
  void clearSession();
  void goOffline();
  void send(const char* command);
  bool sendTransport(const char* command);
  void notify(BtLinkEvent event);

  SendCommand sendCommand_;
  OnEvent onEvent_;
  OnLine lineObserver_ = nullptr;
  bool updateExclusive_ = false;
  void* context_;
  BtLinkState state_{};
  BtLinkState statusBackup_{};
  BtLinkDiagnostics diagnostics_{};
  char line_[MaxLineLength + 1]{};
  size_t lineLength_ = 0;
  bool discardingLine_ = false;
  bool statusOpen_ = false;
  uint8_t statusIdentityFields_ = 0;
  bool statusOtaMalformed_ = false;
  bool diagnosticsOpen_ = false;
  bool protocolSeen_ = false;
  bool unsupportedProtocolReported_ = false;
  bool diagnosticsRequested_ = false;
  uint8_t diagnosticsFields_ = 0;
  uint32_t lastRxMs_ = 0;
  uint32_t lastProbeMs_ = 0;
  uint32_t lastPingMs_ = 0;
};

#endif
