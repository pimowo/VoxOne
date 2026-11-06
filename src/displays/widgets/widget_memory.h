#ifndef VOXONE_WIDGET_MEMORY_H
#define VOXONE_WIDGET_MEMORY_H

#include <stddef.h>
#include <stdlib.h>

namespace voxone {
namespace display {

class WidgetBuffer {
 public:
  using Allocate = void* (*)(size_t, size_t);
  using Release = void (*)(void*);

  WidgetBuffer() = default;
  WidgetBuffer(const WidgetBuffer&) = delete;
  WidgetBuffer& operator=(const WidgetBuffer&) = delete;
  ~WidgetBuffer() { reset(); }

  bool allocate(size_t size, Allocate allocator = &calloc,
                Release releaser = &free) {
    reset();
    if (size == 0 || !allocator || !releaser) return false;
    data_ = static_cast<char*>(allocator(size, 1));
    if (!data_) return false;
    size_ = size;
    releaser_ = releaser;
    return true;
  }

  void reset() {
    if (data_ && releaser_) releaser_(data_);
    data_ = nullptr;
    size_ = 0;
    releaser_ = nullptr;
  }

  char* data() const { return data_; }
  size_t size() const { return size_; }

 private:
  char* data_ = nullptr;
  size_t size_ = 0;
  Release releaser_ = nullptr;
};

template <typename FrameBuffer>
inline bool frameBufferReady(FrameBuffer* frameBuffer) {
  return frameBuffer != nullptr && frameBuffer->ready();
}

}  // namespace display
}  // namespace voxone

#endif
