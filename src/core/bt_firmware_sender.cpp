#include "bt_firmware_sender.h"

#include <stdio.h>
#include <string.h>

namespace {
void put16(uint8_t* p, uint16_t n) {
  p[0] = static_cast<uint8_t>(n);
  p[1] = static_cast<uint8_t>(n >> 8);
}
void put32(uint8_t* p, uint32_t n) {
  for (uint8_t i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(n >> (8 * i));
}
uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t value = 0xFFFFFFFFU;
  for (size_t i = 0; i < n; ++i) {
    value ^= p[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      value = (value >> 1) ^ ((value & 1U) ? 0xEDB88320U : 0U);
  }
  return value ^ 0xFFFFFFFFU;
}
bool decimal(const char*& p, uint32_t& value) {
  if (*p < '0' || *p > '9') return false;
  value = 0;
  do {
    const uint32_t digit = static_cast<uint32_t>(*p++ - '0');
    if (value > (UINT32_MAX - digit) / 10U) return false;
    value = value * 10U + digit;
  } while (*p >= '0' && *p <= '9');
  return true;
}
bool validVersion(const char* text) {
  if (text == nullptr || text[0] == '\0') return false;
  size_t n = 0;
  while (text[n] != '\0' && n <= 24) {
    const char c = text[n++];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == '-'))
      return false;
  }
  return n <= 24;
}
}  // namespace

bool BtFirmwareSender::hasCapability(const char* caps, const char* token) {
  if (caps == nullptr || token == nullptr || *token == '\0') return false;
  const size_t n = strlen(token);
  for (const char* p = caps; *p != '\0';) {
    while (*p == ' ') ++p;
    const char* end = p;
    while (*end != '\0' && *end != ' ') ++end;
    if (static_cast<size_t>(end - p) == n && memcmp(p, token, n) == 0)
      return true;
    p = end;
  }
  return false;
}

bool BtFirmwareSender::start(BtFirmwareImage& image,
                             const Preconditions& p, uint32_t nowMs) {
  if (exclusive()) { error_ = Error::Busy; return false; }
  state_ = State::Idle;
  phase_ = Phase::Prepare;
  error_ = Error::None;
  txLength_ = txOffset_ = 0;
  total_ = confirmed_ = sequence_ = 0;
  nowMs_ = nowMs;
  if (!p.online) { fail(Error::Offline, false); return false; }
  if (p.protocol != 2 || !hasCapability(p.capabilities, "FW_UPDATE")) {
    fail(Error::Unsupported, false); return false;
  }
  if (p.activeBtSource) { fail(Error::ActiveBtSource, false); return false; }
  if (image.size() == 0 || image.size() == UINT32_MAX ||
      !validVersion(image.version())) {
    fail(Error::InvalidImage, false); return false;
  }
  const int length = snprintf(reinterpret_cast<char*>(frame_), sizeof(frame_),
                              "FW_BEGIN %lu %08lX %s\n",
                              static_cast<unsigned long>(image.size()),
                              static_cast<unsigned long>(image.crc32()),
                              image.version());
  if (length <= 0 || length > 65) {
    fail(Error::InvalidImage, false); return false;
  }
  memcpy(expectedVersion_, image.version(), strlen(image.version()) + 1);
  image_ = &image;
  total_ = image.size();
  confirmed_ = sequence_ = 0;
  retransmissions_ = 0;
  readySeen_ = statusProbe_ = abortTx_ = false;
  txLength_ = static_cast<size_t>(length);
  txOffset_ = 0;
  deadlineStartMs_ = nowMs;
  state_ = State::SendingBegin;
  return true;
}

void BtFirmwareSender::encodeFrame(uint8_t type, uint32_t sequence,
                                   size_t payloadLength) {
  frame_[0] = 0xB7;
  frame_[1] = 0x4F;
  frame_[2] = type;
  put32(frame_ + 3, sequence);
  put16(frame_ + 7, static_cast<uint16_t>(payloadLength));
  put32(frame_ + 9 + payloadLength,
        crc32(frame_ + 2, 7 + payloadLength));
  txLength_ = 13 + payloadLength;
  txOffset_ = 0;
}

bool BtFirmwareSender::prepareData() {
  const size_t remaining = static_cast<size_t>(total_ - confirmed_);
  const size_t length = remaining < PayloadSize ? remaining : PayloadSize;
  if (image_ == nullptr || image_->read(confirmed_, frame_ + 9, length) != length)
    return false;
  encodeFrame(1, sequence_, length);
  retransmissions_ = 0;
  state_ = State::SendingData;
  phase_ = Phase::SendingToBt;
  return true;
}

void BtFirmwareSender::prepareEnd() {
  encodeFrame(2, sequence_, 0);
  state_ = State::SendingEnd;
  phase_ = Phase::Verifying;
}

void BtFirmwareSender::prepareAbort() {
  encodeFrame(3, sequence_, 0);
  abortTx_ = true;
  abortProgressMs_ = nowMs_;
}

void BtFirmwareSender::fail(Error error, bool abortRemote) {
  error_ = error;
  phase_ = Phase::Error;
  state_ = State::Error;
  image_ = nullptr;
  txLength_ = txOffset_ = 0;
  abortTx_ = false;
  if (abortRemote) prepareAbort();
}

void BtFirmwareSender::abort() {
  if (!exclusive()) return;
  const bool binary = state_ == State::SendingData || state_ == State::WaitAck ||
                      state_ == State::SendingEnd || state_ == State::WaitVerify ||
                      state_ == State::WaitOk;
  fail(Error::Aborted, binary);
  state_ = State::Aborted;
}

bool BtFirmwareSender::exclusive() const {
  return (state_ != State::Idle && state_ != State::Success &&
          state_ != State::Error && state_ != State::Aborted) || abortTx_;
}

void BtFirmwareSender::transmit(uint32_t nowMs) {
  if (txOffset_ >= txLength_) return;
  const int free = transport_.availableForWrite();
  if (free <= 0) return;
  size_t length = txLength_ - txOffset_;
  if (length > MaxTxPerTick) length = MaxTxPerTick;
  if (length > static_cast<size_t>(free)) length = static_cast<size_t>(free);
  const size_t written = transport_.write(frame_ + txOffset_, length);
  if (written > length) {
    fail(Error::Transport, state_ != State::SendingBegin);
    return;
  }
  txOffset_ += written;
  if (written != 0) deadlineStartMs_ = nowMs;
  if (written != 0 && abortTx_) abortProgressMs_ = nowMs;
  if (txOffset_ != txLength_) return;
  if (abortTx_) { abortTx_ = false; txLength_ = txOffset_ = 0; return; }
  deadlineStartMs_ = nowMs;
  if (state_ == State::SendingBegin) state_ = State::WaitReady;
  else if (state_ == State::SendingData) state_ = State::WaitAck;
  else if (state_ == State::SendingEnd) state_ = State::WaitVerify;
}

void BtFirmwareSender::retry(uint32_t nowMs) {
  if (retransmissions_ >= MaxRetransmissions) {
    fail(Error::RetryLimit, true);
    return;
  }
  ++retransmissions_;
  txOffset_ = 0;  // Exactly the same bytes, including frame CRC.
  deadlineStartMs_ = nowMs;
  state_ = State::SendingData;
}

void BtFirmwareSender::tick(uint32_t nowMs) {
  nowMs_ = nowMs;
  if (abortTx_ &&
      static_cast<uint32_t>(nowMs - abortProgressMs_) >= ReplyTimeoutMs) {
    abortTx_ = false;  // UART unavailable: do not lock normal operation forever.
    txLength_ = txOffset_ = 0;
    return;
  }
  if (abortTx_ || state_ == State::SendingBegin ||
      state_ == State::SendingData || state_ == State::SendingEnd) {
    if (!abortTx_ && static_cast<uint32_t>(nowMs - deadlineStartMs_) >=
                         ReplyTimeoutMs) {
      const bool binary = state_ != State::SendingBegin;
      fail(Error::Transport, binary);
      return;
    }
    transmit(nowMs);
    return;
  }
  if (state_ == State::WaitReady &&
      static_cast<uint32_t>(nowMs - deadlineStartMs_) >= ReplyTimeoutMs)
    fail(Error::ReadyTimeout, true);
  else if (state_ == State::WaitAck &&
           static_cast<uint32_t>(nowMs - deadlineStartMs_) >= ReplyTimeoutMs)
    retry(nowMs);
  else if ((state_ == State::WaitVerify || state_ == State::WaitOk) &&
           static_cast<uint32_t>(nowMs - deadlineStartMs_) >= ReplyTimeoutMs)
    fail(Error::VerifyTimeout, true);
  else if (state_ == State::WaitIdentity &&
           static_cast<uint32_t>(nowMs - deadlineStartMs_) >= IdentityTimeoutMs)
    fail(Error::IdentityTimeout, false);
}

bool BtFirmwareSender::onLine(const char* line, uint32_t nowMs) {
  nowMs_ = nowMs;
  if (line == nullptr) return false;
  if (strcmp(line, "READY") == 0 && state_ == State::WaitIdentity) {
    readySeen_ = true;
    statusProbe_ = true;
    phase_ = Phase::WaitingForBt;
    return false;  // BtLinkProtocol still handles READY and offline state.
  }
  if (strncmp(line, "FW_", 3) != 0 || !exclusive()) return false;
  if (strncmp(line, "FW_ERR ", 7) == 0) {
    fail(Error::RemoteError, false);
    return true;
  }
  if (state_ == State::WaitReady) {
    if (strcmp(line, "FW_READY 1024") == 0) {
      if (!prepareData()) fail(Error::ReadFailed, true);
    } else fail(Error::InvalidReady, true);
    return true;
  }
  if (state_ == State::WaitAck &&
      (strncmp(line, "FW_ACK ", 7) == 0 ||
       strncmp(line, "FW_NACK ", 8) == 0)) {
    const bool ack = line[3] == 'A';
    const char* p = line + (ack ? 7 : 8);
    uint32_t seq = 0, bytes = 0;
    if (!decimal(p, seq) || *p++ != ' ' ||
        (ack ? (!decimal(p, bytes) || *p != '\0') : (*p == '\0'))) {
      fail(Error::InvalidResponse, true); return true;
    }
    if (seq != sequence_) { fail(Error::AckSequence, true); return true; }
    if (ack) {
      const uint32_t expected = confirmed_ +
          static_cast<uint32_t>(txLength_ - 13);
      if (bytes != expected) { fail(Error::AckBytes, true); return true; }
      confirmed_ = bytes;
      ++sequence_;
      if (confirmed_ == total_) prepareEnd();
      else if (!prepareData()) fail(Error::ReadFailed, true);
    } else if (strcmp(p, "FRAME_CRC") == 0 ||
               strcmp(p, "WRONG_SEQUENCE") == 0) {
      retry(nowMs);
    } else fail(Error::RemoteError, true);
    return true;
  }
  if (state_ == State::WaitVerify && strcmp(line, "FW_VERIFY") == 0) {
    state_ = State::WaitOk;
    deadlineStartMs_ = nowMs;
    return true;
  }
  if (state_ == State::WaitOk && strcmp(line, "FW_OK") == 0) {
    state_ = State::WaitIdentity;
    phase_ = Phase::RestartingBt;
    deadlineStartMs_ = nowMs;
    return true;
  }
  if (strcmp(line, "FW_ABORTED") == 0) {
    fail(Error::RemoteError, false);
    return true;
  }
  fail(Error::InvalidResponse,
       state_ != State::WaitIdentity && state_ != State::WaitReady);
  return true;
}

void BtFirmwareSender::onIdentity(bool online, uint8_t protocol,
                                   const char* version, const char* caps) {
  if (state_ != State::WaitIdentity || !readySeen_ || !online) return;
  if (protocol != 2 || !hasCapability(caps, "FW_UPDATE")) {
    fail(Error::IdentityMismatch, false); return;
  }
  if (version == nullptr || strcmp(version, expectedVersion_) != 0) {
    fail(Error::VersionMismatch, false); return;
  }
  state_ = State::Success;
  phase_ = Phase::Success;
  image_ = nullptr;
}

bool BtFirmwareSender::takeStatusProbe() {
  const bool pending = statusProbe_;
  statusProbe_ = false;
  return pending;
}

BtFirmwareSender::Progress BtFirmwareSender::progress() const {
  Progress result;
  result.state = state_;
  result.phase = phase_;
  result.error = error_;
  result.totalBytes = total_;
  result.confirmedBytes = confirmed_;
  result.percent = total_ == 0 ? 0 :
      static_cast<uint8_t>((static_cast<uint64_t>(confirmed_) * 100U) / total_);
  return result;
}
