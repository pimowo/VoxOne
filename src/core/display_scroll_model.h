#ifndef VOXONE_DISPLAY_SCROLL_MODEL_H
#define VOXONE_DISPLAY_SCROLL_MODEL_H

#include <stdint.h>

namespace voxone {
namespace display_scroll {

// User-facing values. Keep their numeric representation stable for the future
// persistence and wire adapters, but do not persist this in-memory structure.
enum class ScrollSpeed : uint8_t {
  Slow = 0,
  Normal = 1,
  Fast = 2
};

// These are presentation layouts, not hardware boards or controller drivers.
// SSD1306 and SSD1309 intentionally share Oled128x64.
enum class ScrollRendererProfile : uint8_t {
  None = 0,
  Oled128x64 = 1,
  St7789_284x76 = 2,
  St7796_480x320 = 3
};

// RendererDefaults preserves the existing renderer-specific behavior until a
// later integration explicitly activates the common user setting.
enum class ScrollSettingsSource : uint8_t {
  RendererDefaults = 0,
  UserSettings = 1
};

struct ScrollSettings {
  bool enabled;
  ScrollSpeed speed;
  // A future engine applies this delay before animation. Zero means no
  // intentional pause; it never means catch-up rendering.
  uint16_t pauseMs;
};

struct ScrollMotion {
  uint8_t stepPixels;
  uint16_t intervalMs;
};

struct ScrollRendererDefaults {
  bool supported;
  bool enabled;
  ScrollSpeed speed;
  uint16_t pauseMs;
};

struct ResolvedScrollParameters {
  uint16_t intervalMs;
  uint16_t pauseMs;
  uint8_t stepPixels;
  bool supported;
  bool enabled;
};

constexpr uint16_t kNominalDisplayLoopIntervalMs = 10;
constexpr uint16_t kMinimumPauseMs = 0;
constexpr uint16_t kMaximumPauseMs = 5000;
constexpr uint16_t kPauseStepMs = 1000;
constexpr bool kDefaultUserEnabled = true;
constexpr uint16_t kDefaultUserPauseMs = 5000;
constexpr ScrollSpeed kDefaultUserSpeed = ScrollSpeed::Normal;

constexpr ScrollSettings defaultScrollSettings() {
  return ScrollSettings{kDefaultUserEnabled, kDefaultUserSpeed,
                        kDefaultUserPauseMs};
}

// Raw adapters can validate future persisted or wire values before converting
// them to bool. The model itself deliberately does not define such an adapter.
constexpr bool isValidScrollEnabledValue(uint8_t enabled) {
  return enabled == 0 || enabled == 1;
}

constexpr bool normalizeScrollEnabled(uint8_t enabled) {
  return isValidScrollEnabledValue(enabled) ? enabled != 0
                                            : kDefaultUserEnabled;
}

constexpr bool isValidScrollSpeed(ScrollSpeed speed) {
  return speed == ScrollSpeed::Slow || speed == ScrollSpeed::Normal ||
         speed == ScrollSpeed::Fast;
}

constexpr bool isValidScrollPause(uint32_t pauseMs) {
  return pauseMs <= kMaximumPauseMs && pauseMs % kPauseStepMs == 0;
}

constexpr bool isValidScrollSettings(const ScrollSettings& settings) {
  return isValidScrollSpeed(settings.speed) &&
         isValidScrollPause(settings.pauseMs);
}

constexpr ScrollSpeed normalizeScrollSpeed(ScrollSpeed speed) {
  return isValidScrollSpeed(speed) ? speed : kDefaultUserSpeed;
}

constexpr uint16_t normalizeScrollPause(uint32_t pauseMs) {
  return isValidScrollPause(pauseMs) ? static_cast<uint16_t>(pauseMs)
                                     : kDefaultUserPauseMs;
}

constexpr ScrollSettings normalizeScrollSettings(
    const ScrollSettings& settings) {
  return ScrollSettings{settings.enabled, normalizeScrollSpeed(settings.speed),
                        normalizeScrollPause(settings.pauseMs)};
}

constexpr bool isKnownScrollRendererProfile(ScrollRendererProfile profile) {
  return profile == ScrollRendererProfile::None ||
         profile == ScrollRendererProfile::Oled128x64 ||
         profile == ScrollRendererProfile::St7789_284x76 ||
         profile == ScrollRendererProfile::St7796_480x320;
}

constexpr bool isValidScrollSettingsSource(ScrollSettingsSource source) {
  return source == ScrollSettingsSource::RendererDefaults ||
         source == ScrollSettingsSource::UserSettings;
}

constexpr ScrollRendererDefaults rendererScrollDefaults(
    ScrollRendererProfile profile) {
  return profile == ScrollRendererProfile::Oled128x64
             ? ScrollRendererDefaults{true, true, ScrollSpeed::Normal, 5000}
         : profile == ScrollRendererProfile::St7789_284x76
             ? ScrollRendererDefaults{true, true, ScrollSpeed::Normal, 5000}
         : profile == ScrollRendererProfile::St7796_480x320
             ? ScrollRendererDefaults{true, true, ScrollSpeed::Normal, 2500}
             : ScrollRendererDefaults{false, false, ScrollSpeed::Normal, 0};
}

constexpr ScrollMotion oled128x64ScrollMotion(ScrollSpeed speed) {
  return normalizeScrollSpeed(speed) == ScrollSpeed::Slow
             ? ScrollMotion{1, 35}
         : normalizeScrollSpeed(speed) == ScrollSpeed::Fast
             ? ScrollMotion{4, 35}
             : ScrollMotion{2, 35};
}

constexpr ScrollMotion st7789_284x76ScrollMotion(ScrollSpeed speed) {
  return normalizeScrollSpeed(speed) == ScrollSpeed::Slow
             ? ScrollMotion{1, 18}
         : normalizeScrollSpeed(speed) == ScrollSpeed::Fast
             ? ScrollMotion{2, 9}
             : ScrollMotion{1, 9};
}

constexpr ScrollMotion st7796_480x320ScrollMotion(ScrollSpeed speed) {
  return normalizeScrollSpeed(speed) == ScrollSpeed::Slow
             ? ScrollMotion{1, 50}
         : normalizeScrollSpeed(speed) == ScrollSpeed::Fast
             ? ScrollMotion{4, 50}
             : ScrollMotion{2, 50};
}

constexpr ScrollMotion scrollMotion(ScrollRendererProfile profile,
                                    ScrollSpeed speed) {
  return profile == ScrollRendererProfile::Oled128x64
             ? oled128x64ScrollMotion(speed)
         : profile == ScrollRendererProfile::St7789_284x76
             ? st7789_284x76ScrollMotion(speed)
         : profile == ScrollRendererProfile::St7796_480x320
             ? st7796_480x320ScrollMotion(speed)
             : ScrollMotion{0, 0};
}

// Row order and row state intentionally do not belong to this value model.
// A future fixed-capacity engine can select only the rows present in a layout,
// while sharing these settings without assuming a three-row display.

inline ResolvedScrollParameters resolveScrollParameters(
    ScrollRendererProfile profile, const ScrollSettings& userSettings,
    ScrollSettingsSource source) {
  const ScrollRendererDefaults defaults = rendererScrollDefaults(profile);
  if (!defaults.supported)
    return ResolvedScrollParameters{0, 0, 0, false, false};

  const bool useUserSettings =
      source == ScrollSettingsSource::UserSettings;
  const ScrollSettings normalized = normalizeScrollSettings(userSettings);
  const bool enabled = useUserSettings ? normalized.enabled : defaults.enabled;
  if (!enabled)
    return ResolvedScrollParameters{0, 0, 0, true, false};

  const ScrollSpeed speed = useUserSettings ? normalized.speed : defaults.speed;
  const uint16_t pauseMs =
      useUserSettings ? normalized.pauseMs : defaults.pauseMs;
  const ScrollMotion motion = scrollMotion(profile, speed);
  return ResolvedScrollParameters{motion.intervalMs, pauseMs,
                                  motion.stepPixels, true, true};
}

}  // namespace display_scroll
}  // namespace voxone

#endif
