#pragma once

#include <stdio.h>

namespace voxone {
namespace serialcli {

inline bool fullyConsumed(const char* input, int consumed) {
  return input != nullptr && consumed >= 0 && input[consumed] == '\0';
}

inline bool matchInt(const char* input, const char* pattern, int& value) {
  int consumed = -1;
  return sscanf(input, pattern, &value, &consumed) == 1 &&
         fullyConsumed(input, consumed);
}

inline bool matchTwoInts(const char* input, const char* pattern,
                         int& first, int& second) {
  int consumed = -1;
  return sscanf(input, pattern, &first, &second, &consumed) == 2 &&
         fullyConsumed(input, consumed);
}

inline bool matchTwoStrings(const char* input, const char* pattern,
                            char* first, char* second) {
  int consumed = -1;
  return sscanf(input, pattern, first, second, &consumed) == 2 &&
         fullyConsumed(input, consumed);
}

}  // namespace serialcli
}  // namespace voxone
