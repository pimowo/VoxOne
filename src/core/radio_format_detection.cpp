#include "radio_format_detection.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

namespace voxone {
namespace radio {
namespace {

struct MimeView {
  const char* begin;
  size_t length;
};

MimeView normalizedMime(const char* contentType) {
  if (!contentType) return {nullptr, 0};
  const char* begin = contentType;
  while (*begin && isspace(static_cast<unsigned char>(*begin))) ++begin;
  const char* end = begin;
  while (*end && *end != ';') ++end;
  while (end > begin && isspace(static_cast<unsigned char>(end[-1]))) --end;
  return {begin, static_cast<size_t>(end - begin)};
}

bool equals(const MimeView& mime, const char* expected) {
  const size_t length = strlen(expected);
  if (mime.length != length) return false;
  for (size_t index = 0; index < length; ++index) {
    if (tolower(static_cast<unsigned char>(mime.begin[index])) != expected[index])
      return false;
  }
  return true;
}

AudioFormatDetection contentType(AudioFormat format, bool aacPlus = false) {
  return {format, AudioFormatSource::ContentType, aacPlus};
}

}  // namespace

AudioFormatDetection detectAudioFormat(const char* value, AudioFormat hint) {
  const MimeView mime = normalizedMime(value);
  if (mime.length == 0)
    return hint == AudioFormat::Unknown
        ? AudioFormatDetection{AudioFormat::Unknown, AudioFormatSource::None,
                               false}
        : AudioFormatDetection{hint, AudioFormatSource::Hint, false};

  if (equals(mime, "audio/mpeg") || equals(mime, "audio/mp3") ||
      equals(mime, "audio/mpeg3") || equals(mime, "audio/x-mpeg") ||
      equals(mime, "audio/x-mpeg-3"))
    return contentType(AudioFormat::Mp3);

  if (equals(mime, "audio/aac") || equals(mime, "audio/x-aac"))
    return contentType(AudioFormat::Aac);
  if (equals(mime, "audio/aacp"))
    return contentType(AudioFormat::Aac, true);

  // M4A is an AAC container. It keeps a distinct container result so the
  // existing M4A header parser can prepare raw AAC blocks for the AAC decoder.
  if (equals(mime, "audio/mp4") || equals(mime, "audio/m4a") ||
      equals(mime, "audio/x-m4a"))
    return contentType(AudioFormat::M4aAac);

  if (equals(mime, "audio/flac") || equals(mime, "audio/x-flac"))
    return contentType(AudioFormat::Flac);

  const bool generic = equals(mime, "application/octet-stream") ||
                       equals(mime, "text/plain");
  if (generic && hint != AudioFormat::Unknown)
    return {hint, AudioFormatSource::Hint, false};

  return {AudioFormat::Unknown, AudioFormatSource::None, false};
}

}  // namespace radio
}  // namespace voxone
