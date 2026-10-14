#include "../src/core/display_scroll_model.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <type_traits>

namespace {
std::size_t allocationCount = 0;
}

void* operator new(std::size_t size) {
  ++allocationCount;
  if (void* memory = std::malloc(size)) return memory;
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
  ++allocationCount;
  if (void* memory = std::malloc(size)) return memory;
  throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }

using namespace voxone::display_scroll;

static void expectMotion(ScrollRendererProfile profile, ScrollSpeed speed,
                         uint8_t pixels, uint16_t intervalMs) {
  const ScrollMotion motion = scrollMotion(profile, speed);
  assert(motion.stepPixels == pixels);
  assert(motion.intervalMs == intervalMs);
}

static void expectBaseline(ScrollRendererProfile profile, uint8_t pixels,
                           uint16_t intervalMs, uint16_t pauseMs) {
  const ResolvedScrollParameters resolved = resolveScrollParameters(
      profile, ScrollSettings{false, ScrollSpeed::Fast, 0},
      ScrollSettingsSource::RendererDefaults);
  assert(resolved.supported);
  assert(resolved.enabled);
  assert(resolved.stepPixels == pixels);
  assert(resolved.intervalMs == intervalMs);
  assert(resolved.pauseMs == pauseMs);
}

int main() {
  static_assert(sizeof(ScrollSpeed) == sizeof(uint8_t),
                "ScrollSpeed must keep its one-byte representation");
  static_assert(static_cast<uint8_t>(ScrollSpeed::Slow) == 0 &&
                    static_cast<uint8_t>(ScrollSpeed::Normal) == 1 &&
                    static_cast<uint8_t>(ScrollSpeed::Fast) == 2,
                "ScrollSpeed wire candidates must remain stable");
  static_assert(sizeof(ScrollRendererProfile) == sizeof(uint8_t),
                "Scroll renderer profile must remain one byte");
  static_assert(std::is_trivially_copyable<ScrollSettings>::value,
                "ScrollSettings must not own dynamic resources");
  static_assert(std::is_standard_layout<ScrollSettings>::value,
                "ScrollSettings must remain a plain value type");
  static_assert(sizeof(ScrollSettings) <= 4,
                "ScrollSettings unexpectedly grew");
  static_assert(sizeof(ResolvedScrollParameters) <= 8,
                "Resolved scroll parameters unexpectedly grew");
  static_assert(kNominalDisplayLoopIntervalMs == 10,
                "Mappings assume the existing nominal display tick");

  const ScrollSettings defaults = defaultScrollSettings();
  assert(defaults.enabled);
  assert(defaults.speed == ScrollSpeed::Normal);
  assert(defaults.pauseMs == 5000);
  assert(isValidScrollSettings(defaults));

  assert(isValidScrollSpeed(ScrollSpeed::Slow));
  assert(isValidScrollSpeed(ScrollSpeed::Normal));
  assert(isValidScrollSpeed(ScrollSpeed::Fast));
  const ScrollSpeed unknownSpeed = static_cast<ScrollSpeed>(0xff);
  assert(!isValidScrollSpeed(unknownSpeed));
  assert(normalizeScrollSpeed(unknownSpeed) == ScrollSpeed::Normal);

  assert(isValidScrollEnabledValue(0));
  assert(isValidScrollEnabledValue(1));
  assert(!isValidScrollEnabledValue(2));
  assert(!isValidScrollEnabledValue(0xff));
  assert(!normalizeScrollEnabled(0));
  assert(normalizeScrollEnabled(1));
  assert(normalizeScrollEnabled(0xff) == kDefaultUserEnabled);

  assert(isValidScrollPause(0));
  assert(isValidScrollPause(1000));
  assert(isValidScrollPause(2000));
  assert(isValidScrollPause(3000));
  assert(isValidScrollPause(4000));
  assert(isValidScrollPause(5000));
  assert(!isValidScrollPause(1));
  assert(!isValidScrollPause(999));
  assert(!isValidScrollPause(1001));
  assert(!isValidScrollPause(2500));
  assert(!isValidScrollPause(4999));
  assert(!isValidScrollPause(5001));
  assert(!isValidScrollPause(60000));
  assert(!isValidScrollPause(0xffffffffu));
  assert(normalizeScrollPause(0) == 0);
  assert(normalizeScrollPause(2500) == kDefaultUserPauseMs);
  assert(normalizeScrollPause(5001) == kDefaultUserPauseMs);

  const ScrollSettings invalidSettings{true, unknownSpeed, 2500};
  assert(!isValidScrollSettings(invalidSettings));
  const ScrollSettings normalized = normalizeScrollSettings(invalidSettings);
  assert(normalized.enabled);
  assert(normalized.speed == ScrollSpeed::Normal);
  assert(normalized.pauseMs == 5000);

  expectMotion(ScrollRendererProfile::St7789_284x76, ScrollSpeed::Slow, 1, 18);
  expectMotion(ScrollRendererProfile::St7789_284x76, ScrollSpeed::Normal, 1, 9);
  expectMotion(ScrollRendererProfile::St7789_284x76, ScrollSpeed::Fast, 2, 9);
  expectMotion(ScrollRendererProfile::Oled128x64, ScrollSpeed::Slow, 1, 35);
  expectMotion(ScrollRendererProfile::Oled128x64, ScrollSpeed::Normal, 2, 35);
  expectMotion(ScrollRendererProfile::Oled128x64, ScrollSpeed::Fast, 4, 35);
  expectMotion(ScrollRendererProfile::St7796_480x320, ScrollSpeed::Slow, 1, 50);
  expectMotion(ScrollRendererProfile::St7796_480x320, ScrollSpeed::Normal, 2, 50);
  expectMotion(ScrollRendererProfile::St7796_480x320, ScrollSpeed::Fast, 4, 50);

  // OLED and ST7796 keep a fixed renderer cadence across all three speeds.
  assert(oled128x64ScrollMotion(ScrollSpeed::Slow).intervalMs ==
         oled128x64ScrollMotion(ScrollSpeed::Fast).intervalMs);
  assert(st7796_480x320ScrollMotion(ScrollSpeed::Slow).intervalMs ==
         st7796_480x320ScrollMotion(ScrollSpeed::Fast).intervalMs);
  // ST7789 Fast intentionally retains Normal's 9 ms interval; speed is
  // increased by the step size and no extra render call is requested.
  assert(st7789_284x76ScrollMotion(ScrollSpeed::Normal).intervalMs ==
         st7789_284x76ScrollMotion(ScrollSpeed::Fast).intervalMs);

  expectBaseline(ScrollRendererProfile::St7789_284x76, 1, 9, 5000);
  expectBaseline(ScrollRendererProfile::Oled128x64, 2, 35, 5000);
  expectBaseline(ScrollRendererProfile::St7796_480x320, 2, 50, 2500);

  const ResolvedScrollParameters pauseZero = resolveScrollParameters(
      ScrollRendererProfile::St7796_480x320,
      ScrollSettings{true, ScrollSpeed::Fast, 0},
      ScrollSettingsSource::UserSettings);
  assert(pauseZero.supported);
  assert(pauseZero.enabled);
  assert(pauseZero.stepPixels == 4);
  assert(pauseZero.intervalMs == 50);
  assert(pauseZero.pauseMs == 0);

  // Disabling animation leaves the renderer supported, but requests static
  // clipping: no timing, pixel step or intentional pause is active.
  const ResolvedScrollParameters disabled = resolveScrollParameters(
      ScrollRendererProfile::St7789_284x76,
      ScrollSettings{false, ScrollSpeed::Fast, 5000},
      ScrollSettingsSource::UserSettings);
  assert(disabled.supported);
  assert(!disabled.enabled);
  assert(disabled.stepPixels == 0);
  assert(disabled.intervalMs == 0);
  assert(disabled.pauseMs == 0);

  const ResolvedScrollParameters invalidUserSettings =
      resolveScrollParameters(ScrollRendererProfile::St7789_284x76,
                              invalidSettings,
                              ScrollSettingsSource::UserSettings);
  assert(invalidUserSettings.supported);
  assert(invalidUserSettings.enabled);
  assert(invalidUserSettings.stepPixels == 1);
  assert(invalidUserSettings.intervalMs == 9);
  assert(invalidUserSettings.pauseMs == 5000);

  const ResolvedScrollParameters none = resolveScrollParameters(
      ScrollRendererProfile::None, defaults,
      ScrollSettingsSource::UserSettings);
  assert(!none.supported);
  assert(!none.enabled);
  assert(none.stepPixels == 0 && none.intervalMs == 0 && none.pauseMs == 0);

  const ScrollRendererProfile unknownProfile =
      static_cast<ScrollRendererProfile>(0xff);
  assert(!isKnownScrollRendererProfile(unknownProfile));
  const ResolvedScrollParameters unknown = resolveScrollParameters(
      unknownProfile, defaults, ScrollSettingsSource::UserSettings);
  assert(!unknown.supported);
  assert(!unknown.enabled);
  assert(unknown.stepPixels == 0 && unknown.intervalMs == 0 &&
         unknown.pauseMs == 0);

  const ScrollSettingsSource unknownSource =
      static_cast<ScrollSettingsSource>(0xff);
  assert(!isValidScrollSettingsSource(unknownSource));
  const ResolvedScrollParameters safeSourceFallback = resolveScrollParameters(
      ScrollRendererProfile::St7796_480x320,
      ScrollSettings{false, ScrollSpeed::Fast, 0}, unknownSource);
  assert(safeSourceFallback.supported);
  assert(safeSourceFallback.enabled);
  assert(safeSourceFallback.stepPixels == 2);
  assert(safeSourceFallback.intervalMs == 50);
  assert(safeSourceFallback.pauseMs == 2500);

  const std::size_t allocationsBefore = allocationCount;
  for (uint8_t profile = 0; profile <= 3; ++profile) {
    for (uint8_t speed = 0; speed <= 2; ++speed) {
      const ScrollSettings settings{true, static_cast<ScrollSpeed>(speed), 0};
      (void)resolveScrollParameters(
          static_cast<ScrollRendererProfile>(profile), settings,
          ScrollSettingsSource::UserSettings);
    }
  }
  assert(allocationCount == allocationsBefore);

  return 0;
}
