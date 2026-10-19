#pragma once

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace serialcli {

constexpr size_t kMaxFrameLength = 127;
constexpr uint32_t kFrameTimeoutMs = 1000;
constexpr uint16_t kMaxBytesPerLoop = 64;
constexpr uint32_t kCommandErrorLogIntervalMs = 1000;

enum class InputEvent : uint8_t {
  None,
  FrameReady,
  FrameRejected,
  Disabled,
};

template <bool RxEnabled>
class InputFramer;

template <>
class InputFramer<true> {
 public:
  InputFramer()
      : length_(0), lastByteAtMs_(0), state_(State::Idle) {
    buffer_[0] = '\0';
  }

  InputEvent feed(uint8_t byte, uint32_t nowMs) {
    expire(nowMs);
    if (byte == '\r' || byte == '\n') return finishFrame();
    if (state_ == State::Discarding) return InputEvent::None;
    if (byte < 0x20 || byte > 0x7e) return rejectFrame();
    if (state_ == State::Idle) {
      length_ = 0;
      buffer_[0] = '\0';
    }
    if (length_ == kMaxFrameLength) return rejectFrame();
    buffer_[length_++] = static_cast<char>(byte);
    buffer_[length_] = '\0';
    lastByteAtMs_ = nowMs;
    state_ = State::Collecting;
    return InputEvent::None;
  }

  InputEvent tick(uint32_t nowMs) {
    return expire(nowMs) ? InputEvent::FrameRejected : InputEvent::None;
  }

  const char* frame() const { return buffer_; }
  size_t frameLength() const { return length_; }
  bool discarding() const { return state_ == State::Discarding; }
  static constexpr bool rxEnabled() { return true; }

 private:
  enum class State : uint8_t { Idle, Collecting, Discarding };

  bool expire(uint32_t nowMs) {
    if (state_ != State::Collecting ||
        static_cast<uint32_t>(nowMs - lastByteAtMs_) < kFrameTimeoutMs)
      return false;
    length_ = 0;
    buffer_[0] = '\0';
    state_ = State::Discarding;
    return true;
  }

  InputEvent rejectFrame() {
    length_ = 0;
    buffer_[0] = '\0';
    state_ = State::Discarding;
    return InputEvent::FrameRejected;
  }

  InputEvent finishFrame() {
    if (state_ == State::Discarding) {
      state_ = State::Idle;
      return InputEvent::None;
    }
    if (state_ != State::Collecting) return InputEvent::None;

    size_t first = 0;
    while (first < length_ && buffer_[first] == ' ') ++first;
    size_t end = length_;
    while (end > first && buffer_[end - 1] == ' ') --end;
    const size_t trimmedLength = end - first;
    if (first != 0 && trimmedLength != 0) {
      for (size_t index = 0; index < trimmedLength; ++index)
        buffer_[index] = buffer_[first + index];
    }
    length_ = trimmedLength;
    buffer_[length_] = '\0';
    state_ = State::Idle;
    return length_ == 0 ? InputEvent::None : InputEvent::FrameReady;
  }

  char buffer_[kMaxFrameLength + 1];
  size_t length_;
  uint32_t lastByteAtMs_;
  State state_;
};

template <>
class InputFramer<false> {
 public:
  InputEvent feed(uint8_t, uint32_t) { return InputEvent::Disabled; }
  InputEvent tick(uint32_t) { return InputEvent::Disabled; }
  const char* frame() const { return ""; }
  size_t frameLength() const { return 0; }
  bool discarding() const { return false; }
  static constexpr bool rxEnabled() { return false; }
};

class CommandErrorLimiter {
 public:
  CommandErrorLimiter() : lastLogAtMs_(0), logged_(false) {}

  bool allow(uint32_t nowMs) {
    if (logged_ &&
        static_cast<uint32_t>(nowMs - lastLogAtMs_) <
            kCommandErrorLogIntervalMs)
      return false;
    logged_ = true;
    lastLogAtMs_ = nowMs;
    return true;
  }

 private:
  uint32_t lastLogAtMs_;
  bool logged_;
};

}  // namespace serialcli
}  // namespace voxone
