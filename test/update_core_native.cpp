#include "../src/core/update_progress.h"
#include "../src/core/bt_update_progress.h"

#include <assert.h>
#include <string.h>

int main() {
  UpdateProgressState state;
  auto s = state.snapshot();
  assert(s.target == UpdateTarget::None && s.phase == UpdatePhase::Idle);
  assert(!s.active && !s.locked && !s.progressKnown);
  UpdateProgressState afterBoot;
  assert(afterBoot.snapshot().phase == UpdatePhase::Idle && !afterBoot.locked());

  assert(state.begin(UpdateTarget::VoxOneFirmware, UpdatePhase::Preparing, 100));
  s = state.snapshot();
  assert(s.target == UpdateTarget::VoxOneFirmware && s.active && s.locked);
  assert(!s.progressKnown);
  assert(!state.begin(UpdateTarget::Filesystem, UpdatePhase::Writing, 100));
  assert(state.snapshot().target == UpdateTarget::VoxOneFirmware);
  state.phase(UpdatePhase::Writing);
  state.progress(25, 100);
  s = state.snapshot();
  assert(s.progressKnown && s.doneBytes == 25 && s.totalBytes == 100 &&
         s.percent == 25);
  const uint32_t revision = s.revision;
  state.phase(UpdatePhase::Verifying);
  s = state.snapshot();
  assert(!s.progressKnown && s.percent == 0 && s.revision > revision);
  state.terminal(UpdatePhase::Success, 0, "finalized");
  s = state.snapshot();
  assert(s.phase == UpdatePhase::Success && !s.active && s.locked);
  assert(!state.begin(UpdateTarget::Filesystem, UpdatePhase::Writing, 100));
  state.terminal(UpdatePhase::Error, 7);  // Late callback cannot release a success lock.
  assert(state.snapshot().phase == UpdatePhase::Success && state.locked());
  state.restarting();
  assert(state.snapshot().phase == UpdatePhase::Restarting && state.locked());

  UpdateProgressState filesystem;
  assert(filesystem.begin(UpdateTarget::Filesystem, UpdatePhase::Writing, 196608));
  filesystem.progress(196608, 196608);
  filesystem.phase(UpdatePhase::Verifying);
  filesystem.terminal(UpdatePhase::Success);
  assert(filesystem.snapshot().target == UpdateTarget::Filesystem &&
         filesystem.snapshot().phase == UpdatePhase::Success && filesystem.locked());
  assert(!filesystem.begin(UpdateTarget::VoxOneFirmware, UpdatePhase::Preparing));

  UpdateProgressState next;
  assert(next.begin(UpdateTarget::Filesystem, UpdatePhase::Preparing));
  next.phase(UpdatePhase::Writing);
  next.progress(100, 0);
  s = next.snapshot();
  assert(!s.progressKnown && s.percent == 0);
  next.terminal(UpdatePhase::Error, 7, "flash failed");
  s = next.snapshot();
  assert(!s.locked && !s.active && s.phase == UpdatePhase::Error &&
         s.errorCode == 7 && strcmp(s.status, "flash failed") == 0);
  assert(next.begin(UpdateTarget::VoxOneBtFirmware, UpdatePhase::Preparing, 1024));
  s = next.snapshot();
  assert(s.errorCode == 0 && s.status[0] == '\0' && s.doneBytes == 0 &&
         s.totalBytes == 1024 && !s.progressKnown);
  next.phase(UpdatePhase::Sending);
  next.progress(512, 1024);
  assert(next.snapshot().percent == 50);
  next.terminal(UpdatePhase::Aborted);
  assert(!next.snapshot().locked && next.snapshot().phase == UpdatePhase::Aborted);
  assert(next.begin(UpdateTarget::VoxOneFirmware, UpdatePhase::Writing, 10));
  assert(next.snapshot().target == UpdateTarget::VoxOneFirmware &&
         next.snapshot().doneBytes == 0 && next.snapshot().percent == 0);

  using State = BtFirmwareSender::State;
  BtFirmwareSender::Progress bt;
  bt.state = State::SendingData;
  assert(btUpdatePhase(bt) == UpdatePhase::Sending);
  bt.state = State::WaitAck;
  assert(btUpdatePhase(bt) == UpdatePhase::Sending);
  bt.state = State::WaitVerify;
  assert(btUpdatePhase(bt) == UpdatePhase::Verifying);
  bt.state = State::WaitOk;
  assert(btUpdatePhase(bt) == UpdatePhase::Verifying);
  bt.state = State::WaitIdentity;
  bt.phase = BtFirmwareSender::Phase::RestartingBt;
  assert(btUpdatePhase(bt) == UpdatePhase::Restarting);  // FW_OK is not success.
  bt.phase = BtFirmwareSender::Phase::WaitingForBt;
  assert(btUpdatePhase(bt) == UpdatePhase::HealthCheck);  // PENDING_VERIFY.
  bt.state = State::Success;
  assert(btUpdatePhase(bt) == UpdatePhase::Success);      // Only after VALID.
  UpdateProgressState btUpdate;
  assert(btUpdate.begin(UpdateTarget::VoxOneBtFirmware, UpdatePhase::Preparing, 1024));
  btUpdate.phase(btUpdatePhase(bt));
  btUpdate.terminal(btUpdatePhase(bt));
  assert(btUpdate.snapshot().target == UpdateTarget::VoxOneBtFirmware &&
         btUpdate.snapshot().phase == UpdatePhase::Success && btUpdate.locked());
  assert(!btUpdate.begin(UpdateTarget::Filesystem, UpdatePhase::Preparing));
  bt.state = State::Error;
  assert(btUpdatePhase(bt) == UpdatePhase::Error);
  UpdateProgressState btError;
  assert(btError.begin(UpdateTarget::VoxOneBtFirmware, UpdatePhase::Sending, 1024));
  btError.terminal(btUpdatePhase(bt), 11);
  assert(btError.snapshot().phase == UpdatePhase::Error && !btError.locked());
  assert(btError.begin(UpdateTarget::Filesystem, UpdatePhase::Preparing));
  bt.state = State::Aborted;
  assert(btUpdatePhase(bt) == UpdatePhase::Aborted);
  UpdateProgressState btAborted;
  assert(btAborted.begin(UpdateTarget::VoxOneBtFirmware, UpdatePhase::Sending, 1024));
  btAborted.terminal(btUpdatePhase(bt));
  assert(btAborted.snapshot().phase == UpdatePhase::Aborted && !btAborted.locked());
  assert(btAborted.begin(UpdateTarget::VoxOneFirmware, UpdatePhase::Preparing));
}
