#ifndef VOXONE_MPEG_TS_AAC_PARSER_H
#define VOXONE_MPEG_TS_AAC_PARSER_H

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace audio {

enum class TsParseStatus : uint8_t {
  Ignored = 0,
  NeedMoreData,
  AudioData,
  Discontinuity,
  Malformed,
  UnsupportedAudio,
};

enum class TsParseError : uint8_t {
  None = 0,
  PacketSize,
  SyncByte,
  Header,
  TransportError,
  Scrambled,
  AdaptationField,
  PointerField,
  PsiContinuity,
  PsiSectionLength,
  PatSection,
  PmtSection,
  PesContinuity,
  PesHeader,
};

struct TsParseResult {
  TsParseStatus status;
  TsParseError error;
  uint16_t audioOffset;
  uint16_t audioLength;
};

const char* tsParseErrorText(TsParseError error);

enum class TsRecoveryAction : uint8_t {
  Continue = 0,
  DropPacket,
  AbortStream,
};

enum class TsRecoveryAbortReason : uint8_t {
  None = 0,
  UnsupportedAudio,
  MalformedBudget,
  NoAudioProgress,
};

const char* tsRecoveryAbortReasonText(TsRecoveryAbortReason reason);

// Tracks demultiplexer progress across HLS segment boundaries. Only accepted
// AAC bytes prove progress here; they do not prove that the decoder made PCM.
class TsStreamRecoveryPolicy {
 public:
  static constexpr uint8_t kMaxMalformedWithoutAudio = 8;
  static constexpr uint16_t kMaxPacketsWithoutAudio = 8192;

  TsStreamRecoveryPolicy() { reset(); }
  void reset();
  TsRecoveryAction observe(const TsParseResult& parsed);
  uint8_t malformedWithoutAudio() const { return malformedWithoutAudio_; }
  uint16_t packetsWithoutAudio() const { return packetsWithoutAudio_; }
  TsRecoveryAbortReason abortReason() const { return abortReason_; }

 private:
  uint16_t packetsWithoutAudio_;
  uint8_t malformedWithoutAudio_;
  TsRecoveryAbortReason abortReason_;
};

// Bounded MPEG-TS demultiplexer for the AAC ADTS path used by Audio.cpp.
// The parser owns no packet bytes. AudioData offsets always refer to the
// 188-byte packet passed to parse().
class MpegTsAacParser {
 public:
  static constexpr size_t kPacketSize = 188;
  static constexpr size_t kMaxPmtPids = 4;
  static constexpr size_t kMaxPsiSectionBytes = 1024;
  static constexpr uint16_t kNullPid = 0x1FFF;
  static constexpr uint16_t kInvalidPid = 0xFFFF;

  MpegTsAacParser();

  void reset();
  void beginSegment();
  TsParseResult parse(const uint8_t* packet, size_t length);

  size_t pmtPidCount() const { return pmtPidCount_; }
  uint16_t pmtPid(size_t index) const {
    return index < pmtPidCount_ ? pmtPids_[index] : kInvalidPid;
  }
  bool pmtPidOverflow() const { return pmtPidOverflow_; }
  uint16_t audioPid() const { return audioPid_; }
  bool latmDetected() const { return latmDetected_; }

 private:
  struct PacketView {
    uint16_t pid;
    uint8_t continuity;
    size_t payloadOffset;
    size_t payloadLength;
    bool payloadUnitStart;
    bool hasPayload;
    bool discontinuity;
  };

  struct PsiAssembly {
    uint8_t bytes[kMaxPsiSectionBytes];
    size_t size;
    size_t expected;
    uint16_t pid;
    uint8_t continuity;
    bool active;
  };

  TsParseResult parseHeader(const uint8_t* packet, size_t length,
                            PacketView& view) const;
  TsParseResult parsePsiPacket(const uint8_t* packet,
                               const PacketView& view);
  TsParseResult processPsiSections(uint16_t pid, uint8_t continuity,
                                   const uint8_t* data, size_t length);
  TsParseResult appendPsi(uint16_t pid, uint8_t continuity,
                          const uint8_t* data, size_t length);
  TsParseResult finishPsiSection(uint16_t pid, const uint8_t* section,
                                 size_t length);
  TsParseResult parsePat(const uint8_t* section, size_t length);
  TsParseResult parsePmt(const uint8_t* section, size_t length);
  TsParseResult parsePes(const uint8_t* packet, const PacketView& view);

  bool isPmtPid(uint16_t pid) const;
  void resetPsi();
  void resetPes();
  void resetProgramMap();
  void startPsi(uint16_t pid, uint8_t continuity, const uint8_t* data,
                size_t length, size_t expected);

  uint16_t pmtPids_[kMaxPmtPids];
  size_t pmtPidCount_;
  bool pmtPidOverflow_;
  uint16_t audioPid_;
  bool latmDetected_;

  // One bounded in-flight PSI section is sufficient for the normal PAT -> PMT
  // sequence. A different interleaved PSI PID replaces an incomplete section
  // safely; it can reduce program discovery but cannot exceed this buffer.
  PsiAssembly psi_;

  size_t pesRemaining_;
  bool pesActive_;
  bool pesUnbounded_;
  uint8_t audioContinuity_;
  bool audioContinuityValid_;
};

}  // namespace audio
}  // namespace voxone

#endif
