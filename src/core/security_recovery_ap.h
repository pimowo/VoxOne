#ifndef VOXONE_SECURITY_RECOVERY_AP_H
#define VOXONE_SECURITY_RECOVERY_AP_H

#include "security_recovery_state.h"

namespace voxone {
namespace security_recovery {

enum class RecoveryApStartResult : uint8_t {
  NotRequested,
  WifiFailed,
  HttpFailed,
  Active,
};

class RecoveryApSessionControl {
 public:
  virtual ~RecoveryApSessionControl() {}
  virtual RecoveryState state() const = 0;
  virtual bool live() const = 0;
  virtual RecoveryTransition activate() = 0;
  virtual RecoveryTransition fail() = 0;
  virtual RecoveryTransition complete(bool success) = 0;
};

class RecoveryApPlatform {
 public:
  virtual ~RecoveryApPlatform() {}
  virtual bool startWifi() = 0;
  virtual bool startHttp() = 0;
  virtual bool serviceHttp(uint32_t nowMs) = 0;
  virtual void stopHttp() = 0;
  virtual void disconnectClient() = 0;
  virtual void stopWifi() = 0;
  virtual void clearTemporary() = 0;
  virtual void restart() = 0;
};

class RecoveryApController {
 public:
  RecoveryApController(RecoveryApSessionControl& session,
                       RecoveryApPlatform& platform)
      : session_(session), platform_(platform) {}

  RecoveryApStartResult start(uint32_t nowMs);
  bool service(uint32_t nowMs);
  void complete(bool success);

  bool ownsRuntime() const { return ownsRuntime_; }
  bool running() const { return running_; }

 private:
  void cleanup();

  RecoveryApSessionControl& session_;
  RecoveryApPlatform& platform_;
  bool ownsRuntime_ = false;
  bool running_ = false;
  bool cleaned_ = true;
  bool restartIssued_ = false;
};

const char* recoveryApStartResultName(RecoveryApStartResult result);

}  // namespace security_recovery
}  // namespace voxone

#endif
