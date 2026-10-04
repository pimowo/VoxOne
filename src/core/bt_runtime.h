#ifndef VOXONE_BT_RUNTIME_H
#define VOXONE_BT_RUNTIME_H

#include <atomic>

#include "bt_link_protocol.h"

// BT hardware, user enablement, module availability and phone connection
// are separate facts. Physical initialization happens only during setup.
struct BtRuntimeStatus {
  bool supportsBt;
  bool btEnabled;
  bool btOnline;
  bool btConnected;
};

class BtRuntime {
 public:
  explicit BtRuntime(bool supportsBt)
      : supportsBt_(supportsBt), btEnabled_(supportsBt) {}

  // May be called only before start(); no persistent setting is stored.
  bool configureBeforeStart(bool enabled) {
    if (started_) return false;
    btEnabled_.store(enabled);
    return true;
  }

  bool start() {
    if (started_) return physicalStarted_;
    started_ = true;
    physicalStarted_ = supportsBt_ && btEnabled_.load();
    return physicalStarted_;
  }

  // Runtime toggles affect source availability. A boot without BT does not
  // acquire UART/I2S later; that requires a separate hot-start lifecycle.
  void setEnabled(bool enabled) { btEnabled_.store(enabled); }
  bool physicalStarted() const { return physicalStarted_; }
  bool available() const { return physicalStarted_ && btEnabled_.load(); }

  const BtLinkState& effectiveLinkState(const BtLinkState& raw) const {
    return available() ? raw : unavailableLink_;
  }

  BtRuntimeStatus status(bool moduleOnline, bool phoneConnected) const {
    const bool online = available() && moduleOnline;
    return {supportsBt_, btEnabled_.load(), online, online && phoneConnected};
  }

 private:
  bool supportsBt_;
  std::atomic<bool> btEnabled_;
  bool started_ = false;
  bool physicalStarted_ = false;
  BtLinkState unavailableLink_{};
};

extern BtRuntime btRuntime;

#endif
