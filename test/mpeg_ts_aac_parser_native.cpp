#include "../src/audioI2S/mpeg_ts_aac_parser.h"
#include "../src/core/radio_reconnect_policy.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using voxone::audio::MpegTsAacParser;
using voxone::audio::TsParseError;
using voxone::audio::TsParseResult;
using voxone::audio::TsParseStatus;
using voxone::audio::TsRecoveryAbortReason;
using voxone::audio::TsRecoveryAction;
using voxone::audio::TsStreamRecoveryPolicy;
using voxone::radio::ReconnectPolicy;

namespace {

using Packet = std::array<uint8_t, MpegTsAacParser::kPacketSize>;

void append16(std::vector<uint8_t>& bytes, uint16_t value) {
  bytes.push_back(static_cast<uint8_t>(value >> 8));
  bytes.push_back(static_cast<uint8_t>(value));
}

std::vector<uint8_t> patSection(const std::vector<uint16_t>& pmtPids,
                                bool includeNetwork = false) {
  const size_t entries = pmtPids.size() + (includeNetwork ? 1 : 0);
  const size_t bodyLength = 9 + entries * 4;
  std::vector<uint8_t> section;
  section.push_back(0x00);
  section.push_back(static_cast<uint8_t>(0xB0 | (bodyLength >> 8)));
  section.push_back(static_cast<uint8_t>(bodyLength));
  append16(section, 1);
  section.push_back(0xC1);
  section.push_back(0);
  section.push_back(0);
  if (includeNetwork) {
    append16(section, 0);
    append16(section, 0xE010);
  }
  for (size_t index = 0; index < pmtPids.size(); ++index) {
    append16(section, static_cast<uint16_t>(index + 1));
    append16(section, static_cast<uint16_t>(0xE000 | pmtPids[index]));
  }
  section.insert(section.end(), 4, 0);  // CRC bytes are not table entries.
  assert(section.size() == bodyLength + 3);
  return section;
}

std::vector<uint8_t> pmtSection(uint8_t streamType, uint16_t audioPid,
                                size_t precedingStreams = 0) {
  const size_t streamCount = precedingStreams + 1;
  const size_t bodyLength = 13 + streamCount * 5;
  std::vector<uint8_t> section;
  section.push_back(0x02);
  section.push_back(static_cast<uint8_t>(0xB0 | (bodyLength >> 8)));
  section.push_back(static_cast<uint8_t>(bodyLength));
  append16(section, 1);
  section.push_back(0xC1);
  section.push_back(0);
  section.push_back(0);
  append16(section, static_cast<uint16_t>(0xE000 | audioPid));
  append16(section, 0xF000);
  for (size_t index = 0; index < precedingStreams; ++index) {
    section.push_back(0x03);
    append16(section, static_cast<uint16_t>(0xE100 | index));
    append16(section, 0xF000);
  }
  section.push_back(streamType);
  append16(section, static_cast<uint16_t>(0xE000 | audioPid));
  append16(section, 0xF000);
  section.insert(section.end(), 4, 0);
  assert(section.size() == bodyLength + 3);
  return section;
}

Packet packetWithPayload(uint16_t pid, bool payloadUnitStart,
                         uint8_t continuity, const uint8_t* payload,
                         size_t payloadLength, int adaptationLength = -1) {
  Packet packet{};
  packet.fill(0xFF);
  packet[0] = 0x47;
  packet[1] = static_cast<uint8_t>((payloadUnitStart ? 0x40 : 0) |
                                   ((pid >> 8) & 0x1F));
  packet[2] = static_cast<uint8_t>(pid);
  size_t offset = 4;
  if (adaptationLength >= 0) {
    packet[3] = static_cast<uint8_t>(0x30 | (continuity & 0x0F));
    packet[4] = static_cast<uint8_t>(adaptationLength);
    if (adaptationLength > 0) packet[5] = 0;
    offset = 5 + static_cast<size_t>(adaptationLength);
  } else {
    packet[3] = static_cast<uint8_t>(0x10 | (continuity & 0x0F));
  }
  assert(offset <= packet.size());
  assert(payloadLength <= packet.size() - offset);
  if (payloadLength) memcpy(packet.data() + offset, payload, payloadLength);
  return packet;
}

Packet psiPacket(uint16_t pid, const std::vector<uint8_t>& section,
                 int adaptationLength = -1, uint8_t continuity = 0) {
  std::vector<uint8_t> payload(1, 0);
  payload.insert(payload.end(), section.begin(), section.end());
  return packetWithPayload(pid, true, continuity, payload.data(),
                           payload.size(), adaptationLength);
}

Packet nullPacket(uint8_t continuity = 0, bool payloadUnitStart = false) {
  const uint8_t stuffing = 0xFF;
  return packetWithPayload(MpegTsAacParser::kNullPid, payloadUnitStart,
                           continuity, &stuffing, 1, 182);
}

Packet adaptationOnlyPacket(uint16_t pid, uint8_t continuity = 0) {
  Packet packet{};
  packet.fill(0xFF);
  packet[0] = 0x47;
  packet[1] = static_cast<uint8_t>((pid >> 8) & 0x1F);
  packet[2] = static_cast<uint8_t>(pid);
  packet[3] = static_cast<uint8_t>(0x20 | (continuity & 0x0F));
  packet[4] = 183;
  packet[5] = 0;
  return packet;
}

void expectStatus(const TsParseResult& parsed, TsParseStatus expected) {
  assert(parsed.status == expected);
  if (expected == TsParseStatus::AudioData) {
    assert(parsed.audioLength > 0);
    assert(static_cast<size_t>(parsed.audioOffset) + parsed.audioLength <=
           MpegTsAacParser::kPacketSize);
  }
}

void prime(MpegTsAacParser& parser, int patAdaptation = -1,
           int pmtAdaptation = -1, uint16_t pmtPid = 0x20,
           uint16_t audioPid = 0x50) {
  expectStatus(parser.parse(psiPacket(0, patSection({pmtPid}),
                                      patAdaptation).data(),
                            MpegTsAacParser::kPacketSize),
               TsParseStatus::Ignored);
  expectStatus(parser.parse(psiPacket(pmtPid, pmtSection(0x0F, audioPid),
                                      pmtAdaptation).data(),
                            MpegTsAacParser::kPacketSize),
               TsParseStatus::Ignored);
  assert(parser.audioPid() == audioPid);
}

std::vector<Packet> pesPackets(uint16_t pid,
                               const std::vector<uint8_t>& audio,
                               bool unbounded = false,
                               uint8_t firstContinuity = 0) {
  std::vector<uint8_t> pes = {0x00, 0x00, 0x01, 0xC0, 0x00, 0x00,
                              0x80, 0x00, 0x00};
  const size_t declared = 3 + audio.size();
  assert(unbounded || declared <= 0xFFFF);
  if (!unbounded) {
    pes[4] = static_cast<uint8_t>(declared >> 8);
    pes[5] = static_cast<uint8_t>(declared);
  }
  pes.insert(pes.end(), audio.begin(), audio.end());

  std::vector<Packet> packets;
  size_t offset = 0;
  uint8_t continuity = firstContinuity;
  bool first = true;
  while (offset < pes.size()) {
    const size_t remaining = pes.size() - offset;
    const size_t copied = remaining < 184 ? remaining : 184;
    const int adaptation = copied < 184 ? static_cast<int>(183 - copied) : -1;
    packets.push_back(packetWithPayload(pid, first, continuity,
                                        pes.data() + offset, copied,
                                        adaptation));
    offset += copied;
    continuity = static_cast<uint8_t>((continuity + 1) & 0x0F);
    first = false;
  }
  return packets;
}

std::vector<uint8_t> extract(MpegTsAacParser& parser,
                             const std::vector<Packet>& packets) {
  std::vector<uint8_t> output;
  for (const Packet& packet : packets) {
    const TsParseResult parsed = parser.parse(packet.data(), packet.size());
    if (parsed.status == TsParseStatus::AudioData) {
      output.insert(output.end(), packet.begin() + parsed.audioOffset,
                    packet.begin() + parsed.audioOffset + parsed.audioLength);
    } else {
      assert(parsed.status == TsParseStatus::NeedMoreData ||
             parsed.status == TsParseStatus::Ignored);
    }
  }
  return output;
}

void testPatPmtWithoutAdaptation() {
  MpegTsAacParser parser;
  prime(parser);
  assert(parser.pmtPidCount() == 1);
  assert(parser.pmtPid(0) == 0x20);
  assert(parser.audioPid() == 0x50);
}

void testPatPmtWithEvidenceAdaptation() {
  MpegTsAacParser parser;
  prime(parser, 166, 161);
  assert(parser.pmtPid(0) == 0x20);
  assert(parser.audioPid() == 0x50);
}

void testSplitPsiSections() {
  MpegTsAacParser parser;
  const std::vector<uint8_t> pat = patSection({0x20});
  const size_t firstPatBytes = 9;
  std::vector<uint8_t> firstPayload(1, 0);
  firstPayload.insert(firstPayload.end(), pat.begin(), pat.begin() + firstPatBytes);
  Packet first = packetWithPayload(0, true, 0, firstPayload.data(),
                                   firstPayload.size(),
                                   static_cast<int>(183 - firstPayload.size()));
  expectStatus(parser.parse(first.data(), first.size()),
               TsParseStatus::NeedMoreData);
  Packet second = packetWithPayload(0, false, 1, pat.data() + firstPatBytes,
                                    pat.size() - firstPatBytes);
  expectStatus(parser.parse(second.data(), second.size()),
               TsParseStatus::Ignored);
  assert(parser.pmtPid(0) == 0x20);

  const std::vector<uint8_t> pmt = pmtSection(0x0F, 0x50);
  const size_t firstPmtBytes = 11;
  std::vector<uint8_t> pmtPayload(1, 0);
  pmtPayload.insert(pmtPayload.end(), pmt.begin(), pmt.begin() + firstPmtBytes);
  Packet pmtFirst = packetWithPayload(0x20, true, 0, pmtPayload.data(),
                                      pmtPayload.size(),
                                      static_cast<int>(183 - pmtPayload.size()));
  expectStatus(parser.parse(pmtFirst.data(), pmtFirst.size()),
               TsParseStatus::NeedMoreData);
  Packet pmtSecond = packetWithPayload(0x20, false, 1,
                                       pmt.data() + firstPmtBytes,
                                       pmt.size() - firstPmtBytes);
  expectStatus(parser.parse(pmtSecond.data(), pmtSecond.size()),
               TsParseStatus::Ignored);
  assert(parser.audioPid() == 0x50);
}

void testPmtPidCapacityAndNetworkEntry() {
  MpegTsAacParser parser;
  const Packet pat = psiPacket(
      0, patSection({0x20, 0x21, 0x22, 0x23, 0x24, 0x25}, true));
  expectStatus(parser.parse(pat.data(), pat.size()), TsParseStatus::Ignored);
  assert(parser.pmtPidCount() == MpegTsAacParser::kMaxPmtPids);
  assert(parser.pmtPidOverflow());
  assert(parser.pmtPid(0) == 0x20);
  assert(parser.pmtPid(3) == 0x23);
  assert(parser.pmtPid(4) == MpegTsAacParser::kInvalidPid);

  // A long PMT stream list has no fixed stream-array write and still finds
  // the bounded AAC ADTS entry at the end.
  const Packet pmt = psiPacket(0x20, pmtSection(0x0F, 0x50, 20));
  expectStatus(parser.parse(pmt.data(), pmt.size()), TsParseStatus::Ignored);
  assert(parser.audioPid() == 0x50);
}

void testMalformedPacketBounds() {
  MpegTsAacParser parser;
  Packet packet{};
  packet.fill(0xFF);
  packet[0] = 0x47;
  packet[1] = 0x40;
  packet[2] = 0;
  packet[3] = 0x30;
  packet[4] = 184;
  TsParseResult parsed = parser.parse(packet.data(), packet.size());
  expectStatus(parsed, TsParseStatus::Malformed);
  assert(parsed.error == TsParseError::AdaptationField);

  packet[4] = 183;  // AFC says payload exists, but adaptation consumes it.
  expectStatus(parser.parse(packet.data(), packet.size()),
               TsParseStatus::Malformed);

  const Packet valid = psiPacket(0, patSection({0x20}));
  expectStatus(parser.parse(valid.data(), valid.size() - 1),
               TsParseStatus::Malformed);
  expectStatus(parser.parse(nullptr, 0), TsParseStatus::Malformed);

  Packet badSync = valid;
  badSync[0] = 0;
  expectStatus(parser.parse(badSync.data(), badSync.size()),
               TsParseStatus::Malformed);
}

void testMalformedPointerAndSectionLength() {
  MpegTsAacParser parser;
  uint8_t payload[17] = {};
  payload[0] = 17;
  Packet pointer = packetWithPayload(0, true, 0, payload, sizeof(payload), 166);
  TsParseResult parsed = parser.parse(pointer.data(), pointer.size());
  expectStatus(parsed, TsParseStatus::Malformed);
  assert(parsed.error == TsParseError::PointerField);

  payload[0] = 0;
  payload[1] = 0;
  payload[2] = 0xBF;
  payload[3] = 0xFF;
  Packet section = packetWithPayload(0, true, 0, payload, sizeof(payload), 166);
  parsed = parser.parse(section.data(), section.size());
  expectStatus(parsed, TsParseStatus::Malformed);
  assert(parsed.error == TsParseError::PsiSectionLength);
}

void testMalformedPmtDescriptors() {
  MpegTsAacParser parser;
  const Packet pat = psiPacket(0, patSection({0x20}));
  expectStatus(parser.parse(pat.data(), pat.size()), TsParseStatus::Ignored);
  std::vector<uint8_t> pmt = pmtSection(0x0F, 0x50);
  pmt[10] = 0xFF;
  pmt[11] = 0xFF;
  const Packet corrupt = psiPacket(0x20, pmt);
  const TsParseResult parsed = parser.parse(corrupt.data(), corrupt.size());
  expectStatus(parsed, TsParseStatus::Malformed);
  assert(parsed.error == TsParseError::PmtSection);
}

void testPesExtractionAndBoundaries() {
  MpegTsAacParser parser;
  prime(parser, 166, 161);
  std::vector<uint8_t> audio(700);
  for (size_t index = 0; index < audio.size(); ++index)
    audio[index] = static_cast<uint8_t>((index * 29) & 0xFF);
  const std::vector<uint8_t> output = extract(parser, pesPackets(0x50, audio));
  assert(output == audio);

  // A repeated PAT/PMT starts the next HLS segment with clean PSI/PES state.
  parser.beginSegment();
  prime(parser, 166, 161);
  const std::vector<uint8_t> next = {0xFF, 0xF1, 0x50, 0x80, 0x01,
                                     0x7F, 0xFC, 0x11, 0x22, 0x33};
  assert(extract(parser, pesPackets(0x50, next, false, 8)) == next);
}

void testUnboundedPesAndContinuity() {
  MpegTsAacParser parser;
  prime(parser);
  std::vector<uint8_t> audio(400, 0x5A);
  assert(extract(parser, pesPackets(0x50, audio, true)) == audio);

  parser.beginSegment();
  prime(parser);
  std::vector<Packet> packets = pesPackets(0x50, audio);
  assert(packets.size() > 1);
  TsParseResult first = parser.parse(packets[0].data(), packets[0].size());
  expectStatus(first, TsParseStatus::AudioData);
  packets[1][3] = static_cast<uint8_t>((packets[1][3] & 0xF0) | 7);
  const TsParseResult gap = parser.parse(packets[1].data(), packets[1].size());
  expectStatus(gap, TsParseStatus::Discontinuity);
  assert(gap.error == TsParseError::PesContinuity);

  // A new PUSI after the gap recovers without reusing partial PES bytes.
  const std::vector<uint8_t> recovered = {1, 2, 3, 4, 5};
  assert(extract(parser, pesPackets(0x50, recovered, false, 8)) == recovered);
}

void testLatmIsNotAdts() {
  MpegTsAacParser parser;
  const Packet pat = psiPacket(0, patSection({0x20}));
  expectStatus(parser.parse(pat.data(), pat.size()), TsParseStatus::Ignored);
  const Packet pmt = psiPacket(0x20, pmtSection(0x11, 0x50));
  expectStatus(parser.parse(pmt.data(), pmt.size()),
               TsParseStatus::UnsupportedAudio);
  assert(parser.latmDetected());
  assert(parser.audioPid() == MpegTsAacParser::kInvalidPid);
}

void testNullPidIsNeverAudio() {
  MpegTsAacParser parser;
  const uint8_t validLookingPes[] = {
      0x00, 0x00, 0x01, 0xC0, 0x00, 0x06, 0x80, 0x00, 0x00, 1, 2, 3};
  const Packet pusi = packetWithPayload(MpegTsAacParser::kNullPid, true, 0,
                                        validLookingPes,
                                        sizeof(validLookingPes));
  const TsParseResult beforePmt = parser.parse(pusi.data(), pusi.size());
  expectStatus(beforePmt, TsParseStatus::Ignored);
  assert(beforePmt.audioLength == 0);
  assert(parser.audioPid() == MpegTsAacParser::kInvalidPid);

  const uint8_t arbitrary[] = {0x00, 0x00, 0x01, 0xC0, 0xFF};
  const Packet noPusi = packetWithPayload(MpegTsAacParser::kNullPid, false, 1,
                                          arbitrary, sizeof(arbitrary));
  expectStatus(parser.parse(noPusi.data(), noPusi.size()),
               TsParseStatus::Ignored);

  const uint8_t badPes[] = {0x00, 0x01, 0x02};
  const Packet malformedPes = packetWithPayload(
      MpegTsAacParser::kNullPid, true, 2, badPes, sizeof(badPes));
  expectStatus(parser.parse(malformedPes.data(), malformedPes.size()),
               TsParseStatus::Ignored);

  Packet malformedHeader = pusi;
  malformedHeader[3] = 0x30;
  malformedHeader[4] = 184;
  expectStatus(parser.parse(malformedHeader.data(), malformedHeader.size()),
               TsParseStatus::Malformed);

  prime(parser);
  expectStatus(parser.parse(pusi.data(), pusi.size()), TsParseStatus::Ignored);
  assert(parser.audioPid() == 0x50);

  MpegTsAacParser nullPmtParser;
  const Packet pat = psiPacket(0, patSection({0x20}));
  expectStatus(nullPmtParser.parse(pat.data(), pat.size()),
               TsParseStatus::Ignored);
  const Packet nullPmt =
      psiPacket(0x20, pmtSection(0x0F, MpegTsAacParser::kNullPid));
  expectStatus(nullPmtParser.parse(nullPmt.data(), nullPmt.size()),
               TsParseStatus::Ignored);
  assert(nullPmtParser.audioPid() == MpegTsAacParser::kInvalidPid);
}

void testProgramMapChangesTransactionally() {
  MpegTsAacParser parser;
  prime(parser);

  const Packet moved = psiPacket(0x20, pmtSection(0x0F, 0x51), -1, 1);
  expectStatus(parser.parse(moved.data(), moved.size()),
               TsParseStatus::Ignored);
  assert(parser.audioPid() == 0x51);
  assert(!parser.latmDetected());
  const std::vector<uint8_t> audio = {0xFF, 0xF1, 1, 2, 3};
  assert(extract(parser, pesPackets(0x50, audio)).empty());
  assert(extract(parser, pesPackets(0x51, audio)) == audio);

  const Packet latm = psiPacket(0x20, pmtSection(0x11, 0x52), -1, 2);
  expectStatus(parser.parse(latm.data(), latm.size()),
               TsParseStatus::UnsupportedAudio);
  assert(parser.audioPid() == MpegTsAacParser::kInvalidPid);
  assert(parser.latmDetected());
  assert(extract(parser, pesPackets(0x51, audio)).empty());

  const Packet adtsAgain = psiPacket(0x20, pmtSection(0x0F, 0x53), -1, 3);
  expectStatus(parser.parse(adtsAgain.data(), adtsAgain.size()),
               TsParseStatus::Ignored);
  assert(parser.audioPid() == 0x53);
  assert(!parser.latmDetected());

  const Packet unsupported = psiPacket(0x20, pmtSection(0x03, 0x54), -1, 4);
  expectStatus(parser.parse(unsupported.data(), unsupported.size()),
               TsParseStatus::Ignored);
  assert(parser.audioPid() == MpegTsAacParser::kInvalidPid);
  assert(!parser.latmDetected());
  assert(extract(parser, pesPackets(0x53, audio)).empty());
}

void testProgramMapAcrossSegmentsAndIncompletePmt() {
  MpegTsAacParser parser;
  prime(parser, 166, 161);
  const std::vector<uint8_t> audio = {1, 2, 3, 4, 5};

  parser.beginSegment();
  assert(parser.audioPid() == 0x50);
  assert(extract(parser, pesPackets(0x50, audio, false, 5)) == audio);

  parser.beginSegment();
  const Packet changedPat = psiPacket(0, patSection({0x21}), -1, 1);
  expectStatus(parser.parse(changedPat.data(), changedPat.size()),
               TsParseStatus::Ignored);
  assert(parser.audioPid() == MpegTsAacParser::kInvalidPid);
  const Packet changedPmt = psiPacket(0x21, pmtSection(0x0F, 0x60));
  expectStatus(parser.parse(changedPmt.data(), changedPmt.size()),
               TsParseStatus::Ignored);
  assert(parser.audioPid() == 0x60);

  const std::vector<uint8_t> incomplete = pmtSection(0x11, 0x61);
  const size_t firstBytes = 10;
  std::vector<uint8_t> payload(1, 0);
  payload.insert(payload.end(), incomplete.begin(),
                 incomplete.begin() + firstBytes);
  const Packet first = packetWithPayload(
      0x21, true, 1, payload.data(), payload.size(),
      static_cast<int>(183 - payload.size()));
  expectStatus(parser.parse(first.data(), first.size()),
               TsParseStatus::NeedMoreData);
  assert(parser.audioPid() == 0x60);
  assert(!parser.latmDetected());
  parser.beginSegment();
  assert(parser.audioPid() == 0x60);
}

void testRecoveryRequiresAudioProgress() {
  MpegTsAacParser parser;
  TsStreamRecoveryPolicy policy;
  prime(parser);
  Packet bad = psiPacket(0, patSection({0x20}));
  bad[0] = 0;

  TsParseResult malformed = parser.parse(bad.data(), bad.size());
  expectStatus(malformed, TsParseStatus::Malformed);
  assert(policy.observe(malformed) == TsRecoveryAction::DropPacket);
  assert(policy.malformedWithoutAudio() == 1);

  const std::vector<uint8_t> audio = {9, 8, 7, 6};
  const std::vector<Packet> audioPackets = pesPackets(0x50, audio);
  const TsParseResult recovered =
      parser.parse(audioPackets[0].data(), audioPackets[0].size());
  expectStatus(recovered, TsParseStatus::AudioData);
  assert(policy.observe(recovered) == TsRecoveryAction::Continue);
  assert(policy.malformedWithoutAudio() == 0);
  assert(policy.packetsWithoutAudio() == 0);

  // Seven malformed packets do not abort, even when NULL packets are
  // interleaved, and NULL never pretends that AAC has recovered.
  policy.reset();
  const Packet null = nullPacket();
  for (uint8_t count = 1; count <
       TsStreamRecoveryPolicy::kMaxMalformedWithoutAudio; ++count) {
    malformed = parser.parse(bad.data(), bad.size());
    assert(policy.observe(malformed) == TsRecoveryAction::DropPacket);
    const TsParseResult ignored = parser.parse(null.data(), null.size());
    expectStatus(ignored, TsParseStatus::Ignored);
    assert(policy.observe(ignored) == TsRecoveryAction::Continue);
    assert(policy.malformedWithoutAudio() == count);
  }
  assert(policy.malformedWithoutAudio() == 7);
  assert(policy.packetsWithoutAudio() == 14);

  // PAT and an incomplete PSI section are valid transport progress, not AAC
  // progress, and therefore preserve the malformed history.
  policy.reset();
  assert(policy.observe(parser.parse(bad.data(), bad.size())) ==
         TsRecoveryAction::DropPacket);
  const Packet pat = psiPacket(0, patSection({0x20}), -1, 3);
  const TsParseResult patResult = parser.parse(pat.data(), pat.size());
  expectStatus(patResult, TsParseStatus::Ignored);
  assert(policy.observe(patResult) == TsRecoveryAction::Continue);
  assert(policy.malformedWithoutAudio() == 1);

  const std::vector<uint8_t> pmt = pmtSection(0x0F, 0x50);
  std::vector<uint8_t> partialPayload(1, 0);
  partialPayload.insert(partialPayload.end(), pmt.begin(), pmt.begin() + 8);
  const Packet partial = packetWithPayload(
      0x20, true, 4, partialPayload.data(), partialPayload.size(),
      static_cast<int>(183 - partialPayload.size()));
  const TsParseResult partialResult = parser.parse(partial.data(), partial.size());
  expectStatus(partialResult, TsParseStatus::NeedMoreData);
  assert(policy.observe(partialResult) == TsRecoveryAction::Continue);
  assert(policy.malformedWithoutAudio() == 1);
  assert(policy.observe(parser.parse(bad.data(), bad.size())) ==
         TsRecoveryAction::DropPacket);
  assert(policy.malformedWithoutAudio() == 2);

  // A PES discontinuity also does not erase a prior parser failure.
  policy.reset();
  assert(policy.observe(parser.parse(bad.data(), bad.size())) ==
         TsRecoveryAction::DropPacket);
  const uint8_t byte = 0;
  Packet discontinuity =
      packetWithPayload(0x50, false, 7, &byte, 1, 182);
  discontinuity[5] = 0x80;
  const TsParseResult gap =
      parser.parse(discontinuity.data(), discontinuity.size());
  expectStatus(gap, TsParseStatus::Discontinuity);
  assert(policy.observe(gap) == TsRecoveryAction::Continue);
  assert(policy.malformedWithoutAudio() == 1);
  assert(policy.observe(parser.parse(bad.data(), bad.size())) ==
         TsRecoveryAction::DropPacket);
  assert(policy.malformedWithoutAudio() == 2);
}

void testRecoveryBudgetsAndSegmentBoundary() {
  MpegTsAacParser parser;
  TsStreamRecoveryPolicy policy;
  prime(parser);
  const Packet null = nullPacket();
  const Packet adaptationOnly = adaptationOnlyPacket(0x50);
  const Packet pat = psiPacket(0, patSection({0x20}));
  const Packet pmt = psiPacket(0x20, pmtSection(0x0F, 0x50));

  // A stream containing only NULL, PAT, and PMT packets reaches the bounded
  // no-AAC window without any of those packets being called malformed.
  for (uint16_t count = 0;
       count < TsStreamRecoveryPolicy::kMaxPacketsWithoutAudio; ++count) {
    const Packet* packet = &null;
    if (count % 3 == 1) packet = &pat;
    else if (count % 3 == 2) packet = &pmt;
    const TsParseResult parsed = parser.parse(packet->data(), packet->size());
    assert(parsed.status == TsParseStatus::Ignored ||
           parsed.status == TsParseStatus::NeedMoreData);
    const TsRecoveryAction action = policy.observe(parsed);
    if (count + 1 < TsStreamRecoveryPolicy::kMaxPacketsWithoutAudio)
      assert(action == TsRecoveryAction::Continue);
    else
      assert(action == TsRecoveryAction::AbortStream);
  }
  assert(policy.abortReason() == TsRecoveryAbortReason::NoAudioProgress);
  assert(policy.packetsWithoutAudio() ==
         TsStreamRecoveryPolicy::kMaxPacketsWithoutAudio);

  // A long but bounded valid start is accepted, and actual AAC extraction
  // resets both counters. Extracted AAC is not asserted to be decoded PCM.
  policy.reset();
  for (uint16_t count = 0;
       count + 1 < TsStreamRecoveryPolicy::kMaxPacketsWithoutAudio; ++count) {
    const Packet& packet = count % 2 == 0 ? null : adaptationOnly;
    const TsParseResult ignored = parser.parse(packet.data(), packet.size());
    assert(policy.observe(ignored) == TsRecoveryAction::Continue);
  }
  const std::vector<uint8_t> audio = {1, 2, 3, 4, 5};
  const std::vector<Packet> audioPackets = pesPackets(0x50, audio, false, 8);
  const TsParseResult progress =
      parser.parse(audioPackets[0].data(), audioPackets[0].size());
  expectStatus(progress, TsParseStatus::AudioData);
  assert(policy.observe(progress) == TsRecoveryAction::Continue);
  assert(policy.packetsWithoutAudio() == 0);
  assert(policy.malformedWithoutAudio() == 0);

  // Segment boundaries reset partial PSI/PES only. They cannot hide a
  // persistent lack of demultiplexed AAC.
  policy.reset();
  const uint16_t half =
      TsStreamRecoveryPolicy::kMaxPacketsWithoutAudio / 2;
  for (uint16_t count = 0; count < half; ++count)
    assert(policy.observe(parser.parse(null.data(), null.size())) ==
           TsRecoveryAction::Continue);
  parser.beginSegment();
  for (uint16_t count = half;
       count < TsStreamRecoveryPolicy::kMaxPacketsWithoutAudio; ++count) {
    const TsRecoveryAction action =
        policy.observe(parser.parse(null.data(), null.size()));
    if (count + 1 < TsStreamRecoveryPolicy::kMaxPacketsWithoutAudio)
      assert(action == TsRecoveryAction::Continue);
    else
      assert(action == TsRecoveryAction::AbortStream);
  }
  assert(policy.abortReason() == TsRecoveryAbortReason::NoAudioProgress);
}

void testMalformedBudgetAndReconnectStop() {
  MpegTsAacParser parser;
  TsStreamRecoveryPolicy policy;
  Packet bad = psiPacket(0, patSection({0x20}));
  bad[0] = 0;

  TsRecoveryAction action = TsRecoveryAction::Continue;
  for (uint8_t count = 1;
       count <= TsStreamRecoveryPolicy::kMaxMalformedWithoutAudio; ++count) {
    const TsParseResult malformed = parser.parse(bad.data(), bad.size());
    action = policy.observe(malformed);
    if (count < TsStreamRecoveryPolicy::kMaxMalformedWithoutAudio)
      assert(action == TsRecoveryAction::DropPacket);
    else
      assert(action == TsRecoveryAction::AbortStream);
  }
  assert(policy.malformedWithoutAudio() ==
         TsStreamRecoveryPolicy::kMaxMalformedWithoutAudio);
  assert(policy.abortReason() == TsRecoveryAbortReason::MalformedBudget);

  // The production reconnect policy coalesces repeated scheduling, and STOP
  // cancels the one pending attempt so a recovery abort cannot create a loop.
  ReconnectPolicy reconnect;
  assert(action == TsRecoveryAction::AbortStream);
  assert(reconnect.schedule(1000));
  for (int duplicate = 0; duplicate < 20; ++duplicate)
    assert(!reconnect.schedule(1000 + duplicate));
  assert(reconnect.pending() && reconnect.failures() == 1);
  assert(!reconnect.takeDue(2000, false, true));
  assert(!reconnect.pending() && reconnect.failures() == 0);

  MpegTsAacParser latmParser;
  const Packet latmPat = psiPacket(0, patSection({0x20}));
  policy.reset();
  policy.observe(latmParser.parse(latmPat.data(), latmPat.size()));
  const Packet latmPmt = psiPacket(0x20, pmtSection(0x11, 0x50));
  assert(policy.observe(latmParser.parse(latmPmt.data(), latmPmt.size())) ==
         TsRecoveryAction::AbortStream);
  assert(policy.abortReason() == TsRecoveryAbortReason::UnsupportedAudio);
}

void testIgnoredPidAndMalformedPes() {
  MpegTsAacParser parser;
  prime(parser);
  const uint8_t unrelated[] = {1, 2, 3};
  const Packet other = packetWithPayload(0x123, true, 0, unrelated,
                                         sizeof(unrelated));
  expectStatus(parser.parse(other.data(), other.size()),
               TsParseStatus::Ignored);

  uint8_t shortPes[] = {0, 0, 1, 0xC0, 0, 4, 0x80, 0, 20};
  const Packet malformed = packetWithPayload(
      0x50, true, 0, shortPes, sizeof(shortPes),
      static_cast<int>(183 - sizeof(shortPes)));
  const TsParseResult parsed = parser.parse(malformed.data(), malformed.size());
  expectStatus(parsed, TsParseStatus::Malformed);
  assert(parsed.error == TsParseError::PesHeader);
}

void verifyExternalTs(const char* path, MpegTsAacParser& parser,
                      TsStreamRecoveryPolicy& policy) {
  std::ifstream stream(path, std::ios::binary);
  assert(stream.good());
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                   std::istreambuf_iterator<char>());
  assert(!bytes.empty() && bytes.size() % MpegTsAacParser::kPacketSize == 0);
  parser.beginSegment();
  std::vector<uint8_t> audio;
  for (size_t offset = 0; offset < bytes.size();
       offset += MpegTsAacParser::kPacketSize) {
    const uint8_t* packet = bytes.data() + offset;
    const TsParseResult parsed =
        parser.parse(packet, MpegTsAacParser::kPacketSize);
    assert(parsed.status != TsParseStatus::Malformed);
    assert(parsed.status != TsParseStatus::UnsupportedAudio);
    assert(policy.observe(parsed) != TsRecoveryAction::AbortStream);
    if (parsed.status == TsParseStatus::AudioData)
      audio.insert(audio.end(), packet + parsed.audioOffset,
                   packet + parsed.audioOffset + parsed.audioLength);
  }
  assert(parser.audioPid() != MpegTsAacParser::kInvalidPid);
  size_t cursor = 0;
  size_t frames = 0;
  while (cursor + 7 <= audio.size()) {
    assert(audio[cursor] == 0xFF && (audio[cursor + 1] & 0xF6) == 0xF0);
    const size_t frameLength =
        static_cast<size_t>(((audio[cursor + 3] & 0x03) << 11) |
                            (audio[cursor + 4] << 3) |
                            (audio[cursor + 5] >> 5));
    assert(frameLength >= 7 && frameLength <= audio.size() - cursor);
    cursor += frameLength;
    ++frames;
  }
  assert(cursor == audio.size());
  assert(frames > 0);
}

}  // namespace

int main(int argc, char** argv) {
  testPatPmtWithoutAdaptation();
  testPatPmtWithEvidenceAdaptation();
  testSplitPsiSections();
  testPmtPidCapacityAndNetworkEntry();
  testMalformedPacketBounds();
  testMalformedPointerAndSectionLength();
  testMalformedPmtDescriptors();
  testPesExtractionAndBoundaries();
  testUnboundedPesAndContinuity();
  testLatmIsNotAdts();
  testNullPidIsNeverAudio();
  testProgramMapChangesTransactionally();
  testProgramMapAcrossSegmentsAndIncompletePmt();
  testRecoveryRequiresAudioProgress();
  testRecoveryBudgetsAndSegmentBoundary();
  testMalformedBudgetAndReconnectStop();
  testIgnoredPidAndMalformedPes();
  MpegTsAacParser externalParser;
  TsStreamRecoveryPolicy externalPolicy;
  for (int index = 1; index < argc; ++index)
    verifyExternalTs(argv[index], externalParser, externalPolicy);
}
