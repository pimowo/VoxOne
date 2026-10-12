#ifndef VOXONE_SECURITY_SETTINGS_H
#define VOXONE_SECURITY_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace security {

constexpr uint8_t kSecuritySettingsVersion = 1;
constexpr uint32_t kPbkdf2Iterations = 10000;
constexpr size_t kSecuritySaltSize = 16;
constexpr size_t kSecurityVerifierSize = 32;
constexpr size_t kSecurityRecordSize = 68;
constexpr size_t kMinAdminPasswordBytes = 8;
constexpr size_t kMaxAdminPasswordBytes = 64;

enum class SecurityState : uint8_t {
  Unconfigured,
  LegacyUnclaimed,
  Protected,
  Open,
  Locked
};

enum class SecurityStorageReadStatus : uint8_t {
  Found,
  Missing,
  Error
};

enum class SecurityLoadStatus : uint8_t {
  Unconfigured,
  LegacyUnclaimed,
  Loaded,
  Corrupt,
  StorageError
};

enum class SecurityOperationStatus : uint8_t {
  Applied,
  Unchanged,
  InvalidState,
  InvalidPassword,
  AuthenticationFailed,
  RandomError,
  StorageError
};

enum class SecurityDecodeStatus : uint8_t {
  Valid,
  InvalidSize,
  InvalidMagic,
  InvalidVersion,
  Corrupt,
  InvalidRecord
};

struct SecuritySettings {
  SecurityState state;
  uint32_t generation;
  uint32_t iterations;
  uint8_t salt[kSecuritySaltSize];
  uint8_t verifier[kSecurityVerifierSize];
};

class SecurityStorage {
 public:
  virtual ~SecurityStorage() {}
  virtual SecurityStorageReadStatus read(uint8_t* output, size_t capacity,
                                         size_t& size) = 0;
  // Implementations must not replace the prior record unless the complete
  // record can be committed atomically.
  virtual bool write(const uint8_t* input, size_t size) = 0;
  virtual bool erase() = 0;
};

class SecurityRandomSource {
 public:
  virtual ~SecurityRandomSource() {}
  virtual bool fill(uint8_t* output, size_t size) = 0;
};

uint32_t securityRecordCrc32(const uint8_t* data, size_t size);
bool serializeSecuritySettings(const SecuritySettings& settings,
                               uint8_t* output, size_t outputSize);
SecurityDecodeStatus deserializeSecuritySettings(const uint8_t* input,
                                                  size_t inputSize,
                                                  SecuritySettings& output);

bool securityPbkdf2HmacSha256(const uint8_t* password, size_t passwordSize,
                              const uint8_t* salt, size_t saltSize,
                              uint32_t iterations,
                              uint8_t output[kSecurityVerifierSize]);

class SecurityService {
 public:
  SecurityService(SecurityStorage& storage, SecurityRandomSource& random);

  SecurityLoadStatus begin(bool wifiConfigured);
  SecurityOperationStatus setWifiConfigured(bool wifiConfigured);
  SecurityOperationStatus chooseOpen();
  SecurityOperationStatus chooseProtected(const char* password);
  SecurityOperationStatus protectOpen(const char* password);
  SecurityOperationStatus unprotect(const char* currentPassword);
  SecurityOperationStatus changePassword(const char* currentPassword,
                                         const char* newPassword);
  SecurityOperationStatus eraseForFactoryReset();

  bool verifyPassword(const char* password) const;
  SecurityState state() const { return settings_.state; }
  SecurityLoadStatus loadStatus() const { return loadStatus_; }
  uint32_t generation() const { return settings_.generation; }

 private:
  SecurityOperationStatus saveOpen();
  SecurityOperationStatus saveProtected(const char* password);
  SecurityOperationStatus persist(const SecuritySettings& candidate);

  SecurityStorage& storage_;
  SecurityRandomSource& random_;
  SecuritySettings settings_;
  SecurityLoadStatus loadStatus_;
};

// Device-owned instance. Startup is coordinated by security_runtime; route
// enforcement and session handling remain outside this service.
SecurityService& deviceSecurityService();

const char* securityStateName(SecurityState state);
const char* securityLoadStatusName(SecurityLoadStatus status);

}  // namespace security
}  // namespace voxone

#endif
