#ifndef VOXONE_WIFI_CONFIG_ATOMIC_H
#define VOXONE_WIFI_CONFIG_ATOMIC_H

#include <cstddef>
#include <cstdint>

namespace voxone {
namespace wifi_config_atomic {

constexpr char kActivePath[] = "/data/wifi.csv";
constexpr char kTemporaryPath[] = "/data/wifi.csv.tmp";
constexpr char kRollbackPath[] = "/data/wifi.csv.old";

inline std::uint32_t recordCrc32(const std::uint8_t* bytes,
                                 std::size_t size) {
  std::uint32_t crc = 0xffffffffU;
  for (std::size_t index = 0; index < size; ++index) {
    crc ^= bytes[index];
    for (std::uint8_t bit = 0; bit < 8U; ++bit)
      crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}

enum class FirstBootFileState : std::uint8_t {
  Missing,
  EmptyBaseline,
  Candidate,
  Scratch,
  Unexpected,
};

template <typename Storage>
FirstBootFileState inspectFirstBootFile(
    const Storage& storage, const char* path, bool scratch,
    std::size_t candidateSize, std::uint32_t candidateCrc) {
  if (!storage.exists(path)) return FirstBootFileState::Missing;
  if (storage.matches(path, nullptr, 0U) && storage.valid(path))
    return FirstBootFileState::EmptyBaseline;
  if (storage.matchesFingerprint(path, candidateSize, candidateCrc))
    return FirstBootFileState::Candidate;
  return scratch ? FirstBootFileState::Scratch
                 : FirstBootFileState::Unexpected;
}

template <typename Storage>
bool removeIfPresent(Storage& storage, const char* path) {
  return !storage.exists(path) || storage.remove(path);
}

// Restores the only two baselines accepted by FIRST BOOT: a missing active
// file or an existing empty active file. The candidate is identified only by
// size and CRC from the transaction journal; no Wi-Fi secret is journaled.
//
// Unlike recover(), this routine never promotes a non-empty candidate. Every
// mutation leaves either the same decision inputs or one step closer to the
// baseline, so a power cut during rollback is safe to retry.
template <typename Storage>
bool rollbackFirstBoot(Storage& storage, bool priorEmpty,
                       std::size_t candidateSize,
                       std::uint32_t candidateCrc) {
  const FirstBootFileState active = inspectFirstBootFile(
      storage, kActivePath, false, candidateSize, candidateCrc);
  const FirstBootFileState temporary = inspectFirstBootFile(
      storage, kTemporaryPath, true, candidateSize, candidateCrc);
  const FirstBootFileState rollback = inspectFirstBootFile(
      storage, kRollbackPath, false, candidateSize, candidateCrc);

  if (active == FirstBootFileState::Unexpected ||
      rollback == FirstBootFileState::Unexpected)
    return false;

  if (!priorEmpty) {
    if (active == FirstBootFileState::EmptyBaseline ||
        rollback == FirstBootFileState::EmptyBaseline)
      return false;
    return removeIfPresent(storage, kTemporaryPath) &&
           removeIfPresent(storage, kRollbackPath) &&
           removeIfPresent(storage, kActivePath) &&
           !storage.exists(kTemporaryPath) &&
           !storage.exists(kRollbackPath) &&
           !storage.exists(kActivePath);
  }

  if (active == FirstBootFileState::EmptyBaseline) {
    return removeIfPresent(storage, kTemporaryPath) &&
           removeIfPresent(storage, kRollbackPath) &&
           storage.matches(kActivePath, nullptr, 0U) &&
           storage.valid(kActivePath) &&
           !storage.exists(kTemporaryPath) &&
           !storage.exists(kRollbackPath);
  }

  if (rollback == FirstBootFileState::EmptyBaseline) {
    return removeIfPresent(storage, kActivePath) &&
           removeIfPresent(storage, kTemporaryPath) &&
           storage.rename(kRollbackPath, kActivePath) &&
           storage.matches(kActivePath, nullptr, 0U) &&
           storage.valid(kActivePath) &&
           !storage.exists(kTemporaryPath) &&
           !storage.exists(kRollbackPath);
  }

  if (temporary == FirstBootFileState::EmptyBaseline) {
    return removeIfPresent(storage, kActivePath) &&
           removeIfPresent(storage, kRollbackPath) &&
           storage.rename(kTemporaryPath, kActivePath) &&
           storage.matches(kActivePath, nullptr, 0U) &&
           storage.valid(kActivePath) &&
           !storage.exists(kTemporaryPath) &&
           !storage.exists(kRollbackPath);
  }

  // A completed candidate may already have removed the old empty file. The
  // baseline contains no secret, so recreate it through tmp and promote it.
  if (!removeIfPresent(storage, kTemporaryPath) ||
      !removeIfPresent(storage, kRollbackPath) ||
      !removeIfPresent(storage, kActivePath) ||
      !storage.write(kTemporaryPath, nullptr, 0U) ||
      !storage.matches(kTemporaryPath, nullptr, 0U) ||
      !storage.valid(kTemporaryPath) ||
      !storage.rename(kTemporaryPath, kActivePath))
    return false;
  return storage.matches(kActivePath, nullptr, 0U) &&
         storage.valid(kActivePath) &&
         !storage.exists(kTemporaryPath) &&
         !storage.exists(kRollbackPath);
}

template <typename Storage>
bool recover(Storage& storage) {
  bool recovered = true;
  const bool activeExists = storage.exists(kActivePath);
  const bool activeValid = !activeExists || storage.valid(kActivePath);
  if (storage.exists(kRollbackPath)) {
    const bool rollbackValid = storage.valid(kRollbackPath);
    if (activeExists && activeValid) {
      if (!storage.remove(kRollbackPath)) return false;
    } else if (rollbackValid) {
      if (activeExists && !storage.remove(kActivePath)) return false;
      if (!storage.rename(kRollbackPath, kActivePath) ||
          !storage.valid(kActivePath))
        return false;
    } else {
      recovered = false;
    }
  } else if (activeExists && !activeValid) {
    recovered = false;
  }
  if (storage.exists(kTemporaryPath) && !storage.remove(kTemporaryPath))
    return false;
  return recovered;
}

template <typename Storage>
bool restoreOld(Storage& storage, bool hadActive) {
  if (!hadActive)
    return !storage.exists(kActivePath) || storage.remove(kActivePath);
  if (!storage.exists(kRollbackPath)) return false;
  if (storage.exists(kActivePath) && !storage.remove(kActivePath)) return false;
  return storage.rename(kRollbackPath, kActivePath) &&
         storage.valid(kActivePath);
}

template <typename Storage>
bool replace(Storage& storage, const uint8_t* bytes, std::size_t size) {
  if ((!bytes && size) || !storage.validBytes(bytes, size) ||
      !recover(storage))
    return false;
  if (!storage.write(kTemporaryPath, bytes, size) ||
      !storage.matches(kTemporaryPath, bytes, size) ||
      !storage.valid(kTemporaryPath)) {
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
  if (!storage.matches(kActivePath, bytes, size) ||
      !storage.valid(kActivePath)) {
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
