#ifndef VOXONE_BT_UPDATE_PROGRESS_H
#define VOXONE_BT_UPDATE_PROGRESS_H

#include "bt_firmware_sender.h"
#include "update_progress.h"

inline UpdatePhase btUpdatePhase(const BtFirmwareSender::Progress& progress) {
  using State = BtFirmwareSender::State;
  switch (progress.state) {
    case State::Idle: return UpdatePhase::Idle;
    case State::SendingBegin:
    case State::WaitReady: return UpdatePhase::Preparing;
    case State::SendingData:
    case State::WaitAck: return UpdatePhase::Sending;
    case State::SendingEnd:
    case State::WaitVerify:
    case State::WaitOk: return UpdatePhase::Verifying;
    case State::WaitIdentity:
      return progress.phase == BtFirmwareSender::Phase::WaitingForBt
          ? UpdatePhase::HealthCheck : UpdatePhase::Restarting;
    case State::Success: return UpdatePhase::Success;
    case State::Error: return UpdatePhase::Error;
    case State::Aborted: return UpdatePhase::Aborted;
  }
  return UpdatePhase::Error;
}

#endif
