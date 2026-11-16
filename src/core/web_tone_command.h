#ifndef VOXONE_WEB_TONE_COMMAND_H
#define VOXONE_WEB_TONE_COMMAND_H

#include <cstdint>
#include <cstdlib>

namespace voxone {
namespace web_tone {

constexpr int8_t kMinimum = -6;
constexpr int8_t kMaximum = 6;

enum class Kind : uint8_t {
  Preset,
  Bass,
  Middle,
  Treble
};

struct Values {
  int8_t bass;
  int8_t middle;
  int8_t treble;
};

struct Command {
  Kind kind;
  Values values;
};

inline bool validValue(int value) {
  return value >= kMinimum && value <= kMaximum;
}

inline bool parseSingleValue(const char* text, int8_t& output) {
  if (!text || !*text) return false;
  const char first = *text;
  if (!((first >= '0' && first <= '9') || first == '+' || first == '-'))
    return false;
  if ((first == '+' || first == '-') && text[1] == '\0') return false;
  char* end = nullptr;
  const long parsed = std::strtol(text, &end, 10);
  if (!end || end == text || *end != '\0' || parsed < kMinimum ||
      parsed > kMaximum)
    return false;
  output = static_cast<int8_t>(parsed);
  return true;
}

inline bool valid(const Command& command) {
  switch (command.kind) {
    case Kind::Preset:
      return validValue(command.values.bass) &&
             validValue(command.values.middle) &&
             validValue(command.values.treble);
    case Kind::Bass:
      return validValue(command.values.bass);
    case Kind::Middle:
      return validValue(command.values.middle);
    case Kind::Treble:
      return validValue(command.values.treble);
  }
  return false;
}

// Resolve partial slider commands when they leave the FIFO, not in async_tcp.
// This preserves the order of rapid Bass/Middle/Treble changes even when more
// than one command is waiting for the main loop.
inline bool resolve(const Command& command, const Values& current,
                    Values& output) {
  if (!valid(command)) return false;
  output = current;
  switch (command.kind) {
    case Kind::Preset:
      output = command.values;
      break;
    case Kind::Bass:
      output.bass = command.values.bass;
      break;
    case Kind::Middle:
      output.middle = command.values.middle;
      break;
    case Kind::Treble:
      output.treble = command.values.treble;
      break;
  }
  return true;
}

}  // namespace web_tone
}  // namespace voxone

#endif
