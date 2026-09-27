#include "../src/core/volume_map.h"
#include <cassert>
#include <cstdint>

int main() {
  for (int user = 0; user <= 100; ++user) {
    assert(volumeUserToRaw(user, 100) == volumeUserToRaw(user));
  }
  const uint8_t users[] = {0, 1, 25, 50, 75, 100};
  uint8_t previous = 0;
  for (uint8_t user : users) {
    const VolumeState state = volumeStateFromUser(user, 60);
    assert(state.user == user);
    assert(state.raw >= previous && state.raw <= volumeRawMaximum(60));
    previous = state.raw;
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
