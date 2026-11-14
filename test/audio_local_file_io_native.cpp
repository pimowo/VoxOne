#include "../src/audioI2S/local_file_read_policy.h"

#include <cassert>

int main() {
  using voxone::audio::kLocalFileMaximumReadBytes;
  using voxone::audio::kLocalFileMinimumReadBytes;
  using voxone::audio::localFileReadRequest;

  static_assert(kLocalFileMinimumReadBytes % 512 == 0,
                "local reads must be sector aligned");
  static_assert(kLocalFileMaximumReadBytes % 512 == 0,
                "local bursts must be sector aligned");
  static_assert(kLocalFileMaximumReadBytes >= kLocalFileMinimumReadBytes,
                "invalid local read window");

  assert(localFileReadRequest(100000, 100000, 100000) == 4096);
  assert(localFileReadRequest(100000, 1024, 100000) == 1024);
  assert(localFileReadRequest(100000, 511, 100000) == 0);
  assert(localFileReadRequest(700, 700, 700) == 700);
  assert(localFileReadRequest(0, 4096, 100000) == 0);
  assert(localFileReadRequest(4096, 0, 100000) == 0);
  assert(localFileReadRequest(4096, 4096, 0) == 0);
}
