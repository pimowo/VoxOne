#ifndef VOXONE_RADIO_URL_REQUEST_PARTS_H
#define VOXONE_RADIO_URL_REQUEST_PARTS_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <cctype>
#include <limits>

namespace voxone {
namespace radio {

class OwnedCString {
 public:
  using Allocate = void* (*)(size_t);
  using Release = void (*)(void*);

  OwnedCString() = default;
  OwnedCString(const OwnedCString&) = delete;
  OwnedCString& operator=(const OwnedCString&) = delete;
  ~OwnedCString() { reset(); }

  bool assign(const char* source, Allocate allocator = &malloc,
              Release releaser = &free) {
    reset();
    if (!source || !allocator || !releaser) return false;
    const size_t length = strlen(source);
    if (length == std::numeric_limits<size_t>::max()) return false;
    data_ = static_cast<char*>(allocator(length + 1));
    if (!data_) return false;
    memcpy(data_, source, length + 1);
    releaser_ = releaser;
    return true;
  }

  void reset() {
    if (data_ && releaser_) releaser_(data_);
    data_ = nullptr;
    releaser_ = nullptr;
  }

  char* get() const { return data_; }

 private:
  char* data_ = nullptr;
  Release releaser_ = nullptr;
};

inline size_t spacesOnlyEncodedCapacity(const char* source) {
  if (!source) return 0;
  size_t length = 0;
  for (const unsigned char* cursor =
           reinterpret_cast<const unsigned char*>(source);
       *cursor; ++cursor) {
    const size_t add = *cursor == ' ' ? 3u : 1u;
    if (length > std::numeric_limits<size_t>::max() - add) return 0;
    length += add;
  }
  return length == std::numeric_limits<size_t>::max() ? 0 : length + 1;
}

inline bool urlEncodeInPlace(char* buffer, size_t capacity,
                             bool spacesOnly = false) {
  if (!buffer || !capacity) return false;
  const size_t length = strlen(buffer);
  if (length >= capacity) return false;

  size_t encodedLength = length;
  for (size_t index = 0; index < length; ++index) {
    const unsigned char value = static_cast<unsigned char>(buffer[index]);
    const bool encode = spacesOnly ? value == ' ' : !std::isalnum(value);
    if (!encode) continue;
    if (capacity - encodedLength <= 2) return false;
    encodedLength += 2;
  }

  static const char hex[] = "0123456789ABCDEF";
  buffer[encodedLength] = '\0';
  size_t source = length;
  size_t destination = encodedLength;
  while (source) {
    const unsigned char value =
        static_cast<unsigned char>(buffer[--source]);
    const bool encode = spacesOnly ? value == ' ' : !std::isalnum(value);
    if (encode) {
      buffer[--destination] = hex[value & 0x0f];
      buffer[--destination] = hex[value >> 4];
      buffer[--destination] = '%';
    } else {
      buffer[--destination] = static_cast<char>(value);
    }
  }
  return true;
}

class UrlRequestParts {
 public:
  using Allocate = void* (*)(size_t);
  using Release = void (*)(void*);

  UrlRequestParts() = default;
  UrlRequestParts(const UrlRequestParts&) = delete;
  UrlRequestParts& operator=(const UrlRequestParts&) = delete;
  ~UrlRequestParts() { reset(); }

  bool assign(const char* authorityAndPath, int slash,
              Allocate allocator = &malloc, Release releaser = &free) {
    reset();
    if (!authorityAndPath || !allocator || !releaser) return false;
    release_ = releaser;

    const size_t totalLength = strlen(authorityAndPath);
    const bool hasPath = slash > 1 && static_cast<size_t>(slash) < totalLength;
    const size_t authorityLength = hasPath ? static_cast<size_t>(slash)
                                           : totalLength;
    const char* pathSource = hasPath ? authorityAndPath + slash : "/";
    const size_t pathSourceLength = strlen(pathSource);
    pathCapacity_ = spacesOnlyEncodedCapacity(pathSource);
    if (authorityLength == std::numeric_limits<size_t>::max() ||
        pathCapacity_ == 0) {
      reset();
      return false;
    }

    authority_ = static_cast<char*>(allocator(authorityLength + 1));
    if (!authority_) {
      reset();
      return false;
    }
    path_ = static_cast<char*>(allocator(pathCapacity_));
    if (!path_) {
      reset();
      return false;
    }
    memcpy(authority_, authorityAndPath, authorityLength);
    authority_[authorityLength] = '\0';
    // Copy only the source bytes. pathCapacity_ describes the destination
    // after encoding and may be larger than the source.
    memcpy(path_, pathSource, pathSourceLength + 1);
    return true;
  }

  void reset() {
    if (authority_ && release_) release_(authority_);
    if (path_ && release_) release_(path_);
    authority_ = nullptr;
    path_ = nullptr;
    pathCapacity_ = 0;
    release_ = nullptr;
  }

  char* authority() const { return authority_; }
  char* path() const { return path_; }
  size_t pathCapacity() const { return pathCapacity_; }

 private:
  char* authority_ = nullptr;
  char* path_ = nullptr;
  size_t pathCapacity_ = 0;
  Release release_ = nullptr;
};

}  // namespace radio
}  // namespace voxone

#endif
