#ifndef VOXONE_FIRST_BOOT_RUNTIME_SNAPSHOT_H
#define VOXONE_FIRST_BOOT_RUNTIME_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>

#include "first_boot_core.h"

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#else
#include <mutex>
#endif

namespace voxone {
namespace first_boot {

// Negative values are internal diagnostics. Positive values are the native
// Wi-Fi disconnect reason supplied by the ESP32 event.
enum RuntimeError : int {
  RuntimeNoError = 0,
  RuntimeConnectionTimeout = -1,
  RuntimeInvalidIpv4Readiness = -2,
  RuntimeWifiInventoryInvalid = -3
};

struct RuntimeSnapshot {
  bool initialized;
  uint32_t generation;
  Mode mode;
  Screen screen;
  Connection connection;
  // Set only after Network accepts a GOT_IP readiness event in this boot.
  // It is runtime history, not persisted configuration or authentication.
  bool staValidatedInThisBoot;
  ApSecurity apSecurity;
  bool apActive;
  bool presentApCredential;
  bool hasError;
  int errorCode;
  char product[16];
  char apSsid[33];
  char apAddress[32];
  char title[32];
  char statusText[32];
};

// A small, value-only handoff between Network callbacks and presentation
// consumers. No credential is accepted or stored here. The lock covers only
// bounded memory copies and model projection; it never covers network I/O.
class RuntimeSnapshotStore {
 public:
  RuntimeSnapshotStore()
      : wifiProfileCount_(0), connection_(Connection::Idle), errorCode_(0),
        staValidatedInThisBoot_(false), securityRecovery_(false),
        initialized_(false), generation_(0) {
    apSsid_[0] = '\0';
    apAddress_[0] = '\0';
    RuntimeSnapshot initial{};
    initial.initialized = false;
    initial.generation = 0;
    initial.mode = Mode::Normal;
    initial.screen = Screen::Player;
    initial.connection = Connection::Idle;
    initial.staValidatedInThisBoot = false;
    initial.apSecurity = ApSecurity::NotActive;
    copyText(initial.product, sizeof(initial.product), "VoxOne");
    copyText(initial.title, sizeof(initial.title), titleText(Mode::Normal));
    copyText(initial.statusText, sizeof(initial.statusText),
             statusText(Mode::Normal));
    snapshot_ = initial;
  }

  RuntimeSnapshotStore(const RuntimeSnapshotStore&) = delete;
  RuntimeSnapshotStore& operator=(const RuntimeSnapshotStore&) = delete;

  RuntimeSnapshot read() const {
    lock();
    const RuntimeSnapshot value = snapshot_;
    unlock();
    return value;
  }

  void initialize(uint8_t wifiProfileCount) {
    lock();
    wifiProfileCount_ = wifiProfileCount;
    connection_ = Connection::Idle;
    errorCode_ = RuntimeNoError;
    staValidatedInThisBoot_ = false;
    securityRecovery_ = false;
    clearApLocked();
    initialized_ = true;
    rebuildLocked();
    unlock();
  }

  void inventoryInvalid() {
    lock();
    connection_ = Connection::Failed;
    errorCode_ = RuntimeWifiInventoryInvalid;
    staValidatedInThisBoot_ = false;
    securityRecovery_ = false;
    clearApLocked();
    initialized_ = true;
    rebuildLocked();
    unlock();
  }

  void beginStaAttempt() {
    lock();
    connection_ = Connection::Connecting;
    errorCode_ = RuntimeNoError;
    securityRecovery_ = false;
    clearApLocked();
    rebuildLocked();
    unlock();
  }

  void recordDisconnect(int reasonCode) {
    lock();
    if (!apConnectionLocked()) {
      connection_ = Connection::Connecting;
      errorCode_ = reasonCode;
      rebuildLocked();
    }
    unlock();
  }

  void recordTimeout() {
    lock();
    if (!apConnectionLocked()) {
      connection_ = Connection::Connecting;
      if (errorCode_ == RuntimeNoError)
        errorCode_ = RuntimeConnectionTimeout;
      rebuildLocked();
    }
    unlock();
  }

  void connected() {
    lock();
    // A late STA event must not replace an AP state. A new explicit STA
    // attempt clears the AP state before a GOT_IP event can be accepted.
    if (!apConnectionLocked()) {
      connection_ = Connection::Connected;
      errorCode_ = RuntimeNoError;
      staValidatedInThisBoot_ = true;
      clearApLocked();
      rebuildLocked();
    }
    unlock();
  }

  void activateAp(const char* ssid, const char* address, bool started) {
    lock();
    securityRecovery_ = false;
    const bool complete = started && ssid && ssid[0] != '\0' && address &&
        address[0] != '\0';
    if (complete) {
      connection_ = wifiProfileCount_ == 0 ? Connection::SetupAp
                                           : Connection::RecoveryAp;
      copyText(apSsid_, sizeof(apSsid_), ssid);
      copyText(apAddress_, sizeof(apAddress_), address);
    } else {
      connection_ = Connection::Failed;
      clearApLocked();
    }
    rebuildLocked();
    unlock();
  }

  void activateSecurityRecoveryAp(const char* ssid, const char* address,
                                  bool started) {
    lock();
    const bool complete = started && ssid && ssid[0] != '\0' && address &&
        address[0] != '\0';
    securityRecovery_ = complete;
    initialized_ = true;
    errorCode_ = RuntimeNoError;
    staValidatedInThisBoot_ = false;
    if (complete) {
      connection_ = Connection::RecoveryAp;
      copyText(apSsid_, sizeof(apSsid_), ssid);
      copyText(apAddress_, sizeof(apAddress_), address);
    } else {
      connection_ = Connection::Failed;
      clearApLocked();
    }
    rebuildLocked();
    unlock();
  }

 private:
  static void copyText(char* output, size_t outputSize, const char* input) {
    if (outputSize == 0) return;
    size_t offset = 0;
    if (input) {
      while (offset + 1 < outputSize && input[offset] != '\0') {
        output[offset] = input[offset];
        ++offset;
      }
    }
    output[offset] = '\0';
  }

  bool apConnectionLocked() const {
    return connection_ == Connection::SetupAp ||
           connection_ == Connection::RecoveryAp;
  }

  void clearApLocked() {
    apSsid_[0] = '\0';
    apAddress_[0] = '\0';
  }

  void rebuildLocked() {
    const Inputs inputs{wifiProfileCount_, connection_, false,
                        securityRecovery_,
                        apSsid_, apAddress_, errorCode_};
    const View view = makeView(inputs);
    RuntimeSnapshot next{};
    next.initialized = initialized_;
    next.generation = ++generation_;
    next.mode = view.mode;
    next.screen = view.screen;
    next.connection = view.connection;
    next.staValidatedInThisBoot = staValidatedInThisBoot_;
    next.apSecurity = view.apSecurity;
    next.apActive = view.apActive;
    next.presentApCredential = view.presentApCredential;
    next.hasError = view.hasError;
    next.errorCode = view.errorCode;
    copyText(next.product, sizeof(next.product), view.product);
    copyText(next.apSsid, sizeof(next.apSsid), view.apSsid);
    copyText(next.apAddress, sizeof(next.apAddress), view.address);
    copyText(next.title, sizeof(next.title), view.title);
    copyText(next.statusText, sizeof(next.statusText), view.statusText);
    snapshot_ = next;
  }

  void lock() const {
#if defined(ARDUINO)
    portENTER_CRITICAL(&mux_);
#else
    mutex_.lock();
#endif
  }

  void unlock() const {
#if defined(ARDUINO)
    portEXIT_CRITICAL(&mux_);
#else
    mutex_.unlock();
#endif
  }

  uint8_t wifiProfileCount_;
  Connection connection_;
  int errorCode_;
  bool staValidatedInThisBoot_;
  bool securityRecovery_;
  bool initialized_;
  uint32_t generation_;
  char apSsid_[33];
  char apAddress_[32];
  RuntimeSnapshot snapshot_;
#if defined(ARDUINO)
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
#else
  mutable std::mutex mutex_;
#endif
};

}  // namespace first_boot
}  // namespace voxone

#endif
