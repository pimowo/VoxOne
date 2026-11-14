#ifndef VOXONE_SD_PATH_H
#define VOXONE_SD_PATH_H

#include <stddef.h>
#include <string.h>

namespace voxone {
namespace sd_path {

constexpr size_t kPathCapacity = 256;

inline bool valid(const char* path) {
  if (!path || path[0] != '/') return false;
  const size_t length = strlen(path);
  if (length == 0 || length >= kPathCapacity) return false;
  size_t segmentStart = 1;
  for (size_t index = 1; index <= length; ++index) {
    const unsigned char ch = static_cast<unsigned char>(path[index]);
    if (ch == '\\' || (ch != 0 && (ch < 0x20 || ch == 0x7f))) return false;
    if (ch != '/' && ch != 0) continue;
    const size_t segmentLength = index - segmentStart;
    if (index != length && segmentLength == 0) return false;
    if ((segmentLength == 1 && path[segmentStart] == '.') ||
        (segmentLength == 2 && path[segmentStart] == '.' &&
         path[segmentStart + 1] == '.'))
      return false;
    segmentStart = index + 1;
  }
  return true;
}

inline bool audioFile(const char* path) {
  if (!valid(path)) return false;
  const char* dot = strrchr(path, '.');
  if (!dot || !dot[1]) return false;
  auto equal = [](char left, char right) {
    if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
    return left == right;
  };
  auto extension = [&](const char* expected) {
    const char* actual = dot + 1;
    while (*actual && *expected && equal(*actual, *expected)) {
      ++actual;
      ++expected;
    }
    return *actual == 0 && *expected == 0;
  };
  return extension("mp3") || extension("aac") || extension("m4a") ||
         extension("wav") || extension("flac");
}

inline const char* mimeType(const char* path) {
  if (!audioFile(path)) return "application/octet-stream";
  const char* dot = strrchr(path, '.');
  char first = dot[1];
  if (first >= 'A' && first <= 'Z') first += 'a' - 'A';
  if (first == 'm' && (dot[2] == 'p' || dot[2] == 'P')) return "audio/mpeg";
  if (first == 'm') return "audio/mp4";
  if (first == 'a') return "audio/aac";
  if (first == 'w') return "audio/wav";
  return "audio/flac";
}

inline bool parent(const char* path, char* output, size_t capacity) {
  if (!valid(path) || !output || capacity < 2) return false;
  if (strcmp(path, "/") == 0) {
    output[0] = '/';
    output[1] = 0;
    return true;
  }
  const char* slash = strrchr(path, '/');
  const size_t length = slash == path ? 1 : static_cast<size_t>(slash - path);
  if (length + 1 > capacity) return false;
  memcpy(output, path, length);
  output[length] = 0;
  return true;
}

}  // namespace sd_path
}  // namespace voxone

#endif
