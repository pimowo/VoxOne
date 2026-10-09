#ifndef VOXONE_BT_OTA_STATE_H
#define VOXONE_BT_OTA_STATE_H

#include <stdint.h>

enum class BtOtaState : uint8_t {
  Missing, NotPending, PendingVerify, Valid, ConfirmFailed, Unknown
};

inline const char* btOtaStateName(BtOtaState state) {
  switch (state) {
    case BtOtaState::Missing: return "MISSING";
    case BtOtaState::NotPending: return "NOT_PENDING";
    case BtOtaState::PendingVerify: return "PENDING_VERIFY";
    case BtOtaState::Valid: return "VALID";
    case BtOtaState::ConfirmFailed: return "CONFIRM_FAILED";
    case BtOtaState::Unknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

#endif
