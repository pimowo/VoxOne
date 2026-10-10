#include "../src/core/volume_map.h"
#include "../src/core/mute_state.h"
#include <cassert>
#include <cstdint>
#include <initializer_list>

int main() {
  MuteState encoderMute;
  uint8_t encoderUser = 0;
  const auto turn = [&](int8_t delta) {
    encoderUser = encoderMute.stepUserVolume(encoderUser, delta);
    const VolumeState state = volumeStateFromUser(encoderUser, 100);
    assert(state.user == encoderUser);
    assert(state.raw == volumeUserToRaw(encoderUser));
    assert(state.raw <= volumeRawMaximum(100));
  };
  turn(-1); assert(encoderUser == 0);
  turn(1); assert(encoderUser == 1);
  encoderUser = 99;
  turn(1); assert(encoderUser == 100);
  turn(1); assert(encoderUser == 100);
  encoderUser = 1;
  turn(-1); assert(encoderUser == 0);
  turn(-1); assert(encoderUser == 0);
  encoderUser = 49;
  turn(1); assert(encoderUser == 50);
  turn(1); assert(encoderUser == 51);
  turn(-1); assert(encoderUser == 50);
  turn(-1); assert(encoderUser == 49);
  // RADIO uses the sign of a batched encoder delta, as on A0.
  encoderUser = 50;
  turn(3); assert(encoderUser == 51);
  assert(volumeStateFromUser(50, 100).raw == 103);
  assert(volumeStateFromUser(51, 100).raw == 106);
  assert(volumeStateFromUser(99, 100).raw == 251);
  assert(volumeStateFromUser(100, 100).raw == 254);
  assert(volumeStateFromUser(101, 100).user == 100);
  assert(volumeStateFromUser(255, 100).user == 100);

  for (int user = 0; user <= 100; ++user) {
    assert(volumeUserToRaw(user, 100) == volumeUserToRaw(user));
  }
  const uint8_t users[] = {0, 1, 25, 50, 75, 99, 100};
  uint8_t previous = 0;
  for (uint8_t user : users) {
    const VolumeState state = volumeStateFromUser(user, 60);
    assert(state.user == user);
    assert(state.raw >= previous && state.raw <= volumeRawMaximum(60));
    previous = state.raw;
  }
  for (int raw = 0; raw <= 254; ++raw)
    assert(volumeStateFromRaw(static_cast<uint8_t>(raw), 100).user <= 100);
  for (uint8_t raw : {uint8_t{0}, uint8_t{1}, uint8_t{64}, uint8_t{127},
                      uint8_t{200}, uint8_t{253}, uint8_t{254}}) {
    const VolumeState state = volumeStateFromRaw(raw, 100);
    assert(state.user <= 100);
  }
  assert(volumeRawMaximum(60) == 131);
  assert(volumeStateFromUser(0, 60).raw == 0);
  assert(volumeStateFromUser(1, 60).raw == 1);
  assert(volumeStateFromUser(25, 60).raw == 22);
  assert(volumeStateFromUser(50, 60).raw == 53);
  assert(volumeStateFromUser(75, 60).raw == 90);
  assert(volumeStateFromUser(100, 60).raw == 131);
  assert(volumeStateFromUser(50, 60).raw == volumeUserToRaw(30));

  bool duplicateRaw = false;
  previous = 0;
  for (int user = 0; user <= 100; ++user) {
    const VolumeState state = volumeStateFromUser(user, 20);
    assert(state.user == user);
    assert(state.raw >= previous);
    if (user > 0 && state.raw == previous) duplicateRaw = true;
    previous = state.raw;
  }
  assert(duplicateRaw);
  const VolumeState legacy = volumeStateFromRaw(200, 60);
  assert(legacy.raw == 131 && legacy.user == 100);
  const VolumeState down = volumeStateAfterMaximum(volumeStateFromUser(50, 100), 100, 60);
  assert(down.user == 50 && down.raw < volumeUserToRaw(50));
  const VolumeState up = volumeStateAfterMaximum(down, 60, 100);
  assert(up.raw == down.raw);
  assert(up.user == volumeRawToUser(down.raw, 100));

  const VolumeState last = volumeStateAtStartup(80, 42, 60, false, 20, false);
  assert(last.user == 42 && last.raw == volumeUserToRaw(42, 60));
  const VolumeState fixed = volumeStateAtStartup(200, 42, 60, true, 25, false);
  assert(fixed.user == 25 && fixed.raw == volumeUserToRaw(25, 60));
  const VolumeState migration = volumeStateAtStartup(12, volumeRawToUser(12), 100, false, 20, true);
  assert(migration.raw == 12 && migration.user == 9);
  assert(volumeClampOutput(200, 60) == 131);
  assert(volumeClampOutput(-1, 60) == 0);
}
