#include "security_settings.h"

#include <cstring>

#if defined(ARDUINO)
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#endif

namespace voxone {
namespace security {
namespace {

constexpr uint32_t kSecurityMagic = 0x43535856U;  // "VXSC" little-endian.
constexpr uint8_t kModeOpen = 1;
constexpr uint8_t kModeProtected = 2;
constexpr uint8_t kAlgorithmNone = 0;
constexpr uint8_t kAlgorithmPbkdf2Sha256 = 1;
constexpr uint32_t kMinAcceptedIterations = 1000;
constexpr uint32_t kMaxAcceptedIterations = 1000000;

#if !defined(ARDUINO)
uint32_t rotateRight(uint32_t value, uint8_t count) {
  return (value >> count) | (value << (32U - count));
}
#endif

void secureZero(void* memory, size_t size) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(memory);
  while (size--) *bytes++ = 0;
}

uint32_t readLe32(const uint8_t* input) {
  return static_cast<uint32_t>(input[0]) |
         (static_cast<uint32_t>(input[1]) << 8) |
         (static_cast<uint32_t>(input[2]) << 16) |
         (static_cast<uint32_t>(input[3]) << 24);
}

void writeLe32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
  output[2] = static_cast<uint8_t>(value >> 16);
  output[3] = static_cast<uint8_t>(value >> 24);
}

bool isAllZero(const uint8_t* input, size_t size) {
  uint8_t combined = 0;
  for (size_t i = 0; i < size; ++i) combined |= input[i];
  return combined == 0;
}

bool constantTimeEqual(const uint8_t* left, const uint8_t* right, size_t size) {
  uint8_t difference = 0;
  for (size_t i = 0; i < size; ++i) difference |= left[i] ^ right[i];
  return difference == 0;
}

#if !defined(ARDUINO)
struct Sha256Context {
  uint32_t state[8];
  uint64_t totalBytes;
  uint8_t block[64];
  size_t blockSize;
};

constexpr uint32_t kSha256Constants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

void sha256Transform(Sha256Context& context, const uint8_t block[64]) {
  uint32_t words[64];
  for (size_t i = 0; i < 16; ++i) {
    const size_t offset = i * 4;
    words[i] = (static_cast<uint32_t>(block[offset]) << 24) |
               (static_cast<uint32_t>(block[offset + 1]) << 16) |
               (static_cast<uint32_t>(block[offset + 2]) << 8) |
               static_cast<uint32_t>(block[offset + 3]);
  }
  for (size_t i = 16; i < 64; ++i) {
    const uint32_t s0 = rotateRight(words[i - 15], 7) ^
                        rotateRight(words[i - 15], 18) ^
                        (words[i - 15] >> 3);
    const uint32_t s1 = rotateRight(words[i - 2], 17) ^
                        rotateRight(words[i - 2], 19) ^
                        (words[i - 2] >> 10);
    words[i] = words[i - 16] + s0 + words[i - 7] + s1;
  }

  uint32_t a = context.state[0];
  uint32_t b = context.state[1];
  uint32_t c = context.state[2];
  uint32_t d = context.state[3];
  uint32_t e = context.state[4];
  uint32_t f = context.state[5];
  uint32_t g = context.state[6];
  uint32_t h = context.state[7];
  for (size_t i = 0; i < 64; ++i) {
    const uint32_t sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^
                          rotateRight(e, 25);
    const uint32_t choose = (e & f) ^ ((~e) & g);
    const uint32_t temp1 = h + sum1 + choose + kSha256Constants[i] + words[i];
    const uint32_t sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^
                          rotateRight(a, 22);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = sum0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  context.state[0] += a;
  context.state[1] += b;
  context.state[2] += c;
  context.state[3] += d;
  context.state[4] += e;
  context.state[5] += f;
  context.state[6] += g;
  context.state[7] += h;
  secureZero(words, sizeof(words));
}

void sha256Init(Sha256Context& context) {
  const uint32_t initial[8] = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                               0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                               0x1f83d9abU, 0x5be0cd19U};
  std::memcpy(context.state, initial, sizeof(initial));
  context.totalBytes = 0;
  context.blockSize = 0;
}

void sha256Update(Sha256Context& context, const uint8_t* input, size_t size) {
  if (!input || !size) return;
  context.totalBytes += size;
  while (size) {
    const size_t available = sizeof(context.block) - context.blockSize;
    const size_t copied = size < available ? size : available;
    std::memcpy(context.block + context.blockSize, input, copied);
    context.blockSize += copied;
    input += copied;
    size -= copied;
    if (context.blockSize == sizeof(context.block)) {
      sha256Transform(context, context.block);
      context.blockSize = 0;
    }
  }
}

void sha256Finish(Sha256Context& context, uint8_t output[32]) {
  const uint64_t totalBits = context.totalBytes * 8U;
  context.block[context.blockSize++] = 0x80;
  if (context.blockSize > 56) {
    std::memset(context.block + context.blockSize, 0,
                sizeof(context.block) - context.blockSize);
    sha256Transform(context, context.block);
    context.blockSize = 0;
  }
  std::memset(context.block + context.blockSize, 0, 56 - context.blockSize);
  for (size_t i = 0; i < 8; ++i)
    context.block[63 - i] = static_cast<uint8_t>(totalBits >> (i * 8));
  sha256Transform(context, context.block);
  for (size_t i = 0; i < 8; ++i) {
    output[i * 4] = static_cast<uint8_t>(context.state[i] >> 24);
    output[i * 4 + 1] = static_cast<uint8_t>(context.state[i] >> 16);
    output[i * 4 + 2] = static_cast<uint8_t>(context.state[i] >> 8);
    output[i * 4 + 3] = static_cast<uint8_t>(context.state[i]);
  }
  secureZero(&context, sizeof(context));
}

void hmacSha256(const uint8_t* key, size_t keySize, const uint8_t* input,
                size_t inputSize, uint8_t output[32]) {
  uint8_t keyBlock[64]{};
  if (keySize > sizeof(keyBlock)) {
    Sha256Context keyContext;
    sha256Init(keyContext);
    sha256Update(keyContext, key, keySize);
    sha256Finish(keyContext, keyBlock);
  } else if (keySize) {
    std::memcpy(keyBlock, key, keySize);
  }
  uint8_t innerPad[64];
  uint8_t outerPad[64];
  for (size_t i = 0; i < sizeof(keyBlock); ++i) {
    innerPad[i] = keyBlock[i] ^ 0x36;
    outerPad[i] = keyBlock[i] ^ 0x5c;
  }
  Sha256Context context;
  sha256Init(context);
  sha256Update(context, innerPad, sizeof(innerPad));
  sha256Update(context, input, inputSize);
  uint8_t innerHash[32];
  sha256Finish(context, innerHash);
  sha256Init(context);
  sha256Update(context, outerPad, sizeof(outerPad));
  sha256Update(context, innerHash, sizeof(innerHash));
  sha256Finish(context, output);
  secureZero(keyBlock, sizeof(keyBlock));
  secureZero(innerPad, sizeof(innerPad));
  secureZero(outerPad, sizeof(outerPad));
  secureZero(innerHash, sizeof(innerHash));
}
#endif

SecuritySettings defaultSettings(SecurityState state) {
  SecuritySettings result{};
  result.state = state;
  return result;
}

bool validPersistentSettings(const SecuritySettings& settings) {
  if (!settings.generation) return false;
  if (settings.state == SecurityState::Open) {
    return settings.iterations == 0 &&
           isAllZero(settings.salt, sizeof(settings.salt)) &&
           isAllZero(settings.verifier, sizeof(settings.verifier));
  }
  if (settings.state != SecurityState::Protected) return false;
  return settings.iterations >= kMinAcceptedIterations &&
         settings.iterations <= kMaxAcceptedIterations &&
         !isAllZero(settings.salt, sizeof(settings.salt));
}

}  // namespace

uint32_t securityRecordCrc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}

bool serializeSecuritySettings(const SecuritySettings& settings,
                               uint8_t* output, size_t outputSize) {
  if (!output || outputSize != kSecurityRecordSize ||
      !validPersistentSettings(settings)) return false;
  std::memset(output, 0, outputSize);
  writeLe32(output, kSecurityMagic);
  output[4] = kSecuritySettingsVersion;
  output[5] = settings.state == SecurityState::Open ? kModeOpen : kModeProtected;
  output[6] = settings.state == SecurityState::Open ? kAlgorithmNone
                                                    : kAlgorithmPbkdf2Sha256;
  writeLe32(output + 8, settings.generation);
  writeLe32(output + 12, settings.iterations);
  std::memcpy(output + 16, settings.salt, sizeof(settings.salt));
  std::memcpy(output + 32, settings.verifier, sizeof(settings.verifier));
  writeLe32(output + 64, securityRecordCrc32(output, 64));
  return true;
}

SecurityDecodeStatus deserializeSecuritySettings(const uint8_t* input,
                                                  size_t inputSize,
                                                  SecuritySettings& output) {
  if (!input || inputSize != kSecurityRecordSize)
    return SecurityDecodeStatus::InvalidSize;
  if (readLe32(input) != kSecurityMagic)
    return SecurityDecodeStatus::InvalidMagic;
  if (input[4] != kSecuritySettingsVersion)
    return SecurityDecodeStatus::InvalidVersion;
  if (readLe32(input + 64) != securityRecordCrc32(input, 64))
    return SecurityDecodeStatus::Corrupt;
  if (input[7] != 0) return SecurityDecodeStatus::InvalidRecord;

  SecuritySettings candidate{};
  candidate.generation = readLe32(input + 8);
  candidate.iterations = readLe32(input + 12);
  std::memcpy(candidate.salt, input + 16, sizeof(candidate.salt));
  std::memcpy(candidate.verifier, input + 32, sizeof(candidate.verifier));
  if (input[5] == kModeOpen && input[6] == kAlgorithmNone)
    candidate.state = SecurityState::Open;
  else if (input[5] == kModeProtected &&
           input[6] == kAlgorithmPbkdf2Sha256)
    candidate.state = SecurityState::Protected;
  else {
    secureZero(&candidate, sizeof(candidate));
    return SecurityDecodeStatus::InvalidRecord;
  }
  if (!validPersistentSettings(candidate)) {
    secureZero(&candidate, sizeof(candidate));
    return SecurityDecodeStatus::InvalidRecord;
  }
  output = candidate;
  secureZero(&candidate, sizeof(candidate));
  return SecurityDecodeStatus::Valid;
}

bool securityPbkdf2HmacSha256(const uint8_t* password, size_t passwordSize,
                              const uint8_t* salt, size_t saltSize,
                              uint32_t iterations,
                              uint8_t output[kSecurityVerifierSize]) {
  if ((!password && passwordSize) || (!salt && saltSize) || !iterations ||
      !output || saltSize > 60) return false;
#if defined(ARDUINO)
  static const uint8_t empty = 0;
  mbedtls_md_context_t context;
  mbedtls_md_init(&context);
  const mbedtls_md_info_t* info =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  const bool initialized = info && mbedtls_md_setup(&context, info, 1) == 0;
  const int result = initialized
                         ? mbedtls_pkcs5_pbkdf2_hmac(
                               &context, password ? password : &empty,
                               passwordSize, salt ? salt : &empty, saltSize,
                               iterations, kSecurityVerifierSize, output)
                         : -1;
  mbedtls_md_free(&context);
  return result == 0;
#else
  uint8_t initialInput[64];
  if (saltSize) std::memcpy(initialInput, salt, saltSize);
  initialInput[saltSize] = 0;
  initialInput[saltSize + 1] = 0;
  initialInput[saltSize + 2] = 0;
  initialInput[saltSize + 3] = 1;

  uint8_t block[32];
  hmacSha256(password, passwordSize, initialInput, saltSize + 4, block);
  std::memcpy(output, block, sizeof(block));
  for (uint32_t iteration = 1; iteration < iterations; ++iteration) {
    uint8_t next[32];
    hmacSha256(password, passwordSize, block, sizeof(block), next);
    for (size_t i = 0; i < sizeof(block); ++i) output[i] ^= next[i];
    std::memcpy(block, next, sizeof(block));
    secureZero(next, sizeof(next));
  }
  secureZero(initialInput, sizeof(initialInput));
  secureZero(block, sizeof(block));
  return true;
#endif
}

bool securitySha256(const uint8_t* input, size_t inputSize,
                    uint8_t output[kSecurityVerifierSize]) {
  if ((!input && inputSize) || !output) return false;
#if defined(ARDUINO)
  static const uint8_t empty = 0;
  const mbedtls_md_info_t* info =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return info &&
         mbedtls_md(info, input ? input : &empty, inputSize, output) == 0;
#else
  Sha256Context context;
  sha256Init(context);
  sha256Update(context, input, inputSize);
  sha256Finish(context, output);
  return true;
#endif
}

bool securityValidateAdminPassword(const char* password, size_t* byteLength,
                                   size_t* characterCount) {
  if (byteLength) *byteLength = 0;
  if (characterCount) *characterCount = 0;
  if (!password) return false;

  size_t length = 0;
  while (length <= kMaxAdminPasswordBytes && password[length]) ++length;
  if (length > kMaxAdminPasswordBytes) return false;

  size_t characters = 0;
  size_t offset = 0;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(password);
  while (offset < length) {
    const uint8_t first = bytes[offset];
    size_t sequenceSize = 0;
    if (first <= 0x7f) {
      sequenceSize = 1;
    } else if (first >= 0xc2 && first <= 0xdf) {
      sequenceSize = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
      sequenceSize = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
      sequenceSize = 4;
    } else {
      return false;
    }
    if (sequenceSize > length - offset) return false;
    for (size_t index = 1; index < sequenceSize; ++index) {
      if ((bytes[offset + index] & 0xc0) != 0x80) return false;
    }
    if (sequenceSize == 3 &&
        ((first == 0xe0 && bytes[offset + 1] < 0xa0) ||
         (first == 0xed && bytes[offset + 1] >= 0xa0)))
      return false;
    if (sequenceSize == 4 &&
        ((first == 0xf0 && bytes[offset + 1] < 0x90) ||
         (first == 0xf4 && bytes[offset + 1] >= 0x90)))
      return false;
    offset += sequenceSize;
    ++characters;
  }

  if (characters < kMinAdminPasswordCharacters) return false;
  if (byteLength) *byteLength = length;
  if (characterCount) *characterCount = characters;
  return true;
}

SecurityService::SecurityService(SecurityStorage& storage,
                                 SecurityRandomSource& random)
    : storage_(storage),
      random_(random),
      settings_(defaultSettings(SecurityState::Unconfigured)),
      loadStatus_(SecurityLoadStatus::Unconfigured) {}

SecurityLoadStatus SecurityService::begin(bool wifiConfigured) {
  uint8_t record[kSecurityRecordSize]{};
  size_t size = 0;
  const SecurityStorageReadStatus readStatus =
      storage_.read(record, sizeof(record), size);
  if (readStatus == SecurityStorageReadStatus::Missing) {
    settings_ = defaultSettings(wifiConfigured ? SecurityState::LegacyUnclaimed
                                               : SecurityState::Unconfigured);
    loadStatus_ = wifiConfigured ? SecurityLoadStatus::LegacyUnclaimed
                                 : SecurityLoadStatus::Unconfigured;
    secureZero(record, sizeof(record));
    return loadStatus_;
  }
  if (readStatus == SecurityStorageReadStatus::Error) {
    settings_ = defaultSettings(SecurityState::Locked);
    loadStatus_ = SecurityLoadStatus::StorageError;
    secureZero(record, sizeof(record));
    return loadStatus_;
  }
  SecuritySettings loaded{};
  if (deserializeSecuritySettings(record, size, loaded) !=
      SecurityDecodeStatus::Valid) {
    settings_ = defaultSettings(SecurityState::Locked);
    loadStatus_ = SecurityLoadStatus::Corrupt;
    secureZero(record, sizeof(record));
    secureZero(&loaded, sizeof(loaded));
    return loadStatus_;
  }
  settings_ = loaded;
  loadStatus_ = SecurityLoadStatus::Loaded;
  secureZero(record, sizeof(record));
  secureZero(&loaded, sizeof(loaded));
  return loadStatus_;
}

SecurityOperationStatus SecurityService::setWifiConfigured(
    bool wifiConfigured) {
  if (settings_.state != SecurityState::Unconfigured &&
      settings_.state != SecurityState::LegacyUnclaimed)
    return SecurityOperationStatus::InvalidState;
  const SecurityState target = wifiConfigured ? SecurityState::LegacyUnclaimed
                                              : SecurityState::Unconfigured;
  if (settings_.state == target) return SecurityOperationStatus::Unchanged;
  settings_ = defaultSettings(target);
  loadStatus_ = wifiConfigured ? SecurityLoadStatus::LegacyUnclaimed
                               : SecurityLoadStatus::Unconfigured;
  return SecurityOperationStatus::Applied;
}

SecurityOperationStatus SecurityService::completeWifiProvisioningOpen() {
  if (settings_.state != SecurityState::Unconfigured)
    return SecurityOperationStatus::InvalidState;
  return saveOpen();
}

SecurityOperationStatus SecurityService::chooseOpen() {
  if (settings_.state != SecurityState::LegacyUnclaimed)
    return SecurityOperationStatus::InvalidState;
  return saveOpen();
}

SecurityOperationStatus SecurityService::chooseProtected(
    const char* password) {
  if (settings_.state != SecurityState::LegacyUnclaimed)
    return SecurityOperationStatus::InvalidState;
  return saveProtected(password);
}

SecurityOperationStatus SecurityService::protectOpen(const char* password) {
  if (settings_.state != SecurityState::Open)
    return SecurityOperationStatus::InvalidState;
  return saveProtected(password);
}

SecurityOperationStatus SecurityService::unprotect(
    const char* currentPassword) {
  if (settings_.state != SecurityState::Protected)
    return SecurityOperationStatus::InvalidState;
  if (!verifyPassword(currentPassword))
    return SecurityOperationStatus::AuthenticationFailed;
  return saveOpen();
}

SecurityOperationStatus SecurityService::changePassword(
    const char* currentPassword, const char* newPassword) {
  if (settings_.state != SecurityState::Protected)
    return SecurityOperationStatus::InvalidState;
  if (!verifyPassword(currentPassword))
    return SecurityOperationStatus::AuthenticationFailed;
  return saveProtected(newPassword);
}

SecurityOperationStatus SecurityService::eraseForFactoryReset() {
  if (!storage_.erase()) return SecurityOperationStatus::StorageError;
  settings_ = defaultSettings(SecurityState::Unconfigured);
  loadStatus_ = SecurityLoadStatus::Unconfigured;
  return SecurityOperationStatus::Applied;
}

bool SecurityService::verifyPassword(const char* password) const {
  if (settings_.state != SecurityState::Protected) return false;
  size_t length = 0;
  if (!securityValidateAdminPassword(password, &length)) return false;
  uint8_t candidate[kSecurityVerifierSize];
  if (!securityPbkdf2HmacSha256(
          reinterpret_cast<const uint8_t*>(password), length, settings_.salt,
          sizeof(settings_.salt), settings_.iterations, candidate)) {
    secureZero(candidate, sizeof(candidate));
    return false;
  }
  const bool matches = constantTimeEqual(candidate, settings_.verifier,
                                         sizeof(candidate));
  secureZero(candidate, sizeof(candidate));
  return matches;
}

SecurityOperationStatus SecurityService::saveOpen() {
  SecuritySettings candidate = defaultSettings(SecurityState::Open);
  candidate.generation = settings_.generation + 1;
  if (!candidate.generation) candidate.generation = 1;
  return persist(candidate);
}

SecurityOperationStatus SecurityService::saveProtected(const char* password) {
  size_t length = 0;
  if (!securityValidateAdminPassword(password, &length))
    return SecurityOperationStatus::InvalidPassword;
  SecuritySettings candidate = defaultSettings(SecurityState::Protected);
  candidate.generation = settings_.generation + 1;
  if (!candidate.generation) candidate.generation = 1;
  candidate.iterations = kPbkdf2Iterations;
  if (!random_.fill(candidate.salt, sizeof(candidate.salt))) {
    secureZero(&candidate, sizeof(candidate));
    return SecurityOperationStatus::RandomError;
  }
  if (isAllZero(candidate.salt, sizeof(candidate.salt))) {
    secureZero(&candidate, sizeof(candidate));
    return SecurityOperationStatus::RandomError;
  }
  if (!securityPbkdf2HmacSha256(
          reinterpret_cast<const uint8_t*>(password), length, candidate.salt,
          sizeof(candidate.salt), candidate.iterations, candidate.verifier)) {
    secureZero(&candidate, sizeof(candidate));
    return SecurityOperationStatus::InvalidPassword;
  }
  const SecurityOperationStatus result = persist(candidate);
  secureZero(&candidate, sizeof(candidate));
  return result;
}

SecurityOperationStatus SecurityService::persist(
    const SecuritySettings& candidate) {
  uint8_t record[kSecurityRecordSize];
  if (!serializeSecuritySettings(candidate, record, sizeof(record)))
    return SecurityOperationStatus::InvalidState;
  if (!storage_.write(record, sizeof(record))) {
    secureZero(record, sizeof(record));
    return SecurityOperationStatus::StorageError;
  }
  settings_ = candidate;
  loadStatus_ = SecurityLoadStatus::Loaded;
  secureZero(record, sizeof(record));
  return SecurityOperationStatus::Applied;
}

const char* securityStateName(SecurityState state) {
  switch (state) {
    case SecurityState::Unconfigured: return "unconfigured";
    case SecurityState::LegacyUnclaimed: return "legacy-unclaimed";
    case SecurityState::Protected: return "protected";
    case SecurityState::Open: return "open";
    case SecurityState::Locked: return "locked";
  }
  return "unknown";
}

const char* securityLoadStatusName(SecurityLoadStatus status) {
  switch (status) {
    case SecurityLoadStatus::Unconfigured: return "unconfigured";
    case SecurityLoadStatus::LegacyUnclaimed: return "legacy-unclaimed";
    case SecurityLoadStatus::Loaded: return "loaded";
    case SecurityLoadStatus::Corrupt: return "corrupt";
    case SecurityLoadStatus::StorageError: return "storage-error";
  }
  return "unknown";
}

}  // namespace security
}  // namespace voxone
