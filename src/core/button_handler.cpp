#include "button_handler.h"

namespace voxone {
namespace power {

void ButtonHandler::reset(bool pressed, uint32_t nowMs) {
  candidatePressed_ = pressed;
  stablePressed_ = pressed;
  longPressReported_ = false;
  clickPending_ = false;
  secondClickEligible_ = false;
  candidateSinceMs_ = nowMs;
  pressStartedMs_ = nowMs;
  firstClickReleasedMs_ = nowMs;
}

ButtonEvent ButtonHandler::update(bool pressed, uint32_t nowMs) {
  ButtonEvent event = ButtonEvent::BUTTON_NONE;

  if (pressed != candidatePressed_) {
    candidatePressed_ = pressed;
    candidateSinceMs_ = nowMs;
    if (candidatePressed_) {
      secondClickEligible_ =
          clickPending_ &&
          nowMs - firstClickReleasedMs_ < kDoubleClickIntervalMs;
    } else if (!stablePressed_) {
      // A bounced press candidate never became a stable second click.
      secondClickEligible_ = false;
    }
  }

  if (candidatePressed_ != stablePressed_ &&
      nowMs - candidateSinceMs_ >= kDebounceMs) {
    stablePressed_ = candidatePressed_;
    if (stablePressed_) {
      pressStartedMs_ = nowMs;
      longPressReported_ = false;

      // A new press outside the double-click interval closes the pending
      // single-click gesture before tracking the new press.
      if (clickPending_ && !secondClickEligible_) {
        clickPending_ = false;
        event = ButtonEvent::BUTTON_CLICK;
      }
    } else {
      const uint32_t heldMs = nowMs - pressStartedMs_;
      if (!longPressReported_ && heldMs < kClickMaximumMs) {
        if (clickPending_ && secondClickEligible_) {
          clickPending_ = false;
          event = ButtonEvent::BUTTON_DOUBLE_CLICK;
        } else {
          clickPending_ = true;
          firstClickReleasedMs_ = nowMs;
        }
      }
      secondClickEligible_ = false;
    }
  }

  // If the second press stops being short, the first click is no longer a
  // double-click candidate. Report it while the second press may continue
  // toward a long-press event.
  if (event == ButtonEvent::BUTTON_NONE && stablePressed_ &&
      secondClickEligible_ && clickPending_ &&
      nowMs - pressStartedMs_ >= kClickMaximumMs) {
    secondClickEligible_ = false;
    clickPending_ = false;
    event = ButtonEvent::BUTTON_CLICK;
  }

  if (event == ButtonEvent::BUTTON_NONE && stablePressed_ &&
      !longPressReported_ && nowMs - pressStartedMs_ >= kLongPressMs) {
    longPressReported_ = true;
    event = ButtonEvent::BUTTON_LONG_PRESS;
  }

  if (event == ButtonEvent::BUTTON_NONE && clickPending_ &&
      !secondClickEligible_ &&
      nowMs - firstClickReleasedMs_ >= kDoubleClickIntervalMs) {
    clickPending_ = false;
    event = ButtonEvent::BUTTON_CLICK;
  }

  return event;
}

}  // namespace power
}  // namespace voxone
