#include "../src/core/bt_firmware_sender.h"
#include "../src/core/bt_link_protocol.h"

#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
uint32_t imageCrc(const std::vector<uint8_t>& bytes) {
  uint32_t crc = 0xFFFFFFFFU;
  for (uint8_t byte : bytes) {
    crc ^= byte;
    for (int i = 0; i < 8; ++i)
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
  }
  return crc ^ 0xFFFFFFFFU;
}

struct Image : BtFirmwareImage {
  std::vector<uint8_t> bytes;
  std::string label = "0.6.1-dev";
  int reads = 0;
  bool failRead = false;
  explicit Image(size_t size) : bytes(size) {
    for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(i);
  }
  uint32_t size() const override { return static_cast<uint32_t>(bytes.size()); }
  uint32_t crc32() const override { return imageCrc(bytes); }
  const char* version() const override { return label.c_str(); }
  size_t read(uint32_t offset, uint8_t* out, size_t n) override {
    ++reads;
    if (failRead || offset + n > bytes.size()) return 0;
    memcpy(out, bytes.data() + offset, n);
    return n;
  }
};

struct Transport : BtFirmwareTransport {
  std::vector<uint8_t> sent;
  int space = 17;
  size_t maxAccepted = SIZE_MAX;
  size_t lastRequested = 0;
  int availableForWrite() override { return space; }
  size_t write(const uint8_t* bytes, size_t n) override {
    lastRequested = n;
    const size_t accepted = std::min(n, maxAccepted);
    sent.insert(sent.end(), bytes, bytes + accepted);
    return accepted;
  }
  void clear() { sent.clear(); }
  std::string text() const { return std::string(sent.begin(), sent.end()); }
};

struct DiagnosticCapture {
  std::vector<std::string> lines;
  static void log(void* context, const char* message) {
    static_cast<DiagnosticCapture*>(context)->lines.emplace_back(message);
  }
  bool contains(const char* fragment) const {
    for (const auto& line : lines)
      if (line.find(fragment) != std::string::npos) return true;
    return false;
  }
};

struct Rig {
  Transport tx;
  BtFirmwareSender sender{tx};
  Image image;
  uint32_t now = 100;
  explicit Rig(size_t size) : image(size) {}
  BtFirmwareSender::Preconditions pre() const {
    BtFirmwareSender::Preconditions p;
    p.online = true;
    p.protocol = 2;
    p.capabilities = "A2DP AVRCP FW_UPDATE VU_RAW";
    return p;
  }
  void pump(BtFirmwareSender::State until) {
    for (int i = 0; i < 3000 && sender.progress().state != until; ++i)
      sender.tick(now++);
    assert(sender.progress().state == until);
  }
  void begin() {
    assert(sender.start(image, pre(), now));
    pump(BtFirmwareSender::State::WaitReady);
    assert(tx.text().find("FW_BEGIN ") == 0);
    assert(tx.text().back() == '\n');
    assert(tx.sent.size() <= 65);
    tx.clear();
    assert(sender.onLine("FW_READY 1024", now));
    pump(BtFirmwareSender::State::WaitAck);
  }
};

void checkFrame(const std::vector<uint8_t>& frame, uint8_t type,
                uint32_t sequence, size_t length) {
  assert(frame.size() == 13 + length);
  assert(frame[0] == 0xB7 && frame[1] == 0x4F && frame[2] == type);
  assert(frame[3] == static_cast<uint8_t>(sequence));
  assert(frame[4] == static_cast<uint8_t>(sequence >> 8));
  assert(frame[5] == static_cast<uint8_t>(sequence >> 16));
  assert(frame[6] == static_cast<uint8_t>(sequence >> 24));
  assert(frame[7] == static_cast<uint8_t>(length));
  assert(frame[8] == static_cast<uint8_t>(length >> 8));
  std::vector<uint8_t> covered(frame.begin() + 2, frame.begin() + 9 + length);
  const uint32_t crc = imageCrc(covered);
  for (int i = 0; i < 4; ++i)
    assert(frame[9 + length + i] == static_cast<uint8_t>(crc >> (8 * i)));
}

struct ProtocolRig {
  std::vector<std::string> commands;
  BtLinkProtocol link{send, event, this};
  BtFirmwareSender* sender = nullptr;
  static void send(void* c, const char* text) {
    static_cast<ProtocolRig*>(c)->commands.push_back(text);
  }
  static void event(void* c, BtLinkEvent kind) {
    auto& rig = *static_cast<ProtocolRig*>(c);
    if (kind == BtLinkEvent::UpdateIdentity && rig.sender) {
      const auto& state = rig.link.state();
      rig.sender->onIdentity(state.runtimeAvailable, state.protocolVersion,
                             state.firmwareVersion, state.capabilities,
                             state.otaState);
    }
  }
  static bool observedLine(void* c, const char* value, uint32_t now) {
    auto& rig = *static_cast<ProtocolRig*>(c);
    return rig.sender && rig.sender->onLine(value, now);
  }
  void observe(BtFirmwareSender& source) {
    sender = &source;
    link.setLineObserver(observedLine);
  }
  void line(const char* text, uint32_t now) {
    for (const char* p = text; *p; ++p) link.feed(*p, now);
    link.feed('\n', now);
  }
};

void prepareIdentity(Rig& rig, ProtocolRig& protocol) {
  rig.begin();
  assert(rig.sender.onLine("FW_ACK 0 1", rig.now));
  rig.pump(BtFirmwareSender::State::WaitVerify);
  assert(rig.sender.onLine("FW_VERIFY", rig.now));
  assert(rig.sender.onLine("FW_OK", rig.now));
  protocol.link.begin(0);
  protocol.commands.clear();
  protocol.observe(rig.sender);
  protocol.link.setUpdateExclusive(true);
  protocol.line("READY", rig.now + 1);
  assert(rig.sender.takeStatusProbe());
  protocol.link.requestStatus(rig.now + 1);
  assert(protocol.commands.size() == 1 && protocol.commands.back() == "GET_STATUS");
}

void identitySnapshot(ProtocolRig& protocol, uint32_t& now,
                      const char* version, const char* otaState) {
  protocol.line("STATUS_BEGIN", now++);
  protocol.line("PROTO 2", now++);
  protocol.line(version, now++);
  protocol.line("CAPS A2DP FW_UPDATE", now++);
  if (otaState) protocol.line(otaState, now++);
  protocol.line("STATUS_END", now++);
}
}  // namespace

int main() {
  using State = BtFirmwareSender::State;
  using Error = BtFirmwareSender::Error;
  {
    Rig r(2048);
    r.tx.space = 1024;
    assert(r.sender.start(r.image, r.pre(), r.now));
    r.pump(State::WaitReady);
    r.tx.clear();
    assert(r.sender.onLine("FW_READY 1024", r.now));
    r.sender.tick(r.now++);
    assert(r.tx.sent.size() == 512 && r.tx.lastRequested == 512);
    assert(r.sender.progress().state == State::SendingData);
    assert(r.sender.progress().confirmedBytes == 0);
    r.sender.tick(r.now++);
    assert(r.tx.sent.size() == 1024);
    r.sender.tick(r.now++);
    assert(r.tx.lastRequested == 13 && r.tx.sent.size() == 1037);
    assert(r.sender.progress().state == State::WaitAck);
    checkFrame(r.tx.sent, 1, 0, 1024);
    const auto first = r.tx.sent;
    r.now += 4000;
    r.sender.tick(r.now);
    assert(r.tx.sent == first);  // No second frame without ACK.
    assert(r.sender.onLine("FW_NACK 0 FRAME_CRC", r.now));
    r.tx.clear();
    r.pump(State::WaitAck);
    assert(r.tx.sent == first);  // Retransmission is byte-for-byte identical.
  }
  {
    Rig r(1024);
    r.tx.space = 1024;
    assert(r.sender.start(r.image, r.pre(), r.now));
    r.pump(State::WaitReady);
    r.tx.clear();
    assert(r.sender.onLine("FW_READY 1024", r.now));
    r.tx.space = 80;
    r.sender.tick(r.now++);
    assert(r.tx.sent.size() == 80 && r.tx.lastRequested == 80);
    r.tx.space = 0;
    r.sender.tick(r.now++);
    assert(r.tx.sent.size() == 80 && r.sender.progress().state == State::SendingData);
    r.tx.space = 1024;
    r.tx.maxAccepted = 73;
    r.sender.tick(r.now++);
    assert(r.tx.lastRequested == 512 && r.tx.sent.size() == 153);
    assert(r.sender.progress().state == State::SendingData);
    r.tx.maxAccepted = 0;
    r.sender.tick(r.now++);
    assert(r.tx.sent.size() == 153 && r.sender.progress().state == State::SendingData);
    r.tx.maxAccepted = 512;
    r.pump(State::WaitAck);
    checkFrame(r.tx.sent, 1, 0, 1024);
    assert(r.sender.progress().confirmedBytes == 0);
  }
  {
    auto reach971 = [](Rig& r) {
      r.begin();
      for (uint32_t seq = 0; seq < 971; ++seq) {
        char ack[48];
        snprintf(ack, sizeof(ack), "FW_ACK %lu %lu",
                 static_cast<unsigned long>(seq),
                 static_cast<unsigned long>((seq + 1) * 1024));
        assert(r.sender.onLine(ack, r.now));
        r.tx.clear();
        r.pump(State::WaitAck);
      }
      assert(r.sender.progress().confirmedBytes == 994304);
    };
    Rig ack(1138000);
    reach971(ack);
    DiagnosticCapture log;
    ack.sender.setDiagnosticLogger(&DiagnosticCapture::log, &log);
    assert(ack.sender.onLine("FW_ACK 971 995328", ack.now));
    assert(ack.sender.progress().confirmedBytes == 995328);
    assert(ack.sender.progress().state == State::SendingData);
    assert(log.contains("state=WaitAck seq=971 confirmed=994304 line=\"FW_ACK 971 995328\""));

    Rig nack(1138000);
    reach971(nack);
    nack.sender.setDiagnosticLogger(&DiagnosticCapture::log, &log);
    log.lines.clear();
    assert(nack.sender.onLine("FW_NACK 971 FRAME_CRC", nack.now));
    assert(nack.sender.progress().state == State::SendingData);
    nack.pump(State::WaitAck);
    assert(nack.sender.onLine("FW_NACK 971 WRONG_SEQUENCE", nack.now));
    assert(nack.sender.progress().state == State::SendingData);
    assert(nack.sender.progress().error == Error::None);
    assert(log.contains("FW_NACK 971 FRAME_CRC"));
    assert(log.contains("FW_NACK 971 WRONG_SEQUENCE"));
  }
  {
    auto sendAlmostAllOfSecondFrame = [](Rig& r) {
      r.begin();
      assert(r.sender.onLine("FW_ACK 0 1024", r.now));
      r.tx.clear();
      for (int i = 0; i < 60; ++i) r.sender.tick(r.now++);
      assert(r.tx.sent.size() == 1020);
      assert(r.sender.progress().state == State::SendingData);
      assert(r.sender.progress().confirmedBytes == 1024);
    };
    Rig nack(3072);
    sendAlmostAllOfSecondFrame(nack);
    assert(nack.sender.onLine("FW_NACK 1 FRAME_CRC", nack.now));
    assert(nack.sender.progress().error == Error::None);
    assert(nack.sender.progress().state == State::SendingData);
    assert(nack.tx.sent.size() == 1020);  // No retry before the first frame is complete.
    nack.sender.tick(nack.now++);
    assert(nack.tx.sent.size() == 1037);
    const auto firstAttempt = nack.tx.sent;
    assert(nack.sender.progress().state == State::SendingData);
    nack.pump(State::WaitAck);
    assert(nack.tx.sent.size() == firstAttempt.size() * 2);
    assert(std::equal(firstAttempt.begin(), firstAttempt.end(),
                      nack.tx.sent.begin() + firstAttempt.size()));
    assert(nack.sender.progress().confirmedBytes == 1024);
    assert(nack.sender.progress().error == Error::None);

    Rig ack(3072);
    sendAlmostAllOfSecondFrame(ack);
    assert(ack.sender.onLine("FW_ACK 1 2048", ack.now));
    assert(ack.sender.progress().confirmedBytes == 1024);
    assert(ack.tx.sent.size() == 1020);
    ack.sender.tick(ack.now++);
    assert(ack.tx.sent.size() == 1037);
    assert(ack.sender.progress().confirmedBytes == 2048);
    assert(ack.sender.progress().state == State::SendingData);
    ack.tx.clear();
    ack.pump(State::WaitAck);
    checkFrame(ack.tx.sent, 1, 2, 1024);
  }
  {
    const struct { const char* line; const char* category; } malformed[] = {
      {"FW_ACK 0", "BT FW malformed FW_ACK"},
      {"FW_ACK 0 1024x", "BT FW malformed FW_ACK"},
      {"FW_NACK 0 ", "BT FW malformed FW_NACK"},
      {"FW_VERIFY", "BT FW unexpected FW_* while waiting ACK"}
    };
    for (const auto& sample : malformed) {
      Rig r(2048); r.begin();
      DiagnosticCapture log;
      r.sender.setDiagnosticLogger(&DiagnosticCapture::log, &log);
      assert(r.sender.onLine(sample.line, r.now));
      assert(r.sender.progress().error == Error::InvalidResponse);
      assert(log.contains(sample.category));
      assert(log.contains("BT FW invalid response: state=WaitAck seq=0 confirmed=0"));
      assert(log.contains(sample.line));
    }
    Rig r(2048); r.begin();
    DiagnosticCapture log;
    r.sender.setDiagnosticLogger(&DiagnosticCapture::log, &log);
    const std::string longLine = "FW_ACK " + std::string(200, '9');
    assert(r.sender.onLine(longLine.c_str(), r.now));
    assert(log.contains("[truncated]"));
    for (const auto& line : log.lines) assert(line.size() < 192);
  }
  {
    Rig r(1);
    auto p = r.pre();
    p.online = false;
    assert(!r.sender.start(r.image, p, r.now));
    assert(r.sender.progress().error == Error::Offline);
    p = r.pre(); p.capabilities = "A2DP FW_UPDATE_LATER";
    assert(!r.sender.start(r.image, p, r.now));
    assert(r.sender.progress().error == Error::Unsupported);
    p = r.pre(); p.protocol = 3;
    assert(!r.sender.start(r.image, p, r.now));
    assert(r.sender.progress().error == Error::Unsupported);
    p = r.pre(); p.activeBtSource = true;
    assert(!r.sender.start(r.image, p, r.now));
    assert(r.sender.progress().error == Error::ActiveBtSource);
    assert(r.tx.sent.empty());
    r.image.label = std::string(25, 'X');
    assert(!r.sender.start(r.image, r.pre(), r.now));
    assert(r.sender.progress().error == Error::InvalidImage);
  }
  {
    Rig r(1030);
    r.begin();
    checkFrame(r.tx.sent, 1, 0, 1024);
    assert(r.tx.sent[9] == 0 && r.tx.sent[10] == 1);
    assert(r.image.reads == 1 && r.sender.progress().confirmedBytes == 0);
    const auto first = r.tx.sent;
    r.sender.tick(r.now + 4000);
    assert(r.tx.sent == first);  // Exactly one frame in flight.
    r.now += 5001;
    r.sender.tick(r.now);
    r.pump(State::WaitAck);
    assert(r.tx.sent.size() == first.size() * 2);
    assert(std::equal(first.begin(), first.end(), r.tx.sent.begin() + first.size()));
    assert(r.image.reads == 1);
    r.tx.clear();
    assert(r.sender.onLine("FW_NACK 0 FRAME_CRC", r.now));
    r.pump(State::WaitAck);
    assert(r.tx.sent == first);
    r.tx.clear();
    assert(r.sender.onLine("FW_ACK 0 1024", r.now));
    assert(r.sender.progress().confirmedBytes == 1024);
    assert(r.sender.progress().percent == 99);
    r.pump(State::WaitAck);
    checkFrame(r.tx.sent, 1, 1, 6);
    assert(r.tx.sent[9] == r.image.bytes[1024]);
    r.tx.clear();
    assert(r.sender.onLine("FW_ACK 1 1030", r.now));
    assert(r.sender.progress().percent == 100);
    assert(r.sender.progress().state == State::SendingEnd);
    r.pump(State::WaitVerify);
    checkFrame(r.tx.sent, 2, 2, 0);
    assert(r.sender.onLine("FW_VERIFY", r.now));
    assert(r.sender.progress().state == State::WaitOk);
    assert(r.sender.onLine("FW_OK", r.now));
    assert(r.sender.progress().state == State::WaitIdentity);
    assert(r.sender.progress().percent == 100);
    assert(!r.sender.takeStatusProbe());
    assert(!r.sender.onLine("READY", r.now));
    assert(r.sender.takeStatusProbe());
    assert(!r.sender.takeStatusProbe());
    r.sender.onIdentity(true, 2, "0.6.1-dev", "A2DP FW_UPDATE", BtOtaState::Valid);
    assert(r.sender.progress().state == State::Success);
    assert(!r.sender.exclusive());
  }
  {
    Rig stale(1);
    stale.image.label = "0.6.2-dev";
    DiagnosticCapture log;
    stale.sender.setDiagnosticLogger(&DiagnosticCapture::log, &log);
    stale.begin();
    assert(log.contains("identity start: expectedVersion=\"0.6.2-dev\""));
    assert(stale.sender.onLine("FW_ACK 0 1", stale.now));
    stale.pump(State::WaitVerify);
    assert(stale.sender.onLine("FW_VERIFY", stale.now));
    assert(stale.sender.onLine("FW_OK", stale.now));
    assert(log.contains("identity FW_OK: expectedVersion=\"0.6.2-dev\""));
    stale.sender.onIdentity(true, 2, "0.6.1-dev", "FW_UPDATE", BtOtaState::Valid);
    assert(stale.sender.progress().state == State::WaitIdentity);  // No READY yet.
    assert(!stale.sender.onLine("READY", stale.now));
    stale.sender.onIdentity(true, 2, "0.6.1-dev", "FW_UPDATE", BtOtaState::Valid);
    assert(stale.sender.progress().error == Error::VersionMismatch);
    assert(log.contains("reportedVersion=\"0.6.1-dev\""));

    Rig fresh(1);
    fresh.image.label = "0.6.2-dev";
    fresh.begin();
    assert(fresh.sender.onLine("FW_ACK 0 1", fresh.now));
    fresh.pump(State::WaitVerify);
    assert(fresh.sender.onLine("FW_VERIFY", fresh.now));
    assert(fresh.sender.onLine("FW_OK", fresh.now));
    assert(!fresh.sender.onLine("READY", fresh.now));
    fresh.sender.onIdentity(true, 2, "0.6.2-dev", "FW_UPDATE", BtOtaState::Valid);
    assert(fresh.sender.progress().state == State::Success);
    assert(fresh.sender.start(fresh.image, fresh.pre(), fresh.now));
    assert(fresh.sender.progress().state == State::SendingBegin);
    assert(fresh.sender.progress().error == Error::None);
  }
  {
    Rig rig(1);
    rig.image.label = "0.6.2-dev";
    ProtocolRig protocol;
    DiagnosticCapture log;
    rig.sender.setDiagnosticLogger(&DiagnosticCapture::log, &log);
    prepareIdentity(rig, protocol);
    uint32_t now = 1000;
    identitySnapshot(protocol, now, "FW_VERSION 0.6.2-dev", "OTA_STATE PENDING_VERIFY");
    assert(rig.sender.progress().state == State::WaitIdentity);
    assert(rig.sender.progress().phase == BtFirmwareSender::Phase::WaitingForBt);
    assert(log.contains("otaState=PENDING_VERIFY snapshotComplete=1"));
    const uint32_t firstSnapshotAt = now - 1;
    rig.sender.tick(firstSnapshotAt + BtFirmwareSender::OtaStatusProbeIntervalMs - 1);
    assert(!rig.sender.takeStatusProbe());
    rig.sender.tick(firstSnapshotAt + BtFirmwareSender::OtaStatusProbeIntervalMs);
    assert(rig.sender.takeStatusProbe());
    protocol.link.requestStatus(now++);
    assert(protocol.commands.size() == 2 && protocol.commands.back() == "GET_STATUS");
    identitySnapshot(protocol, now, "FW_VERSION 0.6.2-dev", "OTA_STATE PENDING_VERIFY");
    assert(rig.sender.progress().state == State::WaitIdentity);
    assert(!rig.sender.takeStatusProbe());
    const uint32_t secondSnapshotAt = now - 1;
    rig.sender.tick(secondSnapshotAt + BtFirmwareSender::OtaStatusProbeIntervalMs);
    assert(rig.sender.takeStatusProbe());
    protocol.link.requestStatus(now++);
    identitySnapshot(protocol, now, "FW_VERSION 0.6.2-dev", "OTA_STATE VALID");
    assert(rig.sender.progress().state == State::Success);
    assert(rig.sender.progress().phase == BtFirmwareSender::Phase::Success);
    assert(log.contains("otaState=VALID snapshotComplete=1"));
  }
  {
    const struct { const char* state; Error error; } terminal[] = {
      {"OTA_STATE CONFIRM_FAILED", Error::OtaConfirmFailed},
      {"OTA_STATE UNKNOWN", Error::OtaStateUnknown},
      {"OTA_STATE NOT_PENDING", Error::OtaNotPending}
    };
    for (const auto& sample : terminal) {
      Rig rig(1); rig.image.label = "0.6.2-dev";
      ProtocolRig protocol;
      prepareIdentity(rig, protocol);
      uint32_t now = 1000;
      identitySnapshot(protocol, now, "FW_VERSION 0.6.2-dev", sample.state);
      assert(rig.sender.progress().state == State::Error);
      assert(rig.sender.progress().error == sample.error);
      assert(!rig.sender.exclusive());  // No binary ABORT after FW_OK.
      assert(!rig.sender.takeStatusProbe());
    }
    Rig wrong(1); wrong.image.label = "0.6.2-dev";
    ProtocolRig protocol;
    prepareIdentity(wrong, protocol);
    uint32_t now = 1000;
    identitySnapshot(protocol, now, "FW_VERSION 0.6.1-dev", "OTA_STATE VALID");
    assert(wrong.sender.progress().error == Error::VersionMismatch);
  }
  {
    Rig rig(1); rig.image.label = "0.6.2-dev";
    ProtocolRig protocol;
    prepareIdentity(rig, protocol);
    uint32_t now = 1000;
    protocol.line("PROTO 2", now++);
    protocol.line("FW_VERSION 0.6.2-dev", now++);
    protocol.line("CAPS FW_UPDATE", now++);
    protocol.line("STATUS_BEGIN", now++);
    protocol.line("OTA_STATE VALID", now++);
    protocol.line("STATUS_END", now++);
    assert(rig.sender.progress().state == State::WaitIdentity);
    identitySnapshot(protocol, now, "FW_VERSION 0.6.2-dev", nullptr);
    assert(rig.sender.progress().state == State::WaitIdentity);  // Old V0.
    identitySnapshot(protocol, now, "FW_VERSION 0.6.2-dev", "OTA_STATE VALID");
    assert(rig.sender.progress().state == State::Success);
  }
  {
    Rig rig(1); rig.image.label = "0.6.2-dev";
    rig.now = UINT32_MAX - 10000U;
    ProtocolRig protocol;
    prepareIdentity(rig, protocol);
    const uint32_t fwOkAt = rig.now;
    uint32_t now = fwOkAt + 1000U;
    identitySnapshot(protocol, now, "FW_VERSION 0.6.2-dev", "OTA_STATE PENDING_VERIFY");
    now = fwOkAt + 10000U;
    identitySnapshot(protocol, now, "FW_VERSION 0.6.2-dev", "OTA_STATE PENDING_VERIFY");
    now = fwOkAt + 29000U;
    identitySnapshot(protocol, now, "FW_VERSION 0.6.2-dev", "OTA_STATE PENDING_VERIFY");
    rig.sender.tick(fwOkAt + BtFirmwareSender::IdentityTimeoutMs);
    assert(rig.sender.progress().error == Error::IdentityTimeout);
    assert(!rig.sender.takeStatusProbe());
  }
  {
    Rig r(1); r.begin();
    assert(r.sender.onLine("FW_ACK 1 1", r.now));
    assert(r.sender.progress().error == Error::AckSequence);
    assert(r.sender.exclusive());  // Best-effort ABORT still queued.
    r.pump(State::Error);  // Already terminal; pump not needed for ABORT.
    for (int i = 0; i < 10; ++i) r.sender.tick(r.now++);
    assert(!r.sender.exclusive());
    checkFrame(std::vector<uint8_t>(r.tx.sent.end() - 13, r.tx.sent.end()), 3, 0, 0);
    assert(r.sender.start(r.image, r.pre(), r.now));
  }
  {
    Rig r(1); r.begin();
    assert(r.sender.onLine("FW_ACK 0 2", r.now));
    assert(r.sender.progress().error == Error::AckBytes);
  }
  {
    Rig r(1);
    assert(r.sender.start(r.image, r.pre(), r.now));
    assert(r.tx.sent.empty());  // FW_BEGIN also uses bounded TX.
    r.pump(State::WaitReady);
    assert(r.sender.onLine("FW_READY 512", r.now));
    assert(r.sender.progress().error == Error::InvalidReady);
  }
  {
    Rig r(1);
    assert(r.sender.start(r.image, r.pre(), r.now));
    r.pump(State::WaitReady);
    r.now += 5001;
    r.sender.tick(r.now);
    assert(r.sender.progress().error == Error::ReadyTimeout);
    assert(r.sender.exclusive());  // Best-effort ABORT may free BT binary mode.
  }
  {
    Rig r(1); r.begin();
    const auto frame = r.tx.sent;
    r.tx.clear();
    for (int n = 0; n < 3; ++n) {
      assert(r.sender.onLine("FW_NACK 0 WRONG_SEQUENCE", r.now));
      r.pump(State::WaitAck);
      assert(r.tx.sent.size() == frame.size() * static_cast<size_t>(n + 1));
    }
    assert(r.sender.onLine("FW_NACK 0 WRONG_SEQUENCE", r.now));
    assert(r.sender.progress().error == Error::RetryLimit);
  }
  {
    Rig r(1); r.begin();
    assert(r.sender.onLine("FW_ERR WRITE_FAILED", r.now));
    assert(r.sender.progress().error == Error::RemoteError);
    assert(!r.sender.exclusive());
  }
  {
    Rig r(1); r.begin();
    r.sender.onLine("FW_ACK 0 1", r.now);
    r.pump(State::WaitVerify);
    r.now += 5001;
    r.sender.tick(r.now);
    assert(r.sender.progress().error == Error::VerifyTimeout);
  }
  {
    Rig r(1); r.begin();
    r.sender.abort();
    assert(r.sender.progress().state == State::Aborted);
    for (int i = 0; i < 10; ++i) r.sender.tick(r.now++);
    assert(!r.sender.exclusive());
  }
  {
    Rig r(1); r.begin();
    r.sender.onLine("FW_ACK 0 1", r.now);
    r.pump(State::WaitVerify);
    r.sender.onLine("FW_VERIFY", r.now);
    r.sender.onLine("FW_OK", r.now);
    r.sender.onLine("READY", r.now);
    r.sender.onIdentity(true, 2, "WRONG", "FW_UPDATE", BtOtaState::Valid);
    assert(r.sender.progress().error == Error::VersionMismatch);
  }
  {
    Rig r(1); r.begin();
    r.sender.onLine("FW_ACK 0 1", r.now);
    r.pump(State::WaitVerify);
    r.sender.onLine("FW_VERIFY", r.now);
    r.sender.onLine("FW_OK", r.now);
    r.now += 30001;
    r.sender.tick(r.now);
    assert(r.sender.progress().error == Error::IdentityTimeout);
  }
  {
    Rig r(1);
    r.now = UINT32_MAX - 20;
    r.begin();
    r.now += 5001;
    r.sender.tick(r.now);
    assert(r.sender.progress().state == State::SendingData);
  }
  {
    ProtocolRig p;
    p.link.begin(0);
    p.line("PROTO 2", 1);
    p.line("STATUS_BEGIN", 2);
    p.line("CONNECTED", 3);
    p.line("STATUS_END", 4);
    p.link.setUpdateExclusive(true);
    const size_t before = p.commands.size();
    assert(!p.link.play() && !p.link.setVolume(32));
    p.link.tick(20000);
    assert(p.commands.size() == before);
    assert(p.link.state().runtimeAvailable);
    p.link.setUpdateExclusive(false);
    assert(p.link.play());
  }
  std::puts("BT firmware sender native PASS");
}
