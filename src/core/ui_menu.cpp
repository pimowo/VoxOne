#include "ui_menu.h"

namespace {
constexpr const char* kMenuItems[voxone::ui_menu::UiMenu::kItemCount] = {
    "Polaczenie", "Audio", "Ekran", "System"};
}

namespace voxone {
namespace ui_menu {

const char* itemLabel(uint8_t index) {
  return index < UiMenu::kItemCount ? kMenuItems[index] : "";
}

bool UiMenu::open() {
  if (visible()) return false;
  selectedIndex_.store(0, std::memory_order_relaxed);
  state_.store(MenuState::OPEN, std::memory_order_release);
  markChanged();
  return true;
}

bool UiMenu::close() {
  if (!visible()) return false;
  state_.store(MenuState::CLOSED, std::memory_order_release);
  markChanged();
  return true;
}

bool UiMenu::visible() const {
  return state() != MenuState::CLOSED;
}

MenuState UiMenu::state() const {
  return state_.load(std::memory_order_acquire);
}

uint8_t UiMenu::selectedIndex() const {
  return selectedIndex_.load(std::memory_order_acquire);
}

const char* UiMenu::selectedLabel() const {
  return itemLabel(selectedIndex());
}

MenuSnapshot UiMenu::snapshot() const {
  MenuSnapshot result{};
  uint32_t before = 0;
  do {
    before = revision_.load(std::memory_order_acquire);
    result.state = state_.load(std::memory_order_acquire);
    result.selectedIndex = selectedIndex_.load(std::memory_order_acquire);
    result.revision = revision_.load(std::memory_order_acquire);
  } while (before != result.revision);
  return result;
}

MenuAction UiMenu::handle(MenuCommand command, int8_t steps) {
  if (!visible()) return MenuAction::NONE;

  if (command == MenuCommand::CLOSE) {
    close();
    return MenuAction::CLOSED;
  }

  if (command == MenuCommand::SELECT) {
    const MenuState next = state() == MenuState::EDITING
                               ? MenuState::OPEN
                               : MenuState::EDITING;
    state_.store(next, std::memory_order_release);
    markChanged();
    return MenuAction::SELECTED;
  }

  int16_t magnitude = steps;
  if (magnitude < 0) magnitude = -magnitude;
  if (magnitude == 0) magnitude = 1;
  const int16_t direction = command == MenuCommand::NEXT ? 1 : -1;
  const int16_t count = kItemCount;
  int16_t next = static_cast<int16_t>(selectedIndex()) +
                 direction * (magnitude % count);
  next %= count;
  if (next < 0) next += count;
  const uint8_t nextIndex = static_cast<uint8_t>(next);
  if (nextIndex == selectedIndex()) return MenuAction::NONE;
  selectedIndex_.store(nextIndex, std::memory_order_release);
  markChanged();
  return MenuAction::NAVIGATED;
}

void UiMenu::markChanged() {
  revision_.fetch_add(1, std::memory_order_release);
}

}  // namespace ui_menu
}  // namespace voxone

voxone::ui_menu::UiMenu uiMenu;
