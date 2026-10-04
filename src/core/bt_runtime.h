#ifndef VOXONE_BT_RUNTIME_H
#define VOXONE_BT_RUNTIME_H

// BT hardware, user enablement, module availability and phone connection
// are separate facts. Enablement is fixed for each boot in this stage.
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
    btEnabled_ = enabled;
    return true;
  }

  bool start() {
    started_ = true;
    return shouldStart();
  }

  bool shouldStart() const { return supportsBt_ && btEnabled_; }

  BtRuntimeStatus status(bool moduleOnline, bool phoneConnected) const {
    const bool online = shouldStart() && moduleOnline;
    return {supportsBt_, btEnabled_, online, online && phoneConnected};
  }

 private:
  bool supportsBt_;
  bool btEnabled_;
  bool started_ = false;
};

extern BtRuntime btRuntime;

#endif
