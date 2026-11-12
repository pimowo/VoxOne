#pragma once

#include <cstddef>

namespace voxone::audio_ring {

struct Space {
  std::size_t freeBytes;
  std::size_t contiguousWriteBytes;
};

// The ring always reserves one byte so that equal pointers can represent an
// empty buffer.  When empty is false, equal pointers represent a saturated
// ring and must never be evaluated as 0 - 1 with an unsigned type.
constexpr Space space(std::size_t capacity, std::size_t read,
                      std::size_t write, bool empty) {
  return capacity < 2 || read >= capacity || write >= capacity
             ? Space{0, 0}
             : empty
                   ? Space{capacity - 1,
                           write == 0 ? capacity - 1 : capacity - write}
                   : read == write
                         ? Space{0, 0}
                         : read > write
                               ? Space{read - write - 1,
                                       read - write - 1}
                               : Space{capacity - write + read - 1,
                                       read == 0 ? capacity - write - 1
                                                 : capacity - write};
}

constexpr std::size_t advance(std::size_t capacity, std::size_t position,
                              std::size_t bytes) {
  return capacity == 0 ? 0 : (position + bytes) % capacity;
}

}  // namespace voxone::audio_ring
