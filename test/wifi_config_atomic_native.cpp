#include "../src/core/wifi_config_atomic.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

namespace {

using namespace voxone::wifi_config_atomic;

struct FakeStorage {
  std::map<std::string, std::string> files;
  bool failWrite = false;
  int failRename = 0;
  int renameCalls = 0;

  bool exists(const char* path) const { return files.count(path) != 0; }
  bool remove(const char* path) { return files.erase(path) == 1; }
  bool rename(const char* from, const char* to) {
    ++renameCalls;
    if (failRename == renameCalls || !exists(from) || exists(to)) return false;
    files[to] = files[from];
    files.erase(from);
    return true;
  }
  bool write(const char* path, const uint8_t* bytes, std::size_t size) {
    if (failWrite) {
      files[path] = size ? std::string(reinterpret_cast<const char*>(bytes), size - 1) : "";
      return false;
    }
    files[path] = std::string(reinterpret_cast<const char*>(bytes), size);
    return true;
  }
  bool matches(const char* path, const uint8_t* bytes, std::size_t size) const {
    const auto found = files.find(path);
    return found != files.end() && found->second.size() == size &&
           std::memcmp(found->second.data(), bytes, size) == 0;
  }
};

bool replaceText(FakeStorage& storage, const std::string& value) {
  return replace(storage, reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

void assertClean(const FakeStorage& storage) {
  assert(!storage.exists(kTemporaryPath));
  assert(!storage.exists(kRollbackPath));
}

}  // namespace

int main() {
  const std::string oldRecord = "old-test\told-secret\n";
  const std::string newRecord = "new-test\tnew-secret\n";
  const std::string nextRecord = "next-test\tnext-secret\n";

  // A, E, F: first write creates exactly NEW without orphan files.
  {
    FakeStorage storage;
    assert(replaceText(storage, newRecord));
    assert(storage.files[kActivePath] == newRecord);
    assertClean(storage);
  }

  // B, E, F, H: an existing normal wifi.csv is atomically replaced.
  {
    FakeStorage storage;
    storage.files[kActivePath] = oldRecord;
    assert(replaceText(storage, newRecord));
    assert(storage.files[kActivePath] == newRecord);
    assertClean(storage);
  }

  // C: a failed/partial candidate write leaves OLD untouched.
  {
    FakeStorage storage;
    storage.files[kActivePath] = oldRecord;
    storage.failWrite = true;
    assert(!replaceText(storage, newRecord));
    assert(storage.files[kActivePath] == oldRecord);
    assertClean(storage);
  }

  // D: failure while moving OLD leaves OLD active.
  {
    FakeStorage storage;
    storage.files[kActivePath] = oldRecord;
    storage.failRename = 1;
    assert(!replaceText(storage, newRecord));
    assert(storage.files[kActivePath] == oldRecord);
    assertClean(storage);
  }

  // D: failure while installing NEW restores OLD.
  {
    FakeStorage storage;
    storage.files[kActivePath] = oldRecord;
    storage.failRename = 2;
    assert(!replaceText(storage, newRecord));
    assert(storage.files[kActivePath] == oldRecord);
    assertClean(storage);
  }

  // G: repeated replacements retain only the newest complete generation.
  {
    FakeStorage storage;
    storage.files[kActivePath] = oldRecord;
    assert(replaceText(storage, newRecord));
    assert(replaceText(storage, nextRecord));
    assert(storage.files[kActivePath] == nextRecord);
    assertClean(storage);
  }

  // A power cut after OLD->rollback is recovered to OLD on the next boot.
  {
    FakeStorage storage;
    storage.files[kRollbackPath] = oldRecord;
    storage.files[kTemporaryPath] = newRecord;
    assert(recover(storage));
    assert(storage.files[kActivePath] == oldRecord);
    assertClean(storage);
  }

  // A completed NEW generation wins over a stale rollback after a power cut.
  {
    FakeStorage storage;
    storage.files[kActivePath] = newRecord;
    storage.files[kRollbackPath] = oldRecord;
    assert(recover(storage));
    assert(storage.files[kActivePath] == newRecord);
    assertClean(storage);
  }

  return 0;
}
