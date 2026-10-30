#ifndef VOXONE_TTS_HIL_DIAGNOSTICS_H
#define VOXONE_TTS_HIL_DIAGNOSTICS_H

#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)

#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace voxone {
namespace tts_hil {

enum class RunState : uint32_t {
  Idle = 0,
  Active,
  Terminal,
  Cleaned,
};

enum class EndReason : uint32_t {
  None = 0,
  NaturalEof,
  WatchdogTimeout,
  TransportError,
  ConnectFailed,
  NetworkInterrupted,
  UserStop,
  SourceSwitch,
  UpdateStop,
  Replaced,
  BackendStopped,
};

struct EventCounters {
  uint32_t starts = 0;
  uint32_t progress = 0;
  uint32_t watchdogTimeouts = 0;
  uint32_t naturalEofs = 0;
  uint32_t transportErrors = 0;
  uint32_t connectFailures = 0;
  uint32_t networkInterruptions = 0;
  uint32_t userStops = 0;
  uint32_t sourceSwitches = 0;
  uint32_t updateStops = 0;
  uint32_t replacements = 0;
  uint32_t backendStops = 0;
  uint32_t terminals = 0;
  uint32_t cleanups = 0;
  uint32_t ignoredTerminalEvents = 0;
};

struct TotalCounters {
  uint32_t runs = 0;
  uint32_t watchdogTimeouts = 0;
  uint32_t naturalEofs = 0;
  uint32_t cleanups = 0;
  uint32_t replacements = 0;
};

struct Snapshot {
  uint32_t revision = 0;
  uint32_t runId = 0;
  RunState state = RunState::Idle;
  EndReason endReason = EndReason::None;
  uint64_t bytesRead = 0;  // HTTP response body bytes for the current run.
  uint64_t transportBytesRead = 0;  // Headers, body and framing.
  uint32_t httpContentLength = 0;
  uint32_t startedAtMs = 0;
  uint32_t lastProgressAtMs = 0;
  uint32_t terminalAtMs = 0;
  uint32_t cleanupAtMs = 0;
  EventCounters events;
  TotalCounters totals;
};

inline const char* runStateName(RunState state) {
  switch (state) {
    case RunState::Idle: return "idle";
    case RunState::Active: return "active";
    case RunState::Terminal: return "terminal";
    case RunState::Cleaned: return "cleaned";
  }
  return "unknown";
}

inline const char* endReasonName(EndReason reason) {
  switch (reason) {
    case EndReason::None: return "none";
    case EndReason::NaturalEof: return "natural_eof";
    case EndReason::WatchdogTimeout: return "watchdog_timeout";
    case EndReason::TransportError: return "transport_error";
    case EndReason::ConnectFailed: return "connect_failed";
    case EndReason::NetworkInterrupted: return "network_interrupted";
    case EndReason::UserStop: return "user_stop";
    case EndReason::SourceSwitch: return "source_switch";
    case EndReason::UpdateStop: return "update_stop";
    case EndReason::Replaced: return "replaced";
    case EndReason::BackendStopped: return "backend_stopped";
  }
  return "unknown";
}

// One Player task writes the run record. The Wi-Fi event task only publishes
// a one-word notification, consumed by Player before cleanup. HTTP readers use
// a seqlock snapshot; the audio path never waits for a reader or allocates.
class Diagnostics {
 public:
  static constexpr size_t kJsonCapacity = 1536;

  void start(uint32_t token, uint32_t nowMs, uint64_t transportBytes,
             uint64_t httpBodyBytes) {
    if (!token) return;
    beginWrite();
    transportBase_ = transportBytes;
    lastTransport_ = transportBytes;
    httpBodyBase_ = httpBodyBytes;
    lastHttpBody_ = httpBodyBytes;
    runId_.store(token, std::memory_order_relaxed);
    state_.store(value(RunState::Active), std::memory_order_relaxed);
    endReason_.store(value(EndReason::None), std::memory_order_relaxed);
    storeBytes(0);
    storeTransportBytes(0);
    httpContentLength_.store(0, std::memory_order_relaxed);
    startedAtMs_.store(nowMs, std::memory_order_relaxed);
    lastProgressAtMs_.store(nowMs, std::memory_order_relaxed);
    terminalAtMs_.store(0, std::memory_order_relaxed);
    cleanupAtMs_.store(0, std::memory_order_relaxed);
    resetRunEvents();
    starts_.store(1, std::memory_order_relaxed);
    totalRuns_.fetch_add(1, std::memory_order_relaxed);
    endWrite();
  }

  void observeProgress(uint32_t token, uint32_t nowMs,
                       uint64_t transportBytes, uint64_t httpBodyBytes,
                       uint32_t httpContentLength) {
    if (!matchesCurrent(token) ||
        (transportBytes == lastTransport_ &&
         httpBodyBytes == lastHttpBody_ &&
         (!httpContentLength ||
          httpContentLength_.load(std::memory_order_relaxed) ==
              httpContentLength))) return;
    beginWrite();
    updateProgressLocked(transportBytes, httpBodyBytes, httpContentLength,
                         nowMs);
    endWrite();
  }

  void naturalEof(uint32_t token, uint32_t nowMs, uint64_t transportBytes,
                  uint64_t httpBodyBytes, uint32_t httpContentLength,
                  bool accepted) {
    terminal(token, EndReason::NaturalEof, nowMs, transportBytes,
             httpBodyBytes, httpContentLength, accepted);
  }

  void watchdogTimeout(uint32_t token, uint32_t nowMs,
                       uint64_t transportBytes, uint64_t httpBodyBytes,
                       uint32_t httpContentLength) {
    terminal(token, EndReason::WatchdogTimeout, nowMs, transportBytes,
             httpBodyBytes, httpContentLength, true);
  }

  void transportError(uint32_t token, uint32_t nowMs,
                      uint64_t transportBytes, uint64_t httpBodyBytes,
                      uint32_t httpContentLength, bool accepted) {
    terminal(token, EndReason::TransportError, nowMs, transportBytes,
             httpBodyBytes, httpContentLength, accepted);
  }

  void connectFailed(uint32_t token, uint32_t nowMs, uint64_t transportBytes,
                     uint64_t httpBodyBytes, uint32_t httpContentLength,
                     bool accepted) {
    terminal(token, EndReason::ConnectFailed, nowMs, transportBytes,
             httpBodyBytes, httpContentLength, accepted);
  }

  void userStop(uint32_t token, uint32_t nowMs, uint64_t transportBytes,
                uint64_t httpBodyBytes, uint32_t httpContentLength) {
    terminal(token, EndReason::UserStop, nowMs, transportBytes,
             httpBodyBytes, httpContentLength, true);
  }

  void sourceSwitch(uint32_t token, uint32_t nowMs,
                    uint64_t transportBytes, uint64_t httpBodyBytes,
                    uint32_t httpContentLength) {
    terminal(token, EndReason::SourceSwitch, nowMs, transportBytes,
             httpBodyBytes, httpContentLength, true);
  }

  void updateStop(uint32_t token, uint32_t nowMs, uint64_t transportBytes,
                  uint64_t httpBodyBytes, uint32_t httpContentLength) {
    terminal(token, EndReason::UpdateStop, nowMs, transportBytes,
             httpBodyBytes, httpContentLength, true);
  }

  void replaced(uint32_t token, uint32_t nowMs, uint64_t transportBytes,
                uint64_t httpBodyBytes, uint32_t httpContentLength) {
    terminal(token, EndReason::Replaced, nowMs, transportBytes,
             httpBodyBytes, httpContentLength, true);
  }

  // Called from the Wi-Fi event task. Publishing is wait-free and does not
  // access the non-atomic Audio transport counter.
  void notifyNetworkInterrupted(uint32_t token, bool accepted) {
    if (!token || !accepted) return;
    pendingNetworkToken_.store(token, std::memory_order_release);
  }

  // Called only by Player. Returns true if a current-run notification was
  // consumed; stale notifications are discarded without changing the run.
  bool consumeNetworkInterrupted(uint32_t nowMs, uint64_t transportBytes,
                                 uint64_t httpBodyBytes,
                                 uint32_t httpContentLength) {
    const uint32_t token =
        pendingNetworkToken_.exchange(0, std::memory_order_acq_rel);
    if (!token || !matchesCurrent(token)) return false;
    terminal(token, EndReason::NetworkInterrupted, nowMs, transportBytes,
             httpBodyBytes, httpContentLength, true);
    return true;
  }

  void cleanup(uint32_t token, uint32_t nowMs, uint64_t transportBytes,
               uint64_t httpBodyBytes, uint32_t httpContentLength) {
    if (!matchesCurrent(token) ||
        state_.load(std::memory_order_relaxed) == value(RunState::Cleaned)) {
      return;
    }
    beginWrite();
    updateProgressLocked(transportBytes, httpBodyBytes, httpContentLength,
                         nowMs);
    if (endReason_.load(std::memory_order_relaxed) == value(EndReason::None)) {
      endReason_.store(value(EndReason::BackendStopped),
                       std::memory_order_relaxed);
      terminalAtMs_.store(nowMs, std::memory_order_relaxed);
      backendStops_.fetch_add(1, std::memory_order_relaxed);
      terminals_.fetch_add(1, std::memory_order_relaxed);
    }
    state_.store(value(RunState::Cleaned), std::memory_order_relaxed);
    cleanupAtMs_.store(nowMs, std::memory_order_relaxed);
    cleanups_.fetch_add(1, std::memory_order_relaxed);
    totalCleanups_.fetch_add(1, std::memory_order_relaxed);
    endWrite();
  }

  Snapshot snapshot() const {
    Snapshot result;
    for (;;) {
      const uint32_t before = revision_.load(std::memory_order_acquire);
      if (before & 1U) continue;
      result.revision = before;
      result.runId = runId_.load(std::memory_order_relaxed);
      result.state = static_cast<RunState>(
          state_.load(std::memory_order_relaxed));
      result.endReason = static_cast<EndReason>(
          endReason_.load(std::memory_order_relaxed));
      result.bytesRead = loadBytes();
      result.transportBytesRead = loadTransportBytes();
      result.httpContentLength =
          httpContentLength_.load(std::memory_order_relaxed);
      result.startedAtMs = startedAtMs_.load(std::memory_order_relaxed);
      result.lastProgressAtMs =
          lastProgressAtMs_.load(std::memory_order_relaxed);
      result.terminalAtMs = terminalAtMs_.load(std::memory_order_relaxed);
      result.cleanupAtMs = cleanupAtMs_.load(std::memory_order_relaxed);
      loadRunEvents(result.events);
      result.totals.runs = totalRuns_.load(std::memory_order_relaxed);
      result.totals.watchdogTimeouts =
          totalWatchdogTimeouts_.load(std::memory_order_relaxed);
      result.totals.naturalEofs =
          totalNaturalEofs_.load(std::memory_order_relaxed);
      result.totals.cleanups = totalCleanups_.load(std::memory_order_relaxed);
      result.totals.replacements =
          totalReplacements_.load(std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_acquire);
      const uint32_t after = revision_.load(std::memory_order_relaxed);
      if (before == after && !(after & 1U)) return result;
    }
  }

  bool formatJson(char* output, size_t capacity) const {
    if (!output || capacity == 0) return false;
    const Snapshot s = snapshot();
    const int written = snprintf(
        output, capacity,
        "{\"enabled\":true,\"runId\":%lu,\"state\":\"%s\","
        "\"endReason\":\"%s\",\"bytesRead\":%llu,"
        "\"httpBodyBytesRead\":%llu,\"httpContentLength\":%lu,"
        "\"transportBytesRead\":%llu,"
        "\"startedAtMs\":%lu,\"lastProgressAtMs\":%lu,"
        "\"terminalAtMs\":%lu,\"cleanupAtMs\":%lu,"
        "\"events\":{\"starts\":%lu,\"progress\":%lu,"
        "\"watchdogTimeouts\":%lu,\"naturalEofs\":%lu,"
        "\"transportErrors\":%lu,\"connectFailures\":%lu,"
        "\"networkInterruptions\":%lu,\"userStops\":%lu,"
        "\"sourceSwitches\":%lu,\"updateStops\":%lu,"
        "\"replacements\":%lu,\"backendStops\":%lu,"
        "\"terminals\":%lu,\"cleanups\":%lu,"
        "\"ignoredTerminalEvents\":%lu},"
        "\"totals\":{\"runs\":%lu,\"watchdogTimeouts\":%lu,"
        "\"naturalEofs\":%lu,\"cleanups\":%lu,"
        "\"replacements\":%lu}}",
        static_cast<unsigned long>(s.runId), runStateName(s.state),
        endReasonName(s.endReason),
        static_cast<unsigned long long>(s.bytesRead),
        static_cast<unsigned long long>(s.bytesRead),
        static_cast<unsigned long>(s.httpContentLength),
        static_cast<unsigned long long>(s.transportBytesRead),
        static_cast<unsigned long>(s.startedAtMs),
        static_cast<unsigned long>(s.lastProgressAtMs),
        static_cast<unsigned long>(s.terminalAtMs),
        static_cast<unsigned long>(s.cleanupAtMs),
        static_cast<unsigned long>(s.events.starts),
        static_cast<unsigned long>(s.events.progress),
        static_cast<unsigned long>(s.events.watchdogTimeouts),
        static_cast<unsigned long>(s.events.naturalEofs),
        static_cast<unsigned long>(s.events.transportErrors),
        static_cast<unsigned long>(s.events.connectFailures),
        static_cast<unsigned long>(s.events.networkInterruptions),
        static_cast<unsigned long>(s.events.userStops),
        static_cast<unsigned long>(s.events.sourceSwitches),
        static_cast<unsigned long>(s.events.updateStops),
        static_cast<unsigned long>(s.events.replacements),
        static_cast<unsigned long>(s.events.backendStops),
        static_cast<unsigned long>(s.events.terminals),
        static_cast<unsigned long>(s.events.cleanups),
        static_cast<unsigned long>(s.events.ignoredTerminalEvents),
        static_cast<unsigned long>(s.totals.runs),
        static_cast<unsigned long>(s.totals.watchdogTimeouts),
        static_cast<unsigned long>(s.totals.naturalEofs),
        static_cast<unsigned long>(s.totals.cleanups),
        static_cast<unsigned long>(s.totals.replacements));
    if (written < 0 || static_cast<size_t>(written) >= capacity) {
      output[0] = '\0';
      return false;
    }
    return true;
  }

 private:
  static uint32_t value(RunState state) {
    return static_cast<uint32_t>(state);
  }
  static uint32_t value(EndReason reason) {
    return static_cast<uint32_t>(reason);
  }

  bool matchesCurrent(uint32_t token) const {
    return token && runId_.load(std::memory_order_relaxed) == token;
  }

  void beginWrite() {
    revision_.fetch_add(1, std::memory_order_acq_rel);
  }

  void endWrite() {
    revision_.fetch_add(1, std::memory_order_release);
  }

  void storeBytes(uint64_t bytes) {
    bytesLow_.store(static_cast<uint32_t>(bytes), std::memory_order_relaxed);
    bytesHigh_.store(static_cast<uint32_t>(bytes >> 32),
                     std::memory_order_relaxed);
  }

  uint64_t loadBytes() const {
    return (static_cast<uint64_t>(bytesHigh_.load(std::memory_order_relaxed))
            << 32) |
           bytesLow_.load(std::memory_order_relaxed);
  }

  void storeTransportBytes(uint64_t bytes) {
    transportBytesLow_.store(static_cast<uint32_t>(bytes),
                             std::memory_order_relaxed);
    transportBytesHigh_.store(static_cast<uint32_t>(bytes >> 32),
                              std::memory_order_relaxed);
  }

  uint64_t loadTransportBytes() const {
    return (static_cast<uint64_t>(
                transportBytesHigh_.load(std::memory_order_relaxed)) << 32) |
           transportBytesLow_.load(std::memory_order_relaxed);
  }

  void updateProgressLocked(uint64_t transportBytes, uint64_t httpBodyBytes,
                            uint32_t httpContentLength, uint32_t nowMs) {
    const uint64_t relativeTransport =
        transportBytes >= transportBase_ ? transportBytes - transportBase_ : 0;
    storeTransportBytes(relativeTransport);
    const uint64_t relativeBody =
        httpBodyBytes >= httpBodyBase_ ? httpBodyBytes - httpBodyBase_ : 0;
    lastHttpBody_ = httpBodyBytes;
    storeBytes(relativeBody);
    if (httpContentLength) {
      httpContentLength_.store(httpContentLength, std::memory_order_relaxed);
    }
    if (transportBytes != lastTransport_) {
      lastTransport_ = transportBytes;
      lastProgressAtMs_.store(nowMs, std::memory_order_relaxed);
      progress_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void terminal(uint32_t token, EndReason reason, uint32_t nowMs,
                uint64_t transportBytes, uint64_t httpBodyBytes,
                uint32_t httpContentLength, bool accepted) {
    if (!matchesCurrent(token)) return;
    beginWrite();
    updateProgressLocked(transportBytes, httpBodyBytes, httpContentLength,
                         nowMs);
    incrementReasonEvent(reason);
    const uint32_t currentReason =
        endReason_.load(std::memory_order_relaxed);
    const bool lateNetworkCause = accepted &&
        reason == EndReason::NetworkInterrupted &&
        currentReason == value(EndReason::BackendStopped);
    const bool first = accepted && currentReason == value(EndReason::None);
    if (lateNetworkCause) {
      // Network may publish immediately after Player has observed the same
      // terminal bit and used the generic backend fallback. Correct only that
      // fallback; an explicit EOF/error/timeout reason always remains first.
      endReason_.store(value(reason), std::memory_order_relaxed);
      backendStops_.fetch_sub(1, std::memory_order_relaxed);
    } else if (first) {
      endReason_.store(value(reason), std::memory_order_relaxed);
      state_.store(value(RunState::Terminal), std::memory_order_relaxed);
      terminalAtMs_.store(nowMs, std::memory_order_relaxed);
      terminals_.fetch_add(1, std::memory_order_relaxed);
      incrementAcceptedTotal(reason);
    } else {
      ignoredTerminalEvents_.fetch_add(1, std::memory_order_relaxed);
    }
    endWrite();
  }

  void incrementReasonEvent(EndReason reason) {
    switch (reason) {
      case EndReason::NaturalEof:
        naturalEofs_.fetch_add(1, std::memory_order_relaxed); break;
      case EndReason::WatchdogTimeout:
        watchdogTimeouts_.fetch_add(1, std::memory_order_relaxed); break;
      case EndReason::TransportError:
        transportErrors_.fetch_add(1, std::memory_order_relaxed); break;
      case EndReason::ConnectFailed:
        connectFailures_.fetch_add(1, std::memory_order_relaxed); break;
      case EndReason::NetworkInterrupted:
        networkInterruptions_.fetch_add(1, std::memory_order_relaxed); break;
      case EndReason::UserStop:
        userStops_.fetch_add(1, std::memory_order_relaxed); break;
      case EndReason::SourceSwitch:
        sourceSwitches_.fetch_add(1, std::memory_order_relaxed); break;
      case EndReason::UpdateStop:
        updateStops_.fetch_add(1, std::memory_order_relaxed); break;
      case EndReason::Replaced:
        replacements_.fetch_add(1, std::memory_order_relaxed); break;
      case EndReason::BackendStopped:
        backendStops_.fetch_add(1, std::memory_order_relaxed); break;
      case EndReason::None:
        break;
    }
  }

  void incrementAcceptedTotal(EndReason reason) {
    switch (reason) {
      case EndReason::NaturalEof:
        totalNaturalEofs_.fetch_add(1, std::memory_order_relaxed); break;
      case EndReason::WatchdogTimeout:
        totalWatchdogTimeouts_.fetch_add(1, std::memory_order_relaxed); break;
      case EndReason::Replaced:
        totalReplacements_.fetch_add(1, std::memory_order_relaxed); break;
      default:
        break;
    }
  }

  void resetRunEvents() {
    progress_.store(0, std::memory_order_relaxed);
    watchdogTimeouts_.store(0, std::memory_order_relaxed);
    naturalEofs_.store(0, std::memory_order_relaxed);
    transportErrors_.store(0, std::memory_order_relaxed);
    connectFailures_.store(0, std::memory_order_relaxed);
    networkInterruptions_.store(0, std::memory_order_relaxed);
    userStops_.store(0, std::memory_order_relaxed);
    sourceSwitches_.store(0, std::memory_order_relaxed);
    updateStops_.store(0, std::memory_order_relaxed);
    replacements_.store(0, std::memory_order_relaxed);
    backendStops_.store(0, std::memory_order_relaxed);
    terminals_.store(0, std::memory_order_relaxed);
    cleanups_.store(0, std::memory_order_relaxed);
    ignoredTerminalEvents_.store(0, std::memory_order_relaxed);
  }

  void loadRunEvents(EventCounters& events) const {
    events.starts = starts_.load(std::memory_order_relaxed);
    events.progress = progress_.load(std::memory_order_relaxed);
    events.watchdogTimeouts =
        watchdogTimeouts_.load(std::memory_order_relaxed);
    events.naturalEofs = naturalEofs_.load(std::memory_order_relaxed);
    events.transportErrors = transportErrors_.load(std::memory_order_relaxed);
    events.connectFailures = connectFailures_.load(std::memory_order_relaxed);
    events.networkInterruptions =
        networkInterruptions_.load(std::memory_order_relaxed);
    events.userStops = userStops_.load(std::memory_order_relaxed);
    events.sourceSwitches = sourceSwitches_.load(std::memory_order_relaxed);
    events.updateStops = updateStops_.load(std::memory_order_relaxed);
    events.replacements = replacements_.load(std::memory_order_relaxed);
    events.backendStops = backendStops_.load(std::memory_order_relaxed);
    events.terminals = terminals_.load(std::memory_order_relaxed);
    events.cleanups = cleanups_.load(std::memory_order_relaxed);
    events.ignoredTerminalEvents =
        ignoredTerminalEvents_.load(std::memory_order_relaxed);
  }

  std::atomic<uint32_t> revision_{0};
  std::atomic<uint32_t> runId_{0};
  std::atomic<uint32_t> state_{value(RunState::Idle)};
  std::atomic<uint32_t> endReason_{value(EndReason::None)};
  std::atomic<uint32_t> bytesLow_{0};
  std::atomic<uint32_t> bytesHigh_{0};
  std::atomic<uint32_t> transportBytesLow_{0};
  std::atomic<uint32_t> transportBytesHigh_{0};
  std::atomic<uint32_t> httpContentLength_{0};
  std::atomic<uint32_t> startedAtMs_{0};
  std::atomic<uint32_t> lastProgressAtMs_{0};
  std::atomic<uint32_t> terminalAtMs_{0};
  std::atomic<uint32_t> cleanupAtMs_{0};

  std::atomic<uint32_t> starts_{0};
  std::atomic<uint32_t> progress_{0};
  std::atomic<uint32_t> watchdogTimeouts_{0};
  std::atomic<uint32_t> naturalEofs_{0};
  std::atomic<uint32_t> transportErrors_{0};
  std::atomic<uint32_t> connectFailures_{0};
  std::atomic<uint32_t> networkInterruptions_{0};
  std::atomic<uint32_t> userStops_{0};
  std::atomic<uint32_t> sourceSwitches_{0};
  std::atomic<uint32_t> updateStops_{0};
  std::atomic<uint32_t> replacements_{0};
  std::atomic<uint32_t> backendStops_{0};
  std::atomic<uint32_t> terminals_{0};
  std::atomic<uint32_t> cleanups_{0};
  std::atomic<uint32_t> ignoredTerminalEvents_{0};

  std::atomic<uint32_t> totalRuns_{0};
  std::atomic<uint32_t> totalWatchdogTimeouts_{0};
  std::atomic<uint32_t> totalNaturalEofs_{0};
  std::atomic<uint32_t> totalCleanups_{0};
  std::atomic<uint32_t> totalReplacements_{0};

  std::atomic<uint32_t> pendingNetworkToken_{0};
  uint64_t transportBase_ = 0;
  uint64_t lastTransport_ = 0;
  uint64_t httpBodyBase_ = 0;
  uint64_t lastHttpBody_ = 0;
};

inline Diagnostics& diagnostics() {
  static Diagnostics instance;
  return instance;
}

}  // namespace tts_hil
}  // namespace voxone

#endif  // VOXONE_TTS_HIL_DIAGNOSTICS
#endif  // VOXONE_TTS_HIL_DIAGNOSTICS_H
