#ifndef VOXONE_PLAYLIST_VALIDATION_H
#define VOXONE_PLAYLIST_VALIDATION_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

inline bool playlistValidUtf8(const char* data, size_t size) {
  for (size_t i = 0; i < size;) {
    const uint8_t first = static_cast<uint8_t>(data[i]);
    if (first < 0x80) { ++i; continue; }
    size_t count;
    uint32_t code;
    if (first >= 0xC2 && first <= 0xDF) { count = 2; code = first & 0x1F; }
    else if (first >= 0xE0 && first <= 0xEF) { count = 3; code = first & 0x0F; }
    else if (first >= 0xF0 && first <= 0xF4) { count = 4; code = first & 0x07; }
    else return false;
    if (i + count > size) return false;
    for (size_t j = 1; j < count; ++j) {
      const uint8_t next = static_cast<uint8_t>(data[i + j]);
      if ((next & 0xC0) != 0x80) return false;
      code = (code << 6) | (next & 0x3F);
    }
    if ((count == 3 && (code < 0x800 || (code >= 0xD800 && code <= 0xDFFF))) ||
        (count == 4 && (code < 0x10000 || code > 0x10FFFF))) return false;
    i += count;
  }
  return true;
}

inline bool playlistValidField(const char* data, size_t size, bool requireUtf8) {
  if (!data || size == 0 || size > 169 || strlen(data) != size) return false;
  for (size_t i = 0; i < size; ++i)
    if (data[i] == '\t' || data[i] == '\r' || data[i] == '\n') return false;
  return !requireUtf8 || playlistValidUtf8(data, size);
}

inline bool playlistParseInteger(const char* data, size_t size, int& value) {
  if (!data || size == 0 || size > 4 || strlen(data) != size) return false;
  size_t i = 0;
  int sign = 1;
  if (data[0] == '-' || data[0] == '+') {
    sign = data[0] == '-' ? -1 : 1;
    i = 1;
  }
  if (i == size) return false;
  int parsed = 0;
  for (; i < size; ++i) {
    if (data[i] < '0' || data[i] > '9') return false;
    parsed = parsed * 10 + data[i] - '0';
    if (parsed > 999) return false;
  }
  value = sign * parsed;
  return true;
}

inline bool playlistValidOvol(int value) { return value >= -30 && value <= 30; }

inline uint32_t playlistCrcByte(uint32_t crc, uint8_t byte) {
  crc ^= byte;
  for (uint8_t bit = 0; bit < 8; ++bit)
    crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
  return crc;
}

#endif
