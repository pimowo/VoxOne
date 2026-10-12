#include "../src/core/security_settings.h"

#include <cassert>
#include <cstdint>
#include <cstring>

using namespace voxone::security;

namespace {

class FakeStorage : public SecurityStorage {
 public:
  SecurityStorageReadStatus read(uint8_t* output, size_t capacity,
                                 size_t& outputSize) override {
    ++reads;
    outputSize = 0;
    if (readError) return SecurityStorageReadStatus::Error;
    if (!present) return SecurityStorageReadStatus::Missing;
    outputSize = size;
    if (size <= capacity) std::memcpy(output, bytes, size);
    return SecurityStorageReadStatus::Found;
  }

  bool write(const uint8_t* input, size_t inputSize) override {
    ++writes;
    if (writeError || inputSize > sizeof(bytes)) return false;
    uint8_t staged[kSecurityRecordSize];
    std::memcpy(staged, input, inputSize);
    std::memcpy(bytes, staged, inputSize);
    size = inputSize;
    present = true;
    return true;
  }

  bool erase() override {
    ++erases;
    if (eraseError) return false;
    std::memset(bytes, 0, sizeof(bytes));
    size = 0;
    present = false;
    return true;
  }

  uint8_t bytes[kSecurityRecordSize]{};
  size_t size = 0;
  unsigned reads = 0;
  unsigned writes = 0;
  unsigned erases = 0;
  bool present = false;
  bool readError = false;
  bool writeError = false;
  bool eraseError = false;
};

class DeterministicRandom : public SecurityRandomSource {
 public:
  bool fill(uint8_t* output, size_t size) override {
    ++calls;
    if (fail) return false;
    for (size_t i = 0; i < size; ++i)
      output[i] = allZero ? 0 : static_cast<uint8_t>(next + i);
    ++next;
    return true;
  }

  uint8_t next = 1;
  unsigned calls = 0;
  bool fail = false;
  bool allZero = false;
};

uint8_t hexNibble(char value) {
  if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f')
    return static_cast<uint8_t>(value - 'a' + 10);
  assert(false);
  return 0;
}

void assertHex(const uint8_t* actual, size_t size, const char* expected) {
  assert(std::strlen(expected) == size * 2);
  for (size_t i = 0; i < size; ++i) {
    const uint8_t byte = static_cast<uint8_t>(hexNibble(expected[i * 2]) << 4) |
                         hexNibble(expected[i * 2 + 1]);
    assert(actual[i] == byte);
  }
}

void testPbkdf2Vectors() {
  const uint8_t password[] = {'p', 'a', 's', 's', 'w', 'o', 'r', 'd'};
  const uint8_t salt[] = {'s', 'a', 'l', 't'};
  uint8_t derived[kSecurityVerifierSize];
  assert(securityPbkdf2HmacSha256(password, sizeof(password), salt,
                                  sizeof(salt), 1, derived));
  assertHex(derived, sizeof(derived),
            "120fb6cffcf8b32c43e7225256c4f837a"
            "86548c92ccc35480805987cb70be17b");
  assert(securityPbkdf2HmacSha256(password, sizeof(password), salt,
                                  sizeof(salt), 2, derived));
  assertHex(derived, sizeof(derived),
            "ae4d0c95af6b46d32d0adff928f06dd0"
            "2a303f8ef3c251dfd6e2d85a95474c43");
  assert(securityPbkdf2HmacSha256(password, sizeof(password), salt,
                                  sizeof(salt), 4096, derived));
  assertHex(derived, sizeof(derived),
            "c5e478d59288c841aa530db6845c4c8d"
            "962893a001ce4e11a4963873aa98134a");
  assert(!securityPbkdf2HmacSha256(password, sizeof(password), salt,
                                   sizeof(salt), 0, derived));
}

void testMigrationAndModeSelection() {
  FakeStorage storage;
  DeterministicRandom random;
  SecurityService service(storage, random);

  assert(service.begin(false) == SecurityLoadStatus::Unconfigured);
  assert(service.state() == SecurityState::Unconfigured);
  assert(service.chooseOpen() == SecurityOperationStatus::InvalidState);
  assert(service.setWifiConfigured(false) ==
         SecurityOperationStatus::Unchanged);
  assert(service.setWifiConfigured(true) == SecurityOperationStatus::Applied);
  assert(service.state() == SecurityState::LegacyUnclaimed);
  assert(storage.writes == 0);
  assert(service.chooseOpen() == SecurityOperationStatus::Applied);
  assert(service.state() == SecurityState::Open);
  assert(service.generation() == 1);
  assert(storage.writes == 1);
  assert(service.setWifiConfigured(false) ==
         SecurityOperationStatus::InvalidState);

  SecurityService restarted(storage, random);
  assert(restarted.begin(true) == SecurityLoadStatus::Loaded);
  assert(restarted.state() == SecurityState::Open);
  assert(restarted.generation() == 1);

  FakeStorage legacyStorage;
  SecurityService legacy(legacyStorage, random);
  assert(legacy.begin(true) == SecurityLoadStatus::LegacyUnclaimed);
  assert(legacy.state() == SecurityState::LegacyUnclaimed);
  assert(legacy.chooseProtected("bezpieczne-haslo") ==
         SecurityOperationStatus::Applied);
  assert(legacy.state() == SecurityState::Protected);
  assert(legacy.verifyPassword("bezpieczne-haslo"));
  assert(!legacy.verifyPassword("niepoprawne-haslo"));
}

void testCorruptAndInvalidStorage() {
  FakeStorage storage;
  DeterministicRandom random;
  SecurityService service(storage, random);
  assert(service.begin(true) == SecurityLoadStatus::LegacyUnclaimed);
  assert(service.chooseProtected("pierwsze-haslo") ==
         SecurityOperationStatus::Applied);

  storage.bytes[20] ^= 0x80;
  SecurityService corrupt(storage, random);
  assert(corrupt.begin(true) == SecurityLoadStatus::Corrupt);
  assert(corrupt.state() == SecurityState::Locked);
  assert(!corrupt.verifyPassword("pierwsze-haslo"));
  assert(corrupt.chooseOpen() == SecurityOperationStatus::InvalidState);

  storage.size = kSecurityRecordSize - 1;
  SecurityService truncated(storage, random);
  assert(truncated.begin(true) == SecurityLoadStatus::Corrupt);
  assert(truncated.state() == SecurityState::Locked);

  storage.readError = true;
  SecurityService unavailable(storage, random);
  assert(unavailable.begin(true) == SecurityLoadStatus::StorageError);
  assert(unavailable.state() == SecurityState::Locked);
}

void testPasswordChangesAndModeSwitches() {
  FakeStorage storage;
  DeterministicRandom random;
  SecurityService service(storage, random);
  assert(service.begin(true) == SecurityLoadStatus::LegacyUnclaimed);
  assert(service.chooseProtected("pierwsze-haslo") ==
         SecurityOperationStatus::Applied);
  uint8_t firstRecord[kSecurityRecordSize];
  std::memcpy(firstRecord, storage.bytes, sizeof(firstRecord));

  assert(service.changePassword("zle-haslo", "drugie-bezpieczne") ==
         SecurityOperationStatus::AuthenticationFailed);
  assert(service.changePassword("pierwsze-haslo", "trz") ==
         SecurityOperationStatus::InvalidPassword);
  assert(storage.writes == 1);
  random.fail = true;
  assert(service.changePassword("pierwsze-haslo", "drugie-bezpieczne") ==
         SecurityOperationStatus::RandomError);
  assert(service.verifyPassword("pierwsze-haslo"));
  assert(storage.writes == 1);
  random.fail = false;
  assert(service.changePassword("pierwsze-haslo", "drugie-bezpieczne") ==
         SecurityOperationStatus::Applied);
  assert(service.generation() == 2);
  assert(!service.verifyPassword("pierwsze-haslo"));
  assert(service.verifyPassword("drugie-bezpieczne"));
  assert(std::memcmp(firstRecord + 16, storage.bytes + 16,
                     kSecuritySaltSize) != 0);

  assert(service.unprotect("pierwsze-haslo") ==
         SecurityOperationStatus::AuthenticationFailed);
  assert(service.unprotect("drugie-bezpieczne") ==
         SecurityOperationStatus::Applied);
  assert(service.state() == SecurityState::Open);
  assert(service.generation() == 3);
  SecuritySettings open{};
  assert(deserializeSecuritySettings(storage.bytes, storage.size, open) ==
         SecurityDecodeStatus::Valid);
  assert(open.state == SecurityState::Open);
  for (size_t i = 0; i < sizeof(open.salt); ++i) assert(open.salt[i] == 0);
  for (size_t i = 0; i < sizeof(open.verifier); ++i)
    assert(open.verifier[i] == 0);

  assert(service.protectOpen("haslo-po-otwarciu") ==
         SecurityOperationStatus::Applied);
  assert(service.state() == SecurityState::Protected);
  assert(service.verifyPassword("haslo-po-otwarciu"));
  assert(service.protectOpen("kolejne-haslo") ==
         SecurityOperationStatus::InvalidState);
}

void testAtomicWriteFailures() {
  FakeStorage storage;
  DeterministicRandom random;
  SecurityService service(storage, random);
  assert(service.begin(true) == SecurityLoadStatus::LegacyUnclaimed);
  assert(service.chooseProtected("pierwsze-haslo") ==
         SecurityOperationStatus::Applied);
  uint8_t committed[kSecurityRecordSize];
  std::memcpy(committed, storage.bytes, sizeof(committed));
  const uint32_t generation = service.generation();

  storage.writeError = true;
  assert(service.changePassword("pierwsze-haslo", "drugie-bezpieczne") ==
         SecurityOperationStatus::StorageError);
  assert(service.state() == SecurityState::Protected);
  assert(service.generation() == generation);
  assert(service.verifyPassword("pierwsze-haslo"));
  assert(!service.verifyPassword("drugie-bezpieczne"));
  assert(std::memcmp(committed, storage.bytes, sizeof(committed)) == 0);

  SecurityService restarted(storage, random);
  assert(restarted.begin(true) == SecurityLoadStatus::Loaded);
  assert(restarted.verifyPassword("pierwsze-haslo"));
  assert(restarted.generation() == generation);
}

void testRandomAndEraseFailures() {
  FakeStorage storage;
  DeterministicRandom random;
  SecurityService service(storage, random);
  assert(service.begin(true) == SecurityLoadStatus::LegacyUnclaimed);
  random.fail = true;
  assert(service.chooseProtected("pierwsze-haslo") ==
         SecurityOperationStatus::RandomError);
  assert(service.state() == SecurityState::LegacyUnclaimed);
  assert(storage.writes == 0);
  random.fail = false;
  random.allZero = true;
  assert(service.chooseProtected("pierwsze-haslo") ==
         SecurityOperationStatus::RandomError);
  assert(storage.writes == 0);
  random.allZero = false;
  assert(service.chooseProtected("pierwsze-haslo") ==
         SecurityOperationStatus::Applied);

  storage.eraseError = true;
  assert(service.eraseForFactoryReset() ==
         SecurityOperationStatus::StorageError);
  assert(service.state() == SecurityState::Protected);
  assert(service.verifyPassword("pierwsze-haslo"));
  storage.eraseError = false;
  assert(service.eraseForFactoryReset() == SecurityOperationStatus::Applied);
  assert(service.state() == SecurityState::Unconfigured);
  assert(!storage.present);
}

void testUtf8AndPasswordBounds() {
  size_t bytes = 0;
  size_t characters = 0;
  assert(!securityValidateAdminPassword("trz", &bytes, &characters));
  assert(securityValidateAdminPassword("abcd", &bytes, &characters));
  assert(bytes == 4 && characters == 4);
  assert(securityValidateAdminPassword(
      "\xC4\x85\xC4\x99\xC4\x87\xC5\xBC", &bytes, &characters));
  assert(bytes == 8 && characters == 4);
  assert(securityValidateAdminPassword(
      "\xF0\x9F\x94\x92\xF0\x9F\x8E\xB5"
      "\xF0\x9F\x93\xBB\xF0\x9F\x9A\x80",
      &bytes, &characters));
  assert(bytes == 16 && characters == 4);
  assert(!securityValidateAdminPassword(
      "\xF0\x9F\x94\x92\xF0\x9F\x8E\xB5\xF0\x9F\x93\xBB"));

  assert(!securityValidateAdminPassword("abc\x80"));
  assert(!securityValidateAdminPassword("abc\xC0\xAF"));
  assert(!securityValidateAdminPassword("abc\xE0\x80\xAF"));
  assert(!securityValidateAdminPassword("abc\xED\xA0\x80"));
  assert(!securityValidateAdminPassword("abc\xF4\x90\x80\x80"));
  assert(!securityValidateAdminPassword("abc\xF0\x9F\x94"));

  char maximum[kMaxAdminPasswordBytes + 1];
  std::memset(maximum, 'a', sizeof(maximum));
  maximum[sizeof(maximum) - 1] = '\0';
  assert(securityValidateAdminPassword(maximum, &bytes, &characters));
  assert(bytes == kMaxAdminPasswordBytes &&
         characters == kMaxAdminPasswordBytes);

  FakeStorage storage;
  DeterministicRandom random;
  SecurityService service(storage, random);
  assert(service.begin(true) == SecurityLoadStatus::LegacyUnclaimed);
  assert(service.chooseProtected("za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87-123") ==
         SecurityOperationStatus::Applied);
  assert(service.verifyPassword("za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87-123"));

  char tooLong[kMaxAdminPasswordBytes + 2];
  std::memset(tooLong, 'a', sizeof(tooLong));
  tooLong[sizeof(tooLong) - 1] = '\0';
  assert(service.changePassword(
             "za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87-123", tooLong) ==
         SecurityOperationStatus::InvalidPassword);
}

}  // namespace

int main() {
  testPbkdf2Vectors();
  testMigrationAndModeSelection();
  testCorruptAndInvalidStorage();
  testPasswordChangesAndModeSwitches();
  testAtomicWriteFailures();
  testRandomAndEraseFailures();
  testUtf8AndPasswordBounds();
  return 0;
}
