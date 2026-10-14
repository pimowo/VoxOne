#ifndef VOXONE_SCROLL_TEXT_STATE_H
#define VOXONE_SCROLL_TEXT_STATE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

inline size_t scrollWindowCapacity(size_t displayWidth, size_t charWidth) {
  if (!charWidth) return 2;
  const size_t visibleChars = displayWidth / charWidth;
  return visibleChars > std::numeric_limits<size_t>::max() - 2
      ? std::numeric_limits<size_t>::max()
      : visibleChars + 2;
}

inline size_t scrollWindowPrintCapacity(size_t storageCapacity, size_t rowWidth,
                                        size_t charWidth, size_t extraChars) {
  if (!storageCapacity) return 0;
  if (!charWidth) return 1;
  const size_t visibleChars = rowWidth / charWidth;
  const size_t requested =
      visibleChars > std::numeric_limits<size_t>::max() - extraChars
          ? storageCapacity
          : visibleChars + extraChars;
  if (!requested) return 1;
  return requested < storageCapacity ? requested : storageCapacity;
}

inline bool scrollTextChangedAndResetOffset(const char* current, const char* next,
                                            int16_t& offset, int16_t origin) {
  if (std::strcmp(current, next) == 0) return false;
  offset = origin;
  return true;
}

inline size_t scrollBoundedLength(const char* text, size_t limit) {
  if (!text) return 0;
  size_t length = 0;
  while (length < limit && text[length]) ++length;
  return length;
}

// Assigns the exact text that fits in the widget storage. Comparing the
// truncated representation prevents repeated oversized metadata snapshots
// from restarting an unchanged scroll cycle.
inline bool scrollTextAssign(char* destination, size_t capacity,
                             const char* source, int16_t& offset,
                             int16_t origin) {
  if (!destination || !capacity) return false;
  if (!source) source = "";
  const size_t sourceLength = scrollBoundedLength(source, capacity - 1);
  const size_t destinationLength = scrollBoundedLength(destination, capacity);
  const bool changed = destinationLength != sourceLength ||
      std::memcmp(destination, source, sourceLength) != 0;
  if (!changed) return false;
  if (sourceLength) std::memcpy(destination, source, sourceLength);
  destination[sourceLength] = '\0';
  offset = origin;
  return true;
}

inline uint16_t scrollTextPixelWidth(size_t glyphCount, size_t charWidth) {
  if (!charWidth || !glyphCount) return 0;
  constexpr size_t maximum = std::numeric_limits<uint16_t>::max();
  if (glyphCount > maximum / charWidth) return static_cast<uint16_t>(maximum);
  return static_cast<uint16_t>(glyphCount * charWidth);
}

inline uint16_t scrollCyclePixelWidth(uint16_t textWidth,
                                      uint16_t separatorWidth) {
  constexpr uint32_t maximum = std::numeric_limits<uint16_t>::max();
  const uint32_t total = static_cast<uint32_t>(textWidth) + separatorWidth;
  return static_cast<uint16_t>(total > maximum ? maximum : total);
}

// Converts a CORE-owned absolute offset into the signed cursor coordinate
// used by the existing rasterizer. Invalid/out-of-range input falls back to
// the left edge instead of wrapping a signed coordinate.
inline bool scrollAbsoluteCoordinate(int16_t origin, uint16_t offset,
                                     uint16_t cyclePixels,
                                     int16_t& coordinate) {
  if (cyclePixels == 0 || offset >= cyclePixels) {
    coordinate = origin;
    return offset == 0;
  }
  const int32_t candidate = static_cast<int32_t>(origin) - offset;
  if (candidate < std::numeric_limits<int16_t>::min() ||
      candidate > std::numeric_limits<int16_t>::max()) {
    coordinate = origin;
    return false;
  }
  coordinate = static_cast<int16_t>(candidate);
  return true;
}

inline bool scrollTextNeedsScrolling(uint16_t textWidth, uint16_t rowWidth,
                                     uint16_t charWidth, uint16_t rowHeight) {
  return rowWidth && charWidth && rowHeight && textWidth > rowWidth;
}

inline size_t scrollAppend(char* destination, size_t capacity, size_t used,
                           const char* source) {
  if (!destination || !capacity || !source) return used;
  if (used >= capacity) {
    destination[capacity - 1] = '\0';
    return capacity - 1;
  }
  while (*source && used + 1 < capacity)
    destination[used++] = *source++;
  destination[used] = '\0';
  return used;
}

// Builds the visible repeated sequence without ever manufacturing a pointer
// beyond the text array while the cursor crosses the separator.
inline size_t scrollComposeWindow(char* destination, size_t capacity,
                                  const char* text, const char* separator,
                                  size_t hiddenGlyphs) {
  if (!destination || !capacity) return 0;
  destination[0] = '\0';
  if (!text) text = "";
  if (!separator) separator = "";
  const size_t textLength = std::strlen(text);
  const size_t separatorLength = std::strlen(separator);
  const size_t cycleLength = textLength + separatorLength;
  if (hiddenGlyphs > cycleLength) hiddenGlyphs = cycleLength;

  size_t used = 0;
  if (hiddenGlyphs < textLength) {
    used = scrollAppend(destination, capacity, used, text + hiddenGlyphs);
    used = scrollAppend(destination, capacity, used, separator);
  } else {
    const size_t separatorOffset = hiddenGlyphs - textLength;
    used = scrollAppend(destination, capacity, used,
                        separator + (separatorOffset < separatorLength
                                         ? separatorOffset
                                         : separatorLength));
  }
  return scrollAppend(destination, capacity, used, text);
}

#endif
