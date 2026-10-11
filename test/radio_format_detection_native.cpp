#include "../src/core/radio_format_detection.h"

#include <cassert>

using voxone::radio::AudioFormat;
using voxone::radio::AudioFormatSource;
using voxone::radio::detectAudioFormat;

namespace {

void expectMime(const char* mime, AudioFormat expected) {
  const auto result = detectAudioFormat(mime, AudioFormat::Mp3);
  assert(result.format == expected);
  assert(result.source == AudioFormatSource::ContentType);
}

}  // namespace

int main() {
  expectMime("audio/mpeg", AudioFormat::Mp3);
  expectMime("audio/mp3", AudioFormat::Mp3);
  expectMime("audio/aac", AudioFormat::Aac);
  expectMime("audio/aacp", AudioFormat::Aac);
  expectMime("audio/x-aac", AudioFormat::Aac);
  expectMime("audio/flac", AudioFormat::Flac);
  expectMime("audio/x-flac", AudioFormat::Flac);

  expectMime("AUDIO/AACP", AudioFormat::Aac);
  expectMime("Audio/Aacp", AudioFormat::Aac);
  expectMime("audio/aacp; charset=utf-8", AudioFormat::Aac);
  expectMime("  audio/aacp  ; charset=utf-8  ", AudioFormat::Aac);
  assert(detectAudioFormat("audio/aacp").aacPlus);

  expectMime("audio/mp4", AudioFormat::M4aAac);
  expectMime("audio/m4a", AudioFormat::M4aAac);
  expectMime("audio/x-m4a", AudioFormat::M4aAac);

  const auto unknown = detectAudioFormat("application/x-unknown");
  assert(unknown.format == AudioFormat::Unknown);
  assert(unknown.source == AudioFormatSource::None);
  assert(unknown.format != AudioFormat::Mp3);
  assert(unknown.format != AudioFormat::Aac);

  // An explicit unsupported MIME is authoritative and cannot be overridden
  // by a URL that merely happens to end in .mp3.
  assert(detectAudioFormat("application/json", AudioFormat::Mp3).format ==
         AudioFormat::Unknown);
  assert(detectAudioFormat("text/html", AudioFormat::Aac).format ==
         AudioFormat::Unknown);

  const auto missingHint = detectAudioFormat(nullptr, AudioFormat::Aac);
  assert(missingHint.format == AudioFormat::Aac);
  assert(missingHint.source == AudioFormatSource::Hint);
  const auto genericHint =
      detectAudioFormat(" application/octet-stream ", AudioFormat::Flac);
  assert(genericHint.format == AudioFormat::Flac);
  assert(genericHint.source == AudioFormatSource::Hint);
  assert(detectAudioFormat("text/plain", AudioFormat::Mp3).format ==
         AudioFormat::Mp3);

  const auto flac = detectAudioFormat("audio/x-flac");
  assert(flac.format == AudioFormat::Flac);
  assert(flac.format != AudioFormat::Mp3);
  assert(flac.format != AudioFormat::Aac);
}
