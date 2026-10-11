#ifndef VOXONE_WIFI_CONFIG_ATOMIC_H
#define VOXONE_WIFI_CONFIG_ATOMIC_H

#include <cstddef>
#include <cstdint>

namespace voxone {
namespace wifi_config_atomic {

constexpr char kActivePath[] = "/data/wifi.csv";
constexpr char kTemporaryPath[] = "/data/wifi.csv.tmp";
constexpr char kRollbackPath[] = "/data/wifi.csv.old";

template <typename Storage>
bool recover(Storage& storage) {
  if (storage.exists(kRollbackPath)) {
    if (storage.exists(kActivePath)) {
      if (!storage.remove(kRollbackPath)) return false;
    } else if (!storage.rename(kRollbackPath, kActivePath)) {
      return false;
    }
  }
  return !storage.exists(kTemporaryPath) || storage.remove(kTemporaryPath);
}

template <typename Storage>
bool restoreOld(Storage& storage, bool hadActive) {
  if (!hadActive)
    return !storage.exists(kActivePath) || storage.remove(kActivePath);
  if (!storage.exists(kRollbackPath)) return false;
  if (storage.exists(kActivePath) && !storage.remove(kActivePath)) return false;
  return storage.rename(kRollbackPath, kActivePath);
}

template <typename Storage>
bool replace(Storage& storage, const uint8_t* bytes, std::size_t size) {
  if ((!bytes && size) || !recover(storage)) return false;
  if (!storage.write(kTemporaryPath, bytes, size) ||
      !storage.matches(kTemporaryPath, bytes, size)) {
    if (storage.exists(kTemporaryPath)) storage.remove(kTemporaryPath);
    return false;
  }

  const bool hadActive = storage.exists(kActivePath);
  if (hadActive && !storage.rename(kActivePath, kRollbackPath)) {
    storage.remove(kTemporaryPath);
    return false;
  }
  if (!storage.rename(kTemporaryPath, kActivePath)) {
    restoreOld(storage, hadActive);
    if (storage.exists(kTemporaryPath)) storage.remove(kTemporaryPath);
    return false;
  }
  if (!storage.matches(kActivePath, bytes, size)) {
    restoreOld(storage, hadActive);
    return false;
  }
  if (storage.exists(kRollbackPath) && !storage.remove(kRollbackPath)) {
    restoreOld(storage, hadActive);
    return false;
  }
  return !storage.exists(kTemporaryPath);
}

}  // namespace wifi_config_atomic
}  // namespace voxone

#endif
