#include "../src/core/audio_path_config.h"

#include <cassert>
#include <cstring>

int main() {
  static_assert(AUDIO_PCM5102 == 0, "PCM5102 must remain the migration default");
  static_assert(AUDIO_DSPMINI == 1, "DSPmini persisted identity changed");
  static_assert(voxone::audio_path::kOffset +
                    voxone::audio_path::kEncodedBytes <=
                voxone::ha_name::kStorageBytes,
                "Config v7 weather extension overflow");

  uint8_t storage[voxone::ha_name::kStorageBytes]{};
  AudioPath path = AUDIO_DSPMINI;
  assert(!voxone::audio_path::decode(storage, sizeof(storage), path));
  assert(path == AUDIO_PCM5102);

  assert(voxone::audio_path::encode(
      AUDIO_DSPMINI, storage, sizeof(storage)));
  assert(voxone::audio_path::decode(storage, sizeof(storage), path));
  assert(path == AUDIO_DSPMINI);
  assert(std::strcmp(voxone::audio_path::apiName(path), "dspmini") == 0);

  storage[voxone::audio_path::kOffset + 4] = 0xff;
  assert(!voxone::audio_path::decode(storage, sizeof(storage), path));
  assert(path == AUDIO_PCM5102);
  assert(voxone::audio_path::parseApiName("pcm5102", path));
  assert(path == AUDIO_PCM5102);
  assert(!voxone::audio_path::parseApiName("invalid", path));
  return 0;
}
