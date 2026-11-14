#ifndef VOXONE_SOURCE_MEDIA_REQUEST_H
#define VOXONE_SOURCE_MEDIA_REQUEST_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "source_media_url.h"

namespace voxone {
namespace source_media {

constexpr size_t kMetadataCapacity = 128;
// URL stays below kUrlCapacity. Percent-encoded UTF-8 metadata can use three
// bytes per decoded byte, plus both field names and separators.
constexpr size_t kPlayUrlCommandCapacity = 1536;

enum class Source : uint8_t { HomeAssistant, Dlna, Local };

struct Request {
  const char* url = "";
  const char* title = "";
  const char* artist = "";
  Source source = Source::HomeAssistant;
  bool returnToPrevious = false;

  Request() = default;
  Request(const char* requestUrl, const char* requestTitle,
          const char* requestArtist, Source requestSource,
          bool shouldReturn = false)
      : url(requestUrl), title(requestTitle), artist(requestArtist),
        source(requestSource), returnToPrevious(shouldReturn) {}
};

// Parsing and protocol adapters may use bounded stack storage, while the
// cross-task mailbox carries only a pointer to an exact-sized owned copy.
// Keeping storage separate makes Request a small, non-owning view.
struct RequestStorage {
  char url[kUrlCapacity]{};
  char title[kMetadataCapacity]{};
  char artist[kMetadataCapacity]{};
  bool returnToPrevious = false;

  Request view(Source source = Source::HomeAssistant,
               bool defaultReturn = false) const {
    return Request(url, title, artist, source,
                   returnToPrevious || defaultReturn);
  }
};

inline const char* findLastField(const char* value, size_t length,
                                 const char* marker) {
  const size_t markerLength = strlen(marker);
  if (!value || markerLength > length) return nullptr;
  const char* found = nullptr;
  for (size_t index = 0; index + markerLength <= length; ++index) {
    if (memcmp(value + index, marker, markerLength) == 0)
      found = value + index;
  }
  return found;
}

inline bool decodeMetadata(const char* encoded, size_t length,
                           char* output, size_t capacity) {
  if (!output || capacity == 0) return false;
  size_t written = 0;
  for (size_t index = 0; index < length; ++index) {
    unsigned char value = static_cast<unsigned char>(encoded[index]);
    if (value == '%') {
      if (index + 2 >= length) return false;
      auto hex = [](unsigned char digit) -> int {
        if (digit >= '0' && digit <= '9') return digit - '0';
        if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
        if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
        return -1;
      };
      const int high = hex(static_cast<unsigned char>(encoded[index + 1]));
      const int low = hex(static_cast<unsigned char>(encoded[index + 2]));
      if (high < 0 || low < 0) return false;
      value = static_cast<unsigned char>((high << 4) | low);
      index += 2;
    } else if (value == '+') {
      value = ' ';
    }
    if (value < 0x20 || value == 0x7f || written + 1 >= capacity) return false;
    output[written++] = static_cast<char>(value);
  }
  output[written] = '\0';
  return true;
}

inline bool parseRequest(const char* value, size_t length,
                         RequestStorage& storage) {
  storage = RequestStorage{};
  if (!value || length == 0 || length >= kPlayUrlCommandCapacity) return false;

  const char* titleField = findLastField(value, length, "&title=");
  const char* artistField = findLastField(value, length, "&artist=");
  const char* returnField =
      findLastField(value, length, "&return_to_previous=");
  size_t urlLength = length;
  size_t fieldsEnd = length;
  if (returnField) {
    const char* returnValue = returnField + strlen("&return_to_previous=");
    const size_t returnLength =
        length - static_cast<size_t>(returnValue - value);
    if ((returnLength == 4 && memcmp(returnValue, "true", 4) == 0) ||
        (returnLength == 1 && returnValue[0] == '1')) {
      storage.returnToPrevious = true;
    } else if (!((returnLength == 5 &&
                  memcmp(returnValue, "false", 5) == 0) ||
                 (returnLength == 1 && returnValue[0] == '0'))) {
      return false;
    }
    fieldsEnd = static_cast<size_t>(returnField - value);
  }
  if (titleField || artistField) {
    // Metadata is an unambiguous suffix only when both fields are present in
    // the order emitted by VoxOneHA. A URL containing one similarly named
    // query parameter remains a normal URL.
    if (!titleField || !artistField || titleField >= artistField ||
        (returnField && artistField >= returnField)) return false;
    urlLength = static_cast<size_t>(titleField - value);
    const char* titleValue = titleField + strlen("&title=");
    const char* artistValue = artistField + strlen("&artist=");
    if (!decodeMetadata(titleValue,
                        static_cast<size_t>(artistField - titleValue),
                        storage.title, sizeof(storage.title)) ||
        !decodeMetadata(artistValue,
                        fieldsEnd - static_cast<size_t>(artistValue - value),
                        storage.artist, sizeof(storage.artist)))
      return false;
  } else if (returnField) {
    urlLength = static_cast<size_t>(returnField - value);
  } else {
    memcpy(storage.title, "Home Assistant", sizeof("Home Assistant"));
  }

  if (!isSafeUrl(value, urlLength)) return false;
  memcpy(storage.url, value, urlLength);
  storage.url[urlLength] = '\0';
  if (storage.title[0] == '\0')
    memcpy(storage.title, "Home Assistant", sizeof("Home Assistant"));
  return true;
}

}  // namespace source_media
}  // namespace voxone

#endif
