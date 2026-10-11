#ifndef VOXONE_RADIO_FORMAT_DETECTION_H
#define VOXONE_RADIO_FORMAT_DETECTION_H

#include <stdint.h>

namespace voxone {
namespace radio {

enum class AudioFormat : uint8_t {
  Unknown,
  Mp3,
  Aac,
  M4aAac,
  Flac
};

enum class AudioFormatSource : uint8_t {
  None,
  ContentType,
  Hint
};

struct AudioFormatDetection {
  AudioFormat format;
  AudioFormatSource source;
  bool aacPlus;
};

// A recognized Content-Type always wins. A URL/playlist hint is considered
// only when Content-Type is absent or explicitly generic. Unsupported MIME
// values never fall back to MP3 or another hinted decoder.
AudioFormatDetection detectAudioFormat(
    const char* contentType, AudioFormat hint = AudioFormat::Unknown);

}  // namespace radio
}  // namespace voxone

#endif
