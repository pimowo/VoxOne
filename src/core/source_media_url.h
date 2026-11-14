#ifndef VOXONE_SOURCE_MEDIA_URL_H
#define VOXONE_SOURCE_MEDIA_URL_H

#include <stddef.h>

namespace voxone {
namespace source_media {

constexpr size_t kUrlCapacity = 512;

inline bool isSafeUrl(const char* url, size_t length) {
  if (!url || !length || length >= kUrlCapacity) return false;

  size_t authorityStart = 0;
  if (length > 7 && url[0] == 'h' && url[1] == 't' && url[2] == 't' &&
      url[3] == 'p' && url[4] == ':' && url[5] == '/' && url[6] == '/') {
    authorityStart = 7;
  } else if (length > 8 && url[0] == 'h' && url[1] == 't' &&
             url[2] == 't' && url[3] == 'p' && url[4] == 's' &&
             url[5] == ':' && url[6] == '/' && url[7] == '/') {
    authorityStart = 8;
  } else {
    return false;
  }

  size_t authorityEnd = authorityStart;
  while (authorityEnd < length && url[authorityEnd] != '/' &&
         url[authorityEnd] != '?' && url[authorityEnd] != '#') {
    ++authorityEnd;
  }
  if (authorityEnd == authorityStart) return false;

  for (size_t index = 0; index < length; ++index) {
    const unsigned char character = static_cast<unsigned char>(url[index]);
    if (character <= 0x20 || character >= 0x7f || character == '\\' ||
        character == '"' || character == '<' || character == '>')
      return false;
    if (index >= authorityStart && index < authorityEnd && character == '@')
      return false;
  }
  return true;
}

}  // namespace source_media
}  // namespace voxone

#endif
