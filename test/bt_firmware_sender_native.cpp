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
  int availableForWrite() override { return space; }
  size_t write(const uint8_t* bytes, size_t n) override {
    sent.insert(sent.end(), bytes, bytes + n);
    return n;
  }
  void clear() { sent.clear(); }
  std::string text() const { return std::string(sent.begin(), sent.end()); }
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
  static void send(void* c, const char* text) {
    static_cast<ProtocolRig*>(c)->commands.push_back(text);
  }
  static void event(void*, BtLinkEvent) {}
  void line(const char* text, uint32_t now) {
    for (const char* p = text; *p; ++p) link.feed(*p, now);
    link.feed('\n', now);
  }
};
}  // namespace

int main() {
  using State = BtFirmwareSender::State;
  using Error = BtFirmwareSender::Error;
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
    r.sender.onIdentity(true, 2, "0.6.1-dev", "A2DP FW_UPDATE");
    assert(r.sender.progress().state == State::Success);
    assert(!r.sender.exclusive());
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
    r.sender.onIdentity(true, 2, "WRONG", "FW_UPDATE");
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
