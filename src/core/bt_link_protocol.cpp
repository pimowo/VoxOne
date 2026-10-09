#include "bt_link_protocol.h"

#include <string.h>
#include <stdio.h>

namespace {

template <size_t N>
bool copyField(char (&destination)[N], const char* value) {
  const size_t length = strlen(value);
  if (length >= N) return false;
  memcpy(destination, value, length + 1);
  return true;
}

const char* fieldValue(const char* line, const char* prefix) {
  const size_t length = strlen(prefix);
  return strncmp(line, prefix, length) == 0 ? line + length : nullptr;
}

bool parseUnsigned(const char* text, uint32_t maximum, uint32_t& result) {
  if (*text == '\0') return false;
  uint32_t value = 0;
  for (; *text != '\0'; ++text) {
    if (*text < '0' || *text > '9') return false;
    const uint32_t digit = static_cast<uint32_t>(*text - '0');
    if (value > (maximum - digit) / 10) return false;
    value = value * 10 + digit;
  }
  result = value;
  return true;
}

}  // namespace

BtLinkProtocol::BtLinkProtocol(SendCommand sendCommand, OnEvent onEvent,
                               void* context)
    : sendCommand_(sendCommand), onEvent_(onEvent), context_(context) {}

void BtLinkProtocol::begin(uint32_t nowMs) {
  state_ = BtLinkState{};
  diagnostics_ = BtLinkDiagnostics{};
  lineLength_ = 0;
  discardingLine_ = false;
  statusOpen_ = false;
  statusIdentityFields_ = 0;
  statusOtaMalformed_ = false;
  diagnosticsOpen_ = false;
  protocolSeen_ = false;
  unsupportedProtocolReported_ = false;
  diagnosticsRequested_ = false;
  diagnosticsFields_ = 0;
  lastRxMs_ = nowMs;
  lastPingMs_ = nowMs;
  lastProbeMs_ = nowMs - ProbeIntervalMs;
  requestStatus(nowMs);
}

void BtLinkProtocol::send(const char* command) {
  if (sendCommand_ != nullptr) sendCommand_(context_, command);
}

void BtLinkProtocol::notify(BtLinkEvent event) {
  if (onEvent_ != nullptr) onEvent_(context_, event);
}

void BtLinkProtocol::requestStatus(uint32_t nowMs) {
  send("GET_STATUS");
  lastProbeMs_ = nowMs;
}

void BtLinkProtocol::requestDiag() { send("GET_DIAG"); }

bool BtLinkProtocol::sendTransport(const char* command) {
  if (updateExclusive_ || !state_.runtimeAvailable || !state_.connected || statusOpen_) return false;
  send(command);
  return true;
}

bool BtLinkProtocol::play() { return sendTransport("PLAY"); }
bool BtLinkProtocol::pause() { return sendTransport("PAUSE"); }
bool BtLinkProtocol::next() { return sendTransport("NEXT"); }
bool BtLinkProtocol::prev() { return sendTransport("PREV"); }

bool BtLinkProtocol::setVolume(uint8_t absoluteVolume) {
  if (updateExclusive_ || absoluteVolume > 127 || !state_.runtimeAvailable ||
      !state_.connected || statusOpen_) return false;
  char command[16];
  snprintf(command, sizeof(command), "SET_VOLUME %u", absoluteVolume);
  send(command);
  return true;
}

void BtLinkProtocol::ping(uint32_t nowMs) {
  send("PING");
  lastPingMs_ = nowMs;
}

void BtLinkProtocol::clearSession() {
  state_.connected = false;
  state_.peerName[0] = '\0';
  state_.playback = BtPlayback::Stopped;
  state_.artist[0] = '\0';
  state_.title[0] = '\0';
  state_.album[0] = '\0';
  state_.volume = -1;
  state_.sampleRate = 0;
  state_.rawVuLeft = 0;
  state_.rawVuRight = 0;
  state_.rawVuLastMs = 0;
}

void BtLinkProtocol::goOffline() {
  const bool wasOnline = state_.runtimeAvailable;
  state_.runtimeAvailable = false;
  state_.protocolVersion = 0;
  state_.firmwareVersion[0] = '\0';
  state_.btName[0] = '\0';
  state_.capabilities[0] = '\0';
  state_.otaState = BtOtaState::Missing;
  clearSession();
  statusOpen_ = false;
  statusIdentityFields_ = 0;
  statusOtaMalformed_ = false;
  diagnosticsOpen_ = false;
  protocolSeen_ = false;
  diagnosticsRequested_ = false;
  if (wasOnline) notify(BtLinkEvent::Offline);
}

void BtLinkProtocol::suspend() {
  goOffline();
  lineLength_ = 0;
  discardingLine_ = false;
}

void BtLinkProtocol::tick(uint32_t nowMs) {
  if (updateExclusive_) return;
  if (state_.runtimeAvailable && nowMs - lastRxMs_ >= OfflineTimeoutMs) {
    goOffline();
  }
  if (!state_.runtimeAvailable) {
    if (nowMs - lastProbeMs_ >= ProbeIntervalMs) requestStatus(nowMs);
    return;
  }
  if (nowMs - lastPingMs_ >= PingIntervalMs) ping(nowMs);
}

void BtLinkProtocol::feed(char byte, uint32_t nowMs) {
  if (byte == '\r') return;
  if (byte == '\n') {
    if (!discardingLine_ && lineLength_ != 0) {
      line_[lineLength_] = '\0';
      if ((lineObserver_ != nullptr && lineObserver_(context_, line_, nowMs)) ||
          handleLine(nowMs)) lastRxMs_ = nowMs;
    }
    lineLength_ = 0;
    discardingLine_ = false;
    return;
  }
  if (discardingLine_) return;
  if (static_cast<unsigned char>(byte) < 0x20 ||
      lineLength_ >= MaxLineLength) {
    lineLength_ = 0;
    discardingLine_ = true;
    return;
  }
  line_[lineLength_++] = byte;
}

bool BtLinkProtocol::handleLine(uint32_t nowMs) {
  if (strcmp(line_, "READY") == 0) {
    goOffline();
    // Protocol v1 repeats READY in every GET_STATUS response. Rate-limit it.
    if (!updateExclusive_ && nowMs - lastProbeMs_ >= 1000) requestStatus(nowMs);
    return true;
  }

  if (strcmp(line_, "STATUS_BEGIN") == 0) {
    if (updateExclusive_) statusBackup_ = state_;
    clearSession();
    statusOpen_ = true;
    statusIdentityFields_ = 0;
    statusOtaMalformed_ = false;
    state_.otaState = BtOtaState::Missing;
    return true;
  }
  if (strcmp(line_, "STATUS_END") == 0) {
    if (!statusOpen_) return false;
    statusOpen_ = false;
    const bool completeOtaIdentity = statusIdentityFields_ == 0x0f &&
                                     !statusOtaMalformed_;
    if (updateExclusive_ && !completeOtaIdentity) {
      state_ = statusBackup_;  // A partial OTA snapshot is not a disconnect.
      return true;
    }
    // OTA identity must come from this complete post-READY snapshot, not
    // from startup announcements or fields retained from an older snapshot.
    if ((!updateExclusive_ || completeOtaIdentity) &&
        protocolSeen_ && state_.protocolVersion == 2 &&
        !state_.runtimeAvailable) {
      state_.runtimeAvailable = true;
      lastPingMs_ = nowMs;
      notify(BtLinkEvent::Online);
      if (!updateExclusive_ && !diagnosticsRequested_) {
        requestDiag();
        diagnosticsRequested_ = true;
      }
    }
    if (updateExclusive_ && completeOtaIdentity && state_.runtimeAvailable &&
        state_.protocolVersion == 2)
      notify(BtLinkEvent::UpdateIdentity);
    return true;
  }

  if (strcmp(line_, "DIAG_BEGIN") == 0) {
    diagnostics_ = BtLinkDiagnostics{};
    diagnosticsFields_ = 0;
    diagnosticsOpen_ = true;
    return true;
  }
  if (strcmp(line_, "DIAG_END") == 0) {
    if (!diagnosticsOpen_) return false;
    diagnosticsOpen_ = false;
    if (diagnosticsFields_ == 0x0F) notify(BtLinkEvent::Diagnostics);
    return true;
  }

  if (strcmp(line_, "CONNECTED") == 0) {
    state_.connected = true;
    return true;
  }
  if (strcmp(line_, "DISCONNECTED") == 0) {
    clearSession();
    return true;
  }
  if (strcmp(line_, "PLAYING") == 0) {
    state_.playback = BtPlayback::Playing;
    return true;
  }
  if (strcmp(line_, "PAUSED") == 0) {
    state_.playback = BtPlayback::Paused;
    state_.rawVuLeft = 0;
    state_.rawVuRight = 0;
    return true;
  }
  if (strcmp(line_, "STOPPED") == 0) {
    state_.playback = BtPlayback::Stopped;
    state_.rawVuLeft = 0;
    state_.rawVuRight = 0;
    return true;
  }
  if (strcmp(line_, "PONG") == 0) return true;

  const char* value = nullptr;
  uint32_t number = 0;
  if ((value = fieldValue(line_, "PROTO ")) != nullptr) {
    if (!parseUnsigned(value, UINT8_MAX, number)) return false;
    if (number != 2) {
      if (state_.runtimeAvailable) goOffline();
      state_.protocolVersion = static_cast<uint8_t>(number);
      protocolSeen_ = true;
      if (!unsupportedProtocolReported_) {
        notify(BtLinkEvent::UnsupportedProtocol);
        unsupportedProtocolReported_ = true;
      }
    } else {
      state_.protocolVersion = 2;
      protocolSeen_ = true;
      unsupportedProtocolReported_ = false;
    }
    if (statusOpen_) statusIdentityFields_ |= 0x01;
    return true;
  }
  if ((value = fieldValue(line_, "FW_VERSION ")) != nullptr) {
    if (!copyField(state_.firmwareVersion, value)) return false;
    if (statusOpen_) statusIdentityFields_ |= 0x02;
    return true;
  }
  if ((value = fieldValue(line_, "BT_NAME ")) != nullptr)
    return copyField(state_.btName, value);
  if ((value = fieldValue(line_, "CAPS ")) != nullptr) {
    if (!copyField(state_.capabilities, value)) return false;
    if (statusOpen_) statusIdentityFields_ |= 0x04;
    return true;
  }
  if (strncmp(line_, "OTA_STATE", 9) == 0 &&
      (line_[9] == ' ' || line_[9] == '\0')) {
    if (!statusOpen_) return false;
    if ((statusIdentityFields_ & 0x08) != 0 || line_[9] != ' ') {
      statusOtaMalformed_ = true;
      return true;
    }
    value = line_ + 10;
    if (strcmp(value, "NOT_PENDING") == 0)
      state_.otaState = BtOtaState::NotPending;
    else if (strcmp(value, "PENDING_VERIFY") == 0)
      state_.otaState = BtOtaState::PendingVerify;
    else if (strcmp(value, "VALID") == 0)
      state_.otaState = BtOtaState::Valid;
    else if (strcmp(value, "CONFIRM_FAILED") == 0)
      state_.otaState = BtOtaState::ConfirmFailed;
    else if (strcmp(value, "UNKNOWN") == 0)
      state_.otaState = BtOtaState::Unknown;
    else {
      statusOtaMalformed_ = true;
      return true;
    }
    statusIdentityFields_ |= 0x08;
    return true;
  }
  if ((value = fieldValue(line_, "DEVICE ")) != nullptr)
    return copyField(state_.peerName, value);
  if ((value = fieldValue(line_, "ARTIST ")) != nullptr)
    return copyField(state_.artist, value);
  if ((value = fieldValue(line_, "TITLE ")) != nullptr)
    return copyField(state_.title, value);
  if ((value = fieldValue(line_, "ALBUM ")) != nullptr)
    return copyField(state_.album, value);
  if ((value = fieldValue(line_, "VOLUME ")) != nullptr) {
    if (!parseUnsigned(value, 127, number)) return false;
    state_.volume = static_cast<int16_t>(number);
    ++state_.volumeRevision;
    return true;
  }
  if ((value = fieldValue(line_, "VU ")) != nullptr) {
    uint32_t left = 0;
    uint32_t right = 0;
    const char* separator = strchr(value, ' ');
    if (separator == nullptr || separator == value ||
        separator - value > 5 || separator[1] == '\0') return false;
    char leftText[6];
    memcpy(leftText, value, separator - value);
    leftText[separator - value] = '\0';
    if (!parseUnsigned(leftText, 32768, left) ||
        !parseUnsigned(separator + 1, 32768, right)) return false;
    if (state_.connected) {
      state_.rawVuLeft = static_cast<uint16_t>(left);
      state_.rawVuRight = static_cast<uint16_t>(right);
      state_.rawVuLastMs = nowMs;
    }
    return true;
  }
  if ((value = fieldValue(line_, "SAMPLE_RATE ")) != nullptr) {
    if (!parseUnsigned(value, UINT32_MAX, number) || number == 0)
      return false;
    state_.sampleRate = number;
    return true;
  }
  if (!diagnosticsOpen_) return false;
  if ((value = fieldValue(line_, "RESET_REASON ")) != nullptr) {
    if (!copyField(diagnostics_.resetReason, value)) return false;
    diagnosticsFields_ |= 0x01;
    return true;
  }
  if ((value = fieldValue(line_, "UPTIME ")) != nullptr) {
    if (!parseUnsigned(value, UINT32_MAX, number)) return false;
    diagnostics_.uptime = number;
    diagnosticsFields_ |= 0x02;
    return true;
  }
  if ((value = fieldValue(line_, "HEAP ")) != nullptr) {
    if (!parseUnsigned(value, UINT32_MAX, number)) return false;
    diagnostics_.heap = number;
    diagnosticsFields_ |= 0x04;
    return true;
  }
  if ((value = fieldValue(line_, "MIN_HEAP ")) != nullptr) {
    if (!parseUnsigned(value, UINT32_MAX, number)) return false;
    diagnostics_.minHeap = number;
    diagnosticsFields_ |= 0x08;
    return true;
  }
  return false;
}
