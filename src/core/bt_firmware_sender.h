#ifndef VOXONE_BT_FIRMWARE_SENDER_H
#define VOXONE_BT_FIRMWARE_SENDER_H

#include <stddef.h>
#include <stdint.h>

// The caller retains ownership for the entire transfer. read() must support
// random access so an identical frame can be retransmitted without staging.
class BtFirmwareImage {
 public:
  virtual ~BtFirmwareImage() {}
  virtual uint32_t size() const = 0;
  virtual uint32_t crc32() const = 0;
  virtual const char* version() const = 0;
  virtual size_t read(uint32_t offset, uint8_t* output, size_t length) = 0;
};

class BtFirmwareTransport {
 public:
  virtual ~BtFirmwareTransport() {}
  virtual int availableForWrite() = 0;
  virtual size_t write(const uint8_t* bytes, size_t length) = 0;
};

class BtFirmwareSender {
 public:
  static constexpr size_t PayloadSize = 1024;
  static constexpr size_t FrameSize = 9 + PayloadSize + 4;
  static constexpr size_t MaxTxPerTick = 64;
  static constexpr uint32_t ReplyTimeoutMs = 5000;
  static constexpr uint32_t IdentityTimeoutMs = 30000;
  static constexpr uint8_t MaxRetransmissions = 3;

  enum class State : uint8_t {
    Idle, SendingBegin, WaitReady, SendingData, WaitAck, SendingEnd,
    WaitVerify, WaitOk, WaitIdentity, Success, Error, Aborted
  };
  enum class Phase : uint8_t {
    Prepare, SendingToBt, Verifying, RestartingBt, WaitingForBt,
    Success, Error
  };
  enum class Error : uint8_t {
    None, Busy, Offline, Unsupported, ActiveBtSource, InvalidImage,
    ReadFailed, Transport, ReadyTimeout, InvalidReady, RemoteError,
    InvalidResponse, AckSequence, AckBytes, RetryLimit, VerifyTimeout,
    IdentityTimeout, VersionMismatch, IdentityMismatch, Aborted
  };
  struct Preconditions {
    bool online = false;
    uint8_t protocol = 0;
    const char* capabilities = nullptr;
    bool activeBtSource = false;
  };
  struct Progress {
    enum class Target : uint8_t { VoxOneBt };
    Target target = Target::VoxOneBt;
    State state = State::Idle;
    Phase phase = Phase::Prepare;
    Error error = Error::None;
    uint32_t totalBytes = 0;
    uint32_t confirmedBytes = 0;
    uint8_t percent = 0;
  };

  explicit BtFirmwareSender(BtFirmwareTransport& transport)
      : transport_(transport) {}
  bool start(BtFirmwareImage& image, const Preconditions& preconditions,
             uint32_t nowMs);
  void tick(uint32_t nowMs);
  bool onLine(const char* line, uint32_t nowMs);
  void onIdentity(bool online, uint8_t protocol, const char* version,
                  const char* capabilities);
  void abort();
  bool exclusive() const;
  bool takeStatusProbe();
  Progress progress() const;
  static bool hasCapability(const char* capabilities, const char* token);

 private:
  bool prepareData();
  void prepareEnd();
  void prepareAbort();
  void encodeFrame(uint8_t type, uint32_t sequence, size_t payloadLength);
  void transmit(uint32_t nowMs);
  void retry(uint32_t nowMs);
  void fail(Error error, bool abortRemote);

  BtFirmwareTransport& transport_;
  BtFirmwareImage* image_ = nullptr;
  uint8_t frame_[FrameSize]{};
  char expectedVersion_[25]{};
  size_t txLength_ = 0;
  size_t txOffset_ = 0;
  uint32_t total_ = 0;
  uint32_t confirmed_ = 0;
  uint32_t sequence_ = 0;
  uint32_t deadlineStartMs_ = 0;
  uint32_t abortProgressMs_ = 0;
  uint32_t nowMs_ = 0;
  uint8_t retransmissions_ = 0;
  State state_ = State::Idle;
  Phase phase_ = Phase::Prepare;
  Error error_ = Error::None;
  bool readySeen_ = false;
  bool statusProbe_ = false;
  bool abortTx_ = false;
};

#endif
