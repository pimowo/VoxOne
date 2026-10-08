#ifndef VOXONE_SCROLL_TEXT_STATE_H
#define VOXONE_SCROLL_TEXT_STATE_H

#include <cstddef>
#include <cstdint>
#include <cstring>

inline size_t scrollWindowCapacity(size_t displayWidth, size_t charWidth) {
  return charWidth ? displayWidth / charWidth + 2 : 2;
}

inline size_t scrollWindowPrintCapacity(size_t storageCapacity, size_t rowWidth,
                                        size_t charWidth, size_t extraChars) {
  if (!charWidth) return storageCapacity;
  const size_t requested = rowWidth / charWidth + extraChars;
  return requested < storageCapacity ? requested : storageCapacity;
}

inline bool scrollTextChangedAndResetOffset(const char* current, const char* next,
                                            int16_t& offset, int16_t origin) {
  if (std::strcmp(current, next) == 0) return false;
  offset = origin;
  return true;
}

#endif
