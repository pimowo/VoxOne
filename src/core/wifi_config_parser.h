#ifndef VOXONE_WIFI_CONFIG_PARSER_H
#define VOXONE_WIFI_CONFIG_PARSER_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace voxone {
namespace wifi_config {

constexpr std::size_t kMaxProfiles = 5;
constexpr std::size_t kSsidCapacity = 30;
constexpr std::size_t kPasswordCapacity = 40;
constexpr std::size_t kMaxRecordBytes =
    (kSsidCapacity - 1) + 1 + (kPasswordCapacity - 1) + 1;
constexpr std::size_t kMaxFileBytes = kMaxProfiles * kMaxRecordBytes;

struct Profile {
  char ssid[kSsidCapacity];
  char password[kPasswordCapacity];
};

enum class ParseStatus : uint8_t {
  Ok,
  Empty,
  InvalidArgument,
  TooManyProfiles,
  MissingSeparator,
  ExtraSeparator,
  EmptySsid,
  SsidTooLong,
  PasswordTooLong,
  InvalidCharacter,
  InvalidUtf8,
  FileTooLarge
};

struct ParseResult {
  ParseStatus status;
  std::size_t profileCount;
  std::size_t recordNumber;

  bool valid() const {
    return status == ParseStatus::Ok || status == ParseStatus::Empty;
  }
};

inline const char* parseStatusName(ParseStatus status) {
  switch (status) {
    case ParseStatus::Ok: return "ok";
    case ParseStatus::Empty: return "empty";
    case ParseStatus::InvalidArgument: return "invalid_argument";
    case ParseStatus::TooManyProfiles: return "too_many_profiles";
    case ParseStatus::MissingSeparator: return "missing_separator";
    case ParseStatus::ExtraSeparator: return "extra_separator";
    case ParseStatus::EmptySsid: return "empty_ssid";
    case ParseStatus::SsidTooLong: return "ssid_too_long";
    case ParseStatus::PasswordTooLong: return "password_too_long";
    case ParseStatus::InvalidCharacter: return "invalid_character";
    case ParseStatus::InvalidUtf8: return "invalid_utf8";
    case ParseStatus::FileTooLarge: return "file_too_large";
  }
  return "unknown";
}

inline bool validUtf8(const uint8_t* bytes, std::size_t size) {
  if (!bytes && size) return false;
  std::size_t offset = 0;
  while (offset < size) {
    const uint8_t first = bytes[offset++];
    if (first <= 0x7f) continue;

    std::size_t continuationCount = 0;
    uint8_t secondMinimum = 0x80;
    uint8_t secondMaximum = 0xbf;
    if (first >= 0xc2 && first <= 0xdf) {
      continuationCount = 1;
    } else if (first == 0xe0) {
      continuationCount = 2;
      secondMinimum = 0xa0;
    } else if (first >= 0xe1 && first <= 0xec) {
      continuationCount = 2;
    } else if (first == 0xed) {
      continuationCount = 2;
      secondMaximum = 0x9f;
    } else if (first >= 0xee && first <= 0xef) {
      continuationCount = 2;
    } else if (first == 0xf0) {
      continuationCount = 3;
      secondMinimum = 0x90;
    } else if (first >= 0xf1 && first <= 0xf3) {
      continuationCount = 3;
    } else if (first == 0xf4) {
      continuationCount = 3;
      secondMaximum = 0x8f;
    } else {
      return false;
    }

    if (continuationCount > size - offset) return false;
    const uint8_t second = bytes[offset];
    if (second < secondMinimum || second > secondMaximum) return false;
    for (std::size_t index = 0; index < continuationCount; ++index) {
      const uint8_t continuation = bytes[offset + index];
      if (continuation < 0x80 || continuation > 0xbf) return false;
    }
    offset += continuationCount;
  }
  return true;
}

inline bool validFieldBytes(const uint8_t* bytes, std::size_t size) {
  if (!bytes && size) return false;
  for (std::size_t index = 0; index < size; ++index) {
    if (bytes[index] == 0 || bytes[index] == '\t' || bytes[index] == '\r' ||
        bytes[index] == '\n')
      return false;
  }
  return validUtf8(bytes, size);
}

inline ParseStatus parseRecord(const uint8_t* bytes, std::size_t size,
                               Profile& output) {
  std::memset(&output, 0, sizeof(output));
  if (!bytes && size) return ParseStatus::InvalidArgument;

  std::size_t separator = size;
  for (std::size_t index = 0; index < size; ++index) {
    if (bytes[index] != '\t') continue;
    if (separator != size) return ParseStatus::ExtraSeparator;
    separator = index;
  }
  if (separator == size) return ParseStatus::MissingSeparator;

  const std::size_t ssidLength = separator;
  const std::size_t passwordLength = size - separator - 1;
  if (!ssidLength) return ParseStatus::EmptySsid;
  if (ssidLength >= kSsidCapacity) return ParseStatus::SsidTooLong;
  if (passwordLength >= kPasswordCapacity)
    return ParseStatus::PasswordTooLong;
  if (!validFieldBytes(bytes, ssidLength) ||
      !validFieldBytes(bytes + separator + 1, passwordLength)) {
    return (!validUtf8(bytes, ssidLength) ||
            !validUtf8(bytes + separator + 1, passwordLength))
               ? ParseStatus::InvalidUtf8
               : ParseStatus::InvalidCharacter;
  }

  std::memcpy(output.ssid, bytes, ssidLength);
  output.ssid[ssidLength] = '\0';
  std::memcpy(output.password, bytes + separator + 1, passwordLength);
  output.password[passwordLength] = '\0';
  return ParseStatus::Ok;
}

inline ParseResult parseFile(const uint8_t* bytes, std::size_t size,
                             Profile* output, std::size_t outputCapacity) {
  if (!output || outputCapacity < kMaxProfiles || (!bytes && size))
    return {ParseStatus::InvalidArgument, 0, 0};
  std::memset(output, 0, sizeof(Profile) * outputCapacity);
  if (!size) return {ParseStatus::Empty, 0, 0};
  if (size > kMaxFileBytes)
    return {ParseStatus::FileTooLarge, 0, 0};

  Profile parsed[kMaxProfiles]{};
  std::size_t count = 0;
  std::size_t offset = 0;
  while (offset < size) {
    std::size_t end = offset;
    while (end < size && bytes[end] != '\n') ++end;
    if (count == kMaxProfiles)
      return {ParseStatus::TooManyProfiles, 0, count + 1};
    const ParseStatus status = parseRecord(bytes + offset, end - offset,
                                           parsed[count]);
    if (status != ParseStatus::Ok) return {status, 0, count + 1};
    ++count;
    offset = end < size ? end + 1 : end;
  }

  std::memcpy(output, parsed, sizeof(Profile) * count);
  return {ParseStatus::Ok, count, 0};
}

inline bool validSerializedConfig(const uint8_t* bytes, std::size_t size) {
  Profile profiles[kMaxProfiles]{};
  return parseFile(bytes, size, profiles, kMaxProfiles).valid();
}

}  // namespace wifi_config
}  // namespace voxone

#endif
