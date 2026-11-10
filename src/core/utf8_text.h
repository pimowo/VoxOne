#ifndef VOXONE_UTF8_TEXT_H
#define VOXONE_UTF8_TEXT_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace voxone {
namespace utf8 {

inline std::size_t codepointBytes(const char* text, std::size_t remaining) {
  if (!text || !remaining) return 0;
  const uint8_t first = static_cast<uint8_t>(text[0]);
  std::size_t length = 1;
  if (first >= 0xc2 && first <= 0xdf) length = 2;
  else if (first >= 0xe0 && first <= 0xef) length = 3;
  else if (first >= 0xf0 && first <= 0xf4) length = 4;
  else return 1;
  if (length > remaining) return 0;
  for (std::size_t index = 1; index < length; ++index)
    if ((static_cast<uint8_t>(text[index]) & 0xc0) != 0x80) return 1;
  return length;
}

inline std::size_t count(const char* text) {
  if (!text) return 0;
  const std::size_t bytes = std::strlen(text);
  std::size_t offset = 0;
  std::size_t glyphs = 0;
  while (offset < bytes) {
    const std::size_t length = codepointBytes(text + offset, bytes - offset);
    offset += length ? length : 1;
    ++glyphs;
  }
  return glyphs;
}

inline std::size_t completePrefix(const char* text, std::size_t length,
                                  std::size_t byteLimit) {
  if (!text) return 0;
  if (byteLimit > length) byteLimit = length;
  std::size_t offset = 0;
  while (offset < length) {
    const std::size_t bytes = codepointBytes(text + offset, length - offset);
    if (!bytes || offset + bytes > byteLimit) break;
    offset += bytes;
  }
  return offset;
}

inline std::size_t copySpan(char* output, std::size_t capacity,
                            const char* text, std::size_t length) {
  if (!output || !capacity) return 0;
  if (!text) length = 0;
  const std::size_t copied = text
      ? completePrefix(text, length, capacity - 1) : 0;
  if (copied) std::memcpy(output, text, copied);
  output[copied] = '\0';
  return copied;
}

inline std::size_t copy(char* output, std::size_t capacity,
                        const char* text) {
  return copySpan(output, capacity, text, text ? std::strlen(text) : 0);
}

inline std::size_t append(char* output, std::size_t capacity,
                          const char* text) {
  if (!output || !capacity) return 0;
  const std::size_t used = std::strlen(output);
  if (used >= capacity) return used;
  return used + copy(output + used, capacity - used, text);
}

inline void trimIncompleteTail(char* text) {
  if (!text) return;
  const std::size_t length = std::strlen(text);
  std::size_t offset = 0;
  while (offset < length) {
    const uint8_t first = static_cast<uint8_t>(text[offset]);
    std::size_t expected = 1;
    if (first >= 0xc2 && first <= 0xdf) expected = 2;
    else if (first >= 0xe0 && first <= 0xef) expected = 3;
    else if (first >= 0xf0 && first <= 0xf4) expected = 4;
    if (expected > length - offset) {
      text[offset] = '\0';
      return;
    }
    const std::size_t actual = codepointBytes(text + offset, length - offset);
    offset += actual ? actual : 1;
  }
}

inline bool ellipsize(char* output, std::size_t capacity, const char* text,
                      std::size_t maximumGlyphs) {
  if (!output || !capacity) return false;
  if (!text) text = "";
  const std::size_t inputBytes = std::strlen(text);
  if (count(text) <= maximumGlyphs && inputBytes < capacity) {
    copy(output, capacity, text);
    return false;
  }
  output[0] = '\0';
  if (!maximumGlyphs || capacity == 1) return true;
  const std::size_t dots = maximumGlyphs < 3 ? maximumGlyphs : 3;
  const std::size_t keepGlyphs = maximumGlyphs - dots;
  const std::size_t byteBudget = capacity - 1 > dots
      ? capacity - 1 - dots : 0;
  std::size_t offset = 0;
  std::size_t glyphs = 0;
  while (offset < inputBytes && glyphs < keepGlyphs) {
    const std::size_t bytes = codepointBytes(text + offset,
                                              inputBytes - offset);
    if (!bytes || offset + bytes > byteBudget) break;
    offset += bytes;
    ++glyphs;
  }
  if (offset) std::memcpy(output, text, offset);
  std::size_t used = offset;
  for (std::size_t index = 0; index < dots && used + 1 < capacity; ++index)
    output[used++] = '.';
  output[used] = '\0';
  return true;
}

inline std::size_t encodeCodepoint(uint32_t codepoint, char output[4]) {
  if (!output || codepoint == 0 || codepoint > 0x10ffff ||
      (codepoint >= 0xd800 && codepoint <= 0xdfff)) return 0;
  if (codepoint <= 0x7f) {
    output[0] = static_cast<char>(codepoint);
    return 1;
  }
  if (codepoint <= 0x7ff) {
    output[0] = static_cast<char>(0xc0 | (codepoint >> 6));
    output[1] = static_cast<char>(0x80 | (codepoint & 0x3f));
    return 2;
  }
  if (codepoint <= 0xffff) {
    output[0] = static_cast<char>(0xe0 | (codepoint >> 12));
    output[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
    output[2] = static_cast<char>(0x80 | (codepoint & 0x3f));
    return 3;
  }
  output[0] = static_cast<char>(0xf0 | (codepoint >> 18));
  output[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
  output[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
  output[3] = static_cast<char>(0x80 | (codepoint & 0x3f));
  return 4;
}

}  // namespace utf8
}  // namespace voxone

#endif
