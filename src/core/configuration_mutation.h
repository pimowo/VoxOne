#ifndef VOXONE_CONFIGURATION_MUTATION_H
#define VOXONE_CONFIGURATION_MUTATION_H

#include <atomic>
#include <stdint.h>

namespace voxone {
namespace configuration_mutation {

enum class Owner : uint8_t {
  None = 0,
  FirstBoot,
  HardwareApi,
  ConfigRestore,
  WifiRecovery,
  SerialHardware,
  SerialWifi,
  ConfigBackup,
};

class Coordinator {
 public:
  bool tryAcquire(Owner owner) {
    uint8_t expected = static_cast<uint8_t>(Owner::None);
    return owner != Owner::None &&
           owner_.compare_exchange_strong(expected,
                                          static_cast<uint8_t>(owner));
  }

  void release(Owner owner) {
    uint8_t expected = static_cast<uint8_t>(owner);
    (void)owner_.compare_exchange_strong(
        expected, static_cast<uint8_t>(Owner::None));
  }

  Owner owner() const { return static_cast<Owner>(owner_.load()); }

 private:
  std::atomic<uint8_t> owner_{static_cast<uint8_t>(Owner::None)};
};

inline Coordinator& deviceCoordinator() {
  static Coordinator coordinator;
  return coordinator;
}

class Lease {
 public:
  Lease(Coordinator& coordinator, Owner owner)
      : coordinator_(coordinator), owner_(owner), acquired_(
            coordinator_.tryAcquire(owner_)) {}
  ~Lease() {
    if (acquired_) coordinator_.release(owner_);
  }

  bool acquired() const { return acquired_; }

  Lease(const Lease&) = delete;
  Lease& operator=(const Lease&) = delete;

 private:
  Coordinator& coordinator_;
  Owner owner_;
  bool acquired_;
};

}  // namespace configuration_mutation
}  // namespace voxone

#endif
