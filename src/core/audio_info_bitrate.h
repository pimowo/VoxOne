#ifndef AUDIO_INFO_BITRATE_H
#define AUDIO_INFO_BITRATE_H

#include <stdint.h>

inline bool parseAudioInfoBitrate(const char* info, uint32_t& bitrate) {
  if (!info) return false;

  static const char marker[] = "BitRate:";
  const char* value = 0;
  for (const char* cursor = info; *cursor; ++cursor) {
    const char* text = cursor;
    const char* expected = marker;
    while (*text && *expected && *text == *expected) {
      ++text;
      ++expected;
    }
    if (!*expected) {
      value = text;
      break;
    }
  }
  if (!value) return false;

  while (*value == ' ' || *value == '\t') ++value;
  if (*value < '0' || *value > '9') return false;

  uint32_t parsed = 0;
  const uint32_t maximum = static_cast<uint32_t>(-1);
  do {
    const uint32_t digit = static_cast<uint32_t>(*value - '0');
    if (parsed > (maximum - digit) / 10U) return false;
    parsed = parsed * 10U + digit;
    ++value;
  } while (*value >= '0' && *value <= '9');

  if (*value && *value != ' ' && *value != '\t' &&
      *value != '\r' && *value != '\n') {
    return false;
  }

  bitrate = parsed;
  return true;
}

#endif
