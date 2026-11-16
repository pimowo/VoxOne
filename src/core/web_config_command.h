#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace voxone { namespace web_config {

// Owned, bounded values only. No callback/client/request lifetime crosses tasks.
struct Command {
  char name[24];
  char value[12];
};

enum class Parse { Other, Invalid, Ok };

inline Parse parse(const char* name, const char* value, Command& out) {
  struct Rule { const char* name; long low; long high; bool clamp; };
  static const Rule rules[] = {
      {"balance", -16, 16, false}, {"startupmode", 0, 1, false},
      {"startupfixedvolume", 0, 100, false}, {"dim", 0, 100, true},
      {"encoderuireversed", 0, 1, false},
      {"stationlisttimeout", 0, 120, false},
      {"bttransporttimeout", 0, 120, false}, {"flipscreen", 0, 1, true},
      {"timeint", 1, 10080, false}, {"timeintrtc", 1, 1000, false},
      {"hadiscovery", 0, 1, true}, {"lineoutenabled", 0, 1, false},
      {"lineoutvolume", 0, 100, false}, {"vumeter", 0, 1, false},
      {"maximumvolume", 1, 100, false},
      // These do not commit, but their setters acquire Config's mutex. Keep
      // them out of async_tcp too, without changing the volume save timer.
      {"vol100", 0, 100, false}, {"mute", 0, 1, false}};
  if (!name) return Parse::Other;
  for (const auto& rule : rules) {
    if (std::strcmp(name, rule.name)) continue;
    if (!value || !*value) return Parse::Invalid;
    const bool strictBoolean = !std::strcmp(name, "encoderuireversed") ||
        !std::strcmp(name, "lineoutenabled") || !std::strcmp(name, "vumeter") ||
        !std::strcmp(name, "mute");
    if (strictBoolean && std::strcmp(value, "0") && std::strcmp(value, "1"))
      return Parse::Invalid;
    if ((!std::strcmp(name, "stationlisttimeout") ||
         !std::strcmp(name, "bttransporttimeout")) &&
        (*value < '0' || *value > '9')) return Parse::Invalid;
    const char* digit = value;
    if (*digit == '+' || *digit == '-') ++digit;
    if (*digit < '0' || *digit > '9') return Parse::Invalid;
    errno = 0;
    char* end = nullptr;
    long number = std::strtol(value, &end, 10);
    if (errno == ERANGE || !end || *end) return Parse::Invalid;
    if (!std::strcmp(name, "flipscreen") || !std::strcmp(name, "hadiscovery"))
      number = number != 0;
    if (rule.clamp) {
      if (number < rule.low) number = rule.low;
      if (number > rule.high) number = rule.high;
    }
    if (number < rule.low || number > rule.high) return Parse::Invalid;
    std::snprintf(out.name, sizeof(out.name), "%s", name);
    std::snprintf(out.value, sizeof(out.value), "%ld", number);
    return Parse::Ok;
  }
  return Parse::Other;
}

inline bool durable(const Command& command) {
  return std::strcmp(command.name, "vol100") &&
         std::strcmp(command.name, "mute");
}

inline bool immediate(const char* name) {
  static const char* const commands[] = {
      "source", "webtransport", "start", "stop", "playurl", "ha_play",
      "toggle", "prev", "next", "playstation", "getindex", "getrssi",
      "getsystem", "getwebstatus", "getscreen", "reboot"};
  for (const char* command : commands)
    if (!std::strcmp(name, command)) return true;
  return false;
}

// At most eight outstanding commands, so modular ordering is unambiguous.
inline bool before(uint32_t left, uint32_t right) {
  return left != right && static_cast<uint32_t>(right - left) < 0x80000000U;
}

enum class Authorization { Authorized, Busy, Denied };
// Shared production execution boundary. Busy retains the head, denial consumes
// it without mutation, and no success is emitted before apply returns.
template<class Consume, class Apply, class Report>
bool execute(Authorization authorization, Consume consume, Apply apply,
             Report report) {
  if (authorization == Authorization::Busy) return false;
  consume();
  if (authorization == Authorization::Denied) {
    report(false, false);
    return true;
  }
  const bool success = apply();
  report(true, success);
  return true;
}

} }  // namespace voxone::web_config
