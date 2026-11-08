#include "../src/core/audio_buffer_metrics.h"

#include <cassert>
#include <cstdio>

int main() {
  voxone::audio::AudioBufferMetrics metrics;

  metrics.observe(65535, 65535);
  metrics.observe(12000, 65535);
  metrics.observe(8000, 65535);   // same starvation episode
  metrics.observe(70000, 65535);  // recovery
  metrics.observe(0, 65535);      // second starvation episode

  auto snapshot = metrics.snapshot();
  assert(snapshot.observed);
  assert(snapshot.underflows == 2);
  assert(snapshot.minimumFillBytes == 0);

  metrics.reset();
  metrics.observe(512, 65535, true);  // valid final partial web-file frame
  snapshot = metrics.snapshot();
  assert(snapshot.underflows == 0);
  assert(snapshot.minimumFillBytes == 512);

  std::puts("PASS audio_buffer_metrics_native");
}
