#include "mpeg_ts_aac_parser.h"

#include <string.h>

namespace voxone {
namespace audio {
namespace {

TsParseResult result(TsParseStatus status,
                     TsParseError error = TsParseError::None,
                     size_t offset = 0, size_t length = 0) {
  return {status, error, static_cast<uint16_t>(offset),
          static_cast<uint16_t>(length)};
}

uint16_t readPid(const uint8_t* bytes) {
  return static_cast<uint16_t>(((bytes[0] & 0x1F) << 8) | bytes[1]);
}

size_t sectionLength(const uint8_t* section) {
  return static_cast<size_t>(((section[1] & 0x0F) << 8) | section[2]);
}

}  // namespace

const char* tsParseErrorText(TsParseError error) {
  switch (error) {
    case TsParseError::None: return "none";
    case TsParseError::PacketSize: return "packet size";
    case TsParseError::SyncByte: return "sync byte";
    case TsParseError::Header: return "TS header";
    case TsParseError::TransportError: return "transport error indicator";
    case TsParseError::Scrambled: return "scrambled payload";
    case TsParseError::AdaptationField: return "adaptation field";
    case TsParseError::PointerField: return "PSI pointer field";
    case TsParseError::PsiContinuity: return "PSI continuity";
    case TsParseError::PsiSectionLength: return "PSI section length";
    case TsParseError::PatSection: return "PAT section";
    case TsParseError::PmtSection: return "PMT section";
    case TsParseError::PesContinuity: return "PES continuity";
    case TsParseError::PesHeader: return "PES header";
  }
  return "unknown";
}

const char* tsRecoveryAbortReasonText(TsRecoveryAbortReason reason) {
  switch (reason) {
    case TsRecoveryAbortReason::None: return "none";
    case TsRecoveryAbortReason::UnsupportedAudio: return "unsupported audio";
    case TsRecoveryAbortReason::MalformedBudget: return "malformed packet budget";
    case TsRecoveryAbortReason::NoAudioProgress: return "no AAC demux progress";
  }
  return "unknown";
}

void TsStreamRecoveryPolicy::reset() {
  packetsWithoutAudio_ = 0;
  malformedWithoutAudio_ = 0;
  abortReason_ = TsRecoveryAbortReason::None;
}

TsRecoveryAction TsStreamRecoveryPolicy::observe(
    const TsParseResult& parsed) {
  abortReason_ = TsRecoveryAbortReason::None;
  if (parsed.status == TsParseStatus::UnsupportedAudio) {
    abortReason_ = TsRecoveryAbortReason::UnsupportedAudio;
    return TsRecoveryAction::AbortStream;
  }
  if (parsed.status == TsParseStatus::AudioData) {
    packetsWithoutAudio_ = 0;
    malformedWithoutAudio_ = 0;
    return TsRecoveryAction::Continue;
  }

  if (packetsWithoutAudio_ < kMaxPacketsWithoutAudio)
    ++packetsWithoutAudio_;
  if (parsed.status == TsParseStatus::Malformed &&
      malformedWithoutAudio_ < kMaxMalformedWithoutAudio)
    ++malformedWithoutAudio_;

  if (malformedWithoutAudio_ >= kMaxMalformedWithoutAudio) {
    abortReason_ = TsRecoveryAbortReason::MalformedBudget;
    return TsRecoveryAction::AbortStream;
  }
  if (packetsWithoutAudio_ >= kMaxPacketsWithoutAudio) {
    abortReason_ = TsRecoveryAbortReason::NoAudioProgress;
    return TsRecoveryAction::AbortStream;
  }
  return parsed.status == TsParseStatus::Malformed
             ? TsRecoveryAction::DropPacket
             : TsRecoveryAction::Continue;
}

MpegTsAacParser::MpegTsAacParser() { reset(); }

void MpegTsAacParser::resetPsi() {
  psi_.size = 0;
  psi_.expected = 0;
  psi_.pid = kInvalidPid;
  psi_.continuity = 0;
  psi_.active = false;
}

void MpegTsAacParser::resetPes() {
  pesRemaining_ = 0;
  pesActive_ = false;
  pesUnbounded_ = false;
}

void MpegTsAacParser::resetProgramMap() {
  for (size_t index = 0; index < kMaxPmtPids; ++index)
    pmtPids_[index] = kInvalidPid;
  pmtPidCount_ = 0;
  pmtPidOverflow_ = false;
  audioPid_ = kInvalidPid;
  latmDetected_ = false;
  audioContinuity_ = 0;
  audioContinuityValid_ = false;
  resetPes();
}

void MpegTsAacParser::reset() {
  resetProgramMap();
  resetPsi();
}

void MpegTsAacParser::beginSegment() {
  resetPsi();
  resetPes();
  audioContinuityValid_ = false;
}

bool MpegTsAacParser::isPmtPid(uint16_t pid) const {
  for (size_t index = 0; index < pmtPidCount_; ++index) {
    if (pmtPids_[index] == pid) return true;
  }
  return false;
}

TsParseResult MpegTsAacParser::parseHeader(const uint8_t* packet,
                                            size_t length,
                                            PacketView& view) const {
  if (!packet || length != kPacketSize)
    return result(TsParseStatus::Malformed, TsParseError::PacketSize);
  if (packet[0] != 0x47)
    return result(TsParseStatus::Malformed, TsParseError::SyncByte);

  view.pid = readPid(packet + 1);
  view.payloadUnitStart = (packet[1] & 0x40) != 0;
  view.continuity = packet[3] & 0x0F;
  const uint8_t adaptationControl = (packet[3] >> 4) & 0x03;
  if (adaptationControl == 0)
    return result(TsParseStatus::Malformed, TsParseError::Header);

  view.hasPayload = (adaptationControl & 0x01) != 0;
  const bool hasAdaptation = (adaptationControl & 0x02) != 0;
  view.discontinuity = false;
  size_t payloadOffset = 4;
  if (hasAdaptation) {
    const size_t adaptationLength = packet[4];
    if (adaptationLength > kPacketSize - 5)
      return result(TsParseStatus::Malformed,
                    TsParseError::AdaptationField);
    if (adaptationLength > 0)
      view.discontinuity = (packet[5] & 0x80) != 0;
    payloadOffset = 5 + adaptationLength;
  }

  if (!view.hasPayload) {
    view.payloadOffset = kPacketSize;
    view.payloadLength = 0;
    return result(TsParseStatus::Ignored);
  }
  if (payloadOffset >= kPacketSize)
    return result(TsParseStatus::Malformed, TsParseError::AdaptationField);

  view.payloadOffset = payloadOffset;
  view.payloadLength = kPacketSize - payloadOffset;
  return result(TsParseStatus::Ignored);
}

TsParseResult MpegTsAacParser::parse(const uint8_t* packet, size_t length) {
  PacketView view{};
  const TsParseResult header = parseHeader(packet, length, view);
  if (header.status == TsParseStatus::Malformed) return header;

  // PID 0x1FFF is padding by definition. It is never a valid inactive-state
  // sentinel and can never carry audio, even when PUSI or PES-like bytes are
  // present. Structural header/adaptation validation above still applies.
  if (view.pid == kNullPid) return result(TsParseStatus::Ignored);

  const bool relevant = view.pid == 0 || isPmtPid(view.pid) ||
                        view.pid == audioPid_;
  if ((packet[1] & 0x80) != 0)
    return relevant
        ? result(TsParseStatus::Malformed, TsParseError::TransportError)
        : result(TsParseStatus::Ignored);
  if ((packet[3] & 0xC0) != 0)
    return relevant
        ? result(TsParseStatus::Malformed, TsParseError::Scrambled)
        : result(TsParseStatus::Ignored);

  if (!view.hasPayload) {
    if (view.pid == audioPid_ && view.discontinuity) {
      resetPes();
      audioContinuityValid_ = false;
    }
    return result(TsParseStatus::Ignored);
  }
  if (view.pid == 0 || isPmtPid(view.pid))
    return parsePsiPacket(packet, view);
  if (view.pid == audioPid_) return parsePes(packet, view);
  return result(TsParseStatus::Ignored);
}

void MpegTsAacParser::startPsi(uint16_t pid, uint8_t continuity,
                               const uint8_t* data, size_t length,
                               size_t expected) {
  resetPsi();
  if (length > kMaxPsiSectionBytes) return;
  memcpy(psi_.bytes, data, length);
  psi_.size = length;
  psi_.expected = expected;
  psi_.pid = pid;
  psi_.continuity = continuity;
  psi_.active = true;
}

TsParseResult MpegTsAacParser::processPsiSections(
    uint16_t pid, uint8_t continuity, const uint8_t* data, size_t length) {
  size_t cursor = 0;
  while (cursor < length) {
    if (data[cursor] == 0xFF) return result(TsParseStatus::Ignored);
    const size_t available = length - cursor;
    if (available < 3) {
      startPsi(pid, continuity, data + cursor, available, 0);
      return result(TsParseStatus::NeedMoreData);
    }
    const size_t bodyLength = sectionLength(data + cursor);
    if (bodyLength > kMaxPsiSectionBytes - 3)
      return result(TsParseStatus::Malformed,
                    TsParseError::PsiSectionLength);
    const size_t total = 3 + bodyLength;
    if (total > available) {
      startPsi(pid, continuity, data + cursor, available, total);
      return result(TsParseStatus::NeedMoreData);
    }
    const TsParseResult parsed = finishPsiSection(pid, data + cursor, total);
    if (parsed.status == TsParseStatus::Malformed ||
        parsed.status == TsParseStatus::UnsupportedAudio)
      return parsed;
    cursor += total;
  }
  return result(TsParseStatus::Ignored);
}

TsParseResult MpegTsAacParser::appendPsi(uint16_t pid, uint8_t continuity,
                                         const uint8_t* data,
                                         size_t length) {
  if (!psi_.active || psi_.pid != pid)
    return result(TsParseStatus::NeedMoreData);

  size_t consumed = 0;
  if (psi_.expected == 0) {
    const size_t needed = 3 - psi_.size;
    const size_t copied = needed < length ? needed : length;
    memcpy(psi_.bytes + psi_.size, data, copied);
    psi_.size += copied;
    consumed += copied;
    if (psi_.size < 3) {
      psi_.continuity = continuity;
      return result(TsParseStatus::NeedMoreData);
    }
    const size_t bodyLength = sectionLength(psi_.bytes);
    if (bodyLength > kMaxPsiSectionBytes - 3) {
      resetPsi();
      return result(TsParseStatus::Malformed,
                    TsParseError::PsiSectionLength);
    }
    psi_.expected = 3 + bodyLength;
  }

  const size_t room = psi_.expected - psi_.size;
  const size_t available = length - consumed;
  const size_t copied = room < available ? room : available;
  memcpy(psi_.bytes + psi_.size, data + consumed, copied);
  psi_.size += copied;
  consumed += copied;
  psi_.continuity = continuity;
  if (psi_.size < psi_.expected)
    return result(TsParseStatus::NeedMoreData);

  const size_t completedLength = psi_.expected;
  const TsParseResult parsed =
      finishPsiSection(pid, psi_.bytes, completedLength);
  resetPsi();
  if (parsed.status == TsParseStatus::Malformed ||
      parsed.status == TsParseStatus::UnsupportedAudio)
    return parsed;
  if (consumed < length)
    return processPsiSections(pid, continuity, data + consumed,
                              length - consumed);
  return result(TsParseStatus::Ignored);
}

TsParseResult MpegTsAacParser::parsePsiPacket(const uint8_t* packet,
                                               const PacketView& view) {
  const uint8_t* payload = packet + view.payloadOffset;
  const size_t length = view.payloadLength;
  if (view.discontinuity && psi_.active && psi_.pid == view.pid) resetPsi();

  if (!view.payloadUnitStart) {
    if (!psi_.active || psi_.pid != view.pid)
      return result(TsParseStatus::NeedMoreData);
    const uint8_t expected = static_cast<uint8_t>((psi_.continuity + 1) & 0x0F);
    if (!view.discontinuity && view.continuity != expected) {
      resetPsi();
      return result(TsParseStatus::Discontinuity,
                    TsParseError::PsiContinuity);
    }
    return appendPsi(view.pid, view.continuity, payload, length);
  }

  if (length < 1)
    return result(TsParseStatus::Malformed, TsParseError::PointerField);
  const size_t pointer = payload[0];
  if (pointer > length - 1)
    return result(TsParseStatus::Malformed, TsParseError::PointerField);

  if (psi_.active && psi_.pid == view.pid) {
    const uint8_t expected = static_cast<uint8_t>((psi_.continuity + 1) & 0x0F);
    if (!view.discontinuity && view.continuity != expected) {
      resetPsi();
    } else if (pointer > 0) {
      const TsParseResult previous =
          appendPsi(view.pid, view.continuity, payload + 1, pointer);
      if (previous.status == TsParseStatus::Malformed ||
          previous.status == TsParseStatus::UnsupportedAudio)
        return previous;
    }
  }
  // PUSI announces a new section after pointer bytes. Any still incomplete
  // previous section is discarded rather than read beyond its packet.
  resetPsi();
  return processPsiSections(view.pid, view.continuity,
                            payload + 1 + pointer, length - 1 - pointer);
}

TsParseResult MpegTsAacParser::finishPsiSection(uint16_t pid,
                                                 const uint8_t* section,
                                                 size_t length) {
  if (length < 3 || 3 + sectionLength(section) != length)
    return result(TsParseStatus::Malformed,
                  TsParseError::PsiSectionLength);
  return pid == 0 ? parsePat(section, length) : parsePmt(section, length);
}

TsParseResult MpegTsAacParser::parsePat(const uint8_t* section,
                                         size_t length) {
  const size_t bodyLength = sectionLength(section);
  if (section[0] != 0x00 || (section[1] & 0x80) == 0 || bodyLength < 9 ||
      length < 12)
    return result(TsParseStatus::Malformed, TsParseError::PatSection);

  const size_t entriesBegin = 8;
  const size_t crcBegin = length - 4;
  if (entriesBegin > crcBegin || (crcBegin - entriesBegin) % 4 != 0)
    return result(TsParseStatus::Malformed, TsParseError::PatSection);

  uint16_t newPids[kMaxPmtPids];
  size_t newCount = 0;
  bool overflow = false;
  for (size_t cursor = entriesBegin; cursor < crcBegin; cursor += 4) {
    const uint16_t program =
        static_cast<uint16_t>((section[cursor] << 8) | section[cursor + 1]);
    const uint16_t pid = readPid(section + cursor + 2);
    if (program == 0 || pid == kNullPid) continue;
    bool duplicate = false;
    for (size_t index = 0; index < newCount; ++index)
      duplicate = duplicate || newPids[index] == pid;
    if (duplicate) continue;
    if (newCount < kMaxPmtPids) newPids[newCount++] = pid;
    else overflow = true;
  }

  bool unchanged = newCount == pmtPidCount_;
  for (size_t index = 0; unchanged && index < newCount; ++index)
    unchanged = pmtPids_[index] == newPids[index];
  if (unchanged) {
    pmtPidOverflow_ = overflow;
    return result(TsParseStatus::Ignored);
  }

  resetProgramMap();
  for (size_t index = 0; index < newCount; ++index)
    pmtPids_[index] = newPids[index];
  pmtPidCount_ = newCount;
  pmtPidOverflow_ = overflow;
  return result(TsParseStatus::Ignored);
}

TsParseResult MpegTsAacParser::parsePmt(const uint8_t* section,
                                         size_t length) {
  const size_t bodyLength = sectionLength(section);
  if (section[0] != 0x02 || (section[1] & 0x80) == 0 || bodyLength < 13 ||
      length < 16)
    return result(TsParseStatus::Malformed, TsParseError::PmtSection);

  const size_t crcBegin = length - 4;
  const size_t programInfoLength =
      static_cast<size_t>(((section[10] & 0x0F) << 8) | section[11]);
  size_t cursor = 12 + programInfoLength;
  if (cursor > crcBegin)
    return result(TsParseStatus::Malformed, TsParseError::PmtSection);

  uint16_t adtsPid = kInvalidPid;
  bool sectionHasLatm = false;
  while (cursor < crcBegin) {
    if (crcBegin - cursor < 5)
      return result(TsParseStatus::Malformed, TsParseError::PmtSection);
    const uint8_t streamType = section[cursor];
    const uint16_t elementaryPid = readPid(section + cursor + 1);
    const size_t infoLength =
        static_cast<size_t>(((section[cursor + 3] & 0x0F) << 8) |
                            section[cursor + 4]);
    if (infoLength > crcBegin - cursor - 5)
      return result(TsParseStatus::Malformed, TsParseError::PmtSection);
    if (streamType == 0x0F && elementaryPid != kNullPid &&
        adtsPid == kInvalidPid)
      adtsPid = elementaryPid;
    if (streamType == 0x11) sectionHasLatm = true;
    cursor += 5 + infoLength;
  }

  // Commit the new codec/PID map only after the complete PMT has passed every
  // bounds check. A valid replacement PMT is authoritative: it may move ADTS
  // to another PID, change to unsupported LATM, or remove supported audio.
  const bool newLatmDetected =
      adtsPid == kInvalidPid && sectionHasLatm;
  if (audioPid_ != adtsPid || latmDetected_ != newLatmDetected) {
    resetPes();
    audioContinuityValid_ = false;
  }
  audioPid_ = adtsPid;
  latmDetected_ = newLatmDetected;
  if (newLatmDetected)
    return result(TsParseStatus::UnsupportedAudio);
  return result(TsParseStatus::Ignored);
}

TsParseResult MpegTsAacParser::parsePes(const uint8_t* packet,
                                         const PacketView& view) {
  if (view.discontinuity) {
    resetPes();
    audioContinuityValid_ = false;
    if (!view.payloadUnitStart)
      return result(TsParseStatus::Discontinuity,
                    TsParseError::PesContinuity);
  }
  if (audioContinuityValid_) {
    if (view.continuity == audioContinuity_)
      return result(TsParseStatus::Ignored);  // duplicate packet
    const uint8_t expected = static_cast<uint8_t>((audioContinuity_ + 1) & 0x0F);
    if (view.continuity != expected) {
      resetPes();
      audioContinuity_ = view.continuity;
      return result(TsParseStatus::Discontinuity,
                    TsParseError::PesContinuity);
    }
  }
  audioContinuity_ = view.continuity;
  audioContinuityValid_ = true;

  const uint8_t* payload = packet + view.payloadOffset;
  const size_t payloadLength = view.payloadLength;
  if (view.payloadUnitStart) {
    resetPes();
    if (payloadLength < 9 || payload[0] != 0x00 || payload[1] != 0x00 ||
        payload[2] != 0x01)
      return result(TsParseStatus::Malformed, TsParseError::PesHeader);
    if (payload[3] < 0xC0 || payload[3] > 0xDF)
      return result(TsParseStatus::Malformed, TsParseError::PesHeader);

    const size_t packetLength =
        static_cast<size_t>((payload[4] << 8) | payload[5]);
    const size_t optionalLength = payload[8];
    const size_t headerLength = 9 + optionalLength;
    if (headerLength > payloadLength)
      return result(TsParseStatus::Malformed, TsParseError::PesHeader);
    if (packetLength != 0 && packetLength < 3 + optionalLength)
      return result(TsParseStatus::Malformed, TsParseError::PesHeader);

    pesUnbounded_ = packetLength == 0;
    pesRemaining_ = pesUnbounded_ ? 0 : packetLength - 3 - optionalLength;
    pesActive_ = true;
    const size_t available = payloadLength - headerLength;
    const size_t audioLength =
        pesUnbounded_ || available < pesRemaining_ ? available : pesRemaining_;
    if (!pesUnbounded_) {
      pesRemaining_ -= audioLength;
      if (pesRemaining_ == 0) pesActive_ = false;
    }
    return audioLength
        ? result(TsParseStatus::AudioData, TsParseError::None,
                 view.payloadOffset + headerLength, audioLength)
        : result(TsParseStatus::NeedMoreData);
  }

  if (!pesActive_) return result(TsParseStatus::NeedMoreData);
  const size_t audioLength =
      pesUnbounded_ || payloadLength < pesRemaining_ ? payloadLength
                                                      : pesRemaining_;
  if (!pesUnbounded_) {
    pesRemaining_ -= audioLength;
    if (pesRemaining_ == 0) pesActive_ = false;
  }
  return audioLength
      ? result(TsParseStatus::AudioData, TsParseError::None,
               view.payloadOffset, audioLength)
      : result(TsParseStatus::NeedMoreData);
}

}  // namespace audio
}  // namespace voxone
