#include "ui_menu.h"

#include <stddef.h>
#include <string.h>

namespace {
using voxone::ui_menu::MenuId;

struct MenuItemDefinition {
  const char* label;
  MenuId submenu;
};

constexpr MenuItemDefinition kMainItems[] = {
    {"Audio", MenuId::AUDIO},
    {"Wyświetlacz", MenuId::SCREEN},
    {"Sieć", MenuId::NETWORK},
    {"System", MenuId::SYSTEM},
    {"DLNA", MenuId::DLNA},
};

constexpr MenuItemDefinition kAudioItems[] = {
    {"Balans", MenuId::NONE},
    {"Bass", MenuId::NONE},
    {"Treble", MenuId::NONE},
};

constexpr MenuItemDefinition kDisplayItems[] = {
    {"Jasność", MenuId::NONE},
    {"Motyw", MenuId::NONE},
    {"Wygaszacz", MenuId::NONE},
};

constexpr MenuItemDefinition kNetworkItems[] = {
    {"Wi-Fi", MenuId::NONE},
    {"Status", MenuId::NONE},
    {"Konfiguracja", MenuId::NONE},
};

constexpr MenuItemDefinition kSystemItems[] = {
    {"Informacje", MenuId::NONE},
    {"Zasilanie", MenuId::NONE},
    {"Restart", MenuId::NONE},
};

struct DlnaMenuPage {
  voxone::ui_menu::DlnaMenuEntry
      entries[voxone::ui_menu::kMaxDlnaMenuItems];
  uint8_t count = 0;
};

DlnaMenuPage dlnaPages[2];
std::atomic<uint8_t> activeDlnaPage{0};

struct MenuDefinition {
  const MenuItemDefinition* items;
  uint8_t count;
};

template <size_t Size>
constexpr MenuDefinition definition(const MenuItemDefinition (&items)[Size]) {
  return {items, static_cast<uint8_t>(Size)};
}

MenuDefinition menuDefinition(MenuId menuId) {
  switch (menuId) {
    case MenuId::MAIN: return definition(kMainItems);
    case MenuId::AUDIO: return definition(kAudioItems);
    case MenuId::SCREEN: return definition(kDisplayItems);
    case MenuId::NETWORK: return definition(kNetworkItems);
    case MenuId::SYSTEM: return definition(kSystemItems);
    case MenuId::DLNA: break;
    case MenuId::NONE: break;
  }
  return {nullptr, 0};
}

uint8_t parentIndex(MenuId menuId) {
  switch (menuId) {
    case MenuId::AUDIO: return 0;
    case MenuId::SCREEN: return 1;
    case MenuId::NETWORK: return 2;
    case MenuId::SYSTEM: return 3;
    case MenuId::DLNA: return 4;
    case MenuId::NONE:
    case MenuId::MAIN: return 0;
  }
  return 0;
}
}  // namespace

namespace voxone {
namespace ui_menu {

MenuLevel menuLevel(MenuId menuId) {
  return menuId == MenuId::MAIN ? MenuLevel::MAIN : MenuLevel::SUBMENU;
}

const char* menuTitle(MenuId menuId) {
  switch (menuId) {
    case MenuId::MAIN: return "MENU";
    case MenuId::AUDIO: return "Audio";
    case MenuId::SCREEN: return "Wyświetlacz";
    case MenuId::NETWORK: return "Sieć";
    case MenuId::SYSTEM: return "System";
    case MenuId::DLNA: return "DLNA";
    case MenuId::NONE: return "";
  }
  return "";
}

const char* menuContextHeader(MenuId menuId) {
  switch (menuId) {
    case MenuId::MAIN: return "MENU";
    case MenuId::AUDIO: return "AUDIO";
    case MenuId::SCREEN: return "WYŚWIETLACZ";
    case MenuId::NETWORK: return "SIEĆ";
    case MenuId::SYSTEM: return "SYSTEM";
    case MenuId::DLNA: return "DLNA";
    case MenuId::NONE: return "";
  }
  return "";
}

uint8_t itemCount(MenuId menuId) {
  if (menuId == MenuId::DLNA)
    return dlnaPages[activeDlnaPage.load(std::memory_order_acquire)].count;
  return menuDefinition(menuId).count;
}

const char* itemLabel(MenuId menuId, uint8_t index) {
  if (menuId == MenuId::DLNA) {
    const DlnaMenuPage& page =
        dlnaPages[activeDlnaPage.load(std::memory_order_acquire)];
    return index < page.count ? page.entries[index].label : "";
  }
  const auto menu = menuDefinition(menuId);
  return index < menu.count ? menu.items[index].label : "";
}

bool itemHasSubmenu(MenuId menuId, uint8_t index) {
  if (menuId == MenuId::DLNA) {
    const DlnaMenuPage& page =
        dlnaPages[activeDlnaPage.load(std::memory_order_acquire)];
    return index < page.count && page.entries[index].folder;
  }
  const auto menu = menuDefinition(menuId);
  return index < menu.count && menu.items[index].submenu != MenuId::NONE;
}

MenuRenderPlan menuRenderPlan(const MenuSnapshot& previous,
                              const MenuSnapshot& current,
                              bool previousRendered) {
  if (!previousRendered || previous.menuId != current.menuId ||
      (current.menuId == MenuId::DLNA &&
       previous.dynamicBank != current.dynamicBank))
    return {MenuRenderKind::FULL, previous.selectedIndex,
            current.selectedIndex};
  if (previous.selectedIndex != current.selectedIndex)
    return {MenuRenderKind::SELECTION, previous.selectedIndex,
            current.selectedIndex};
  return {MenuRenderKind::NONE, previous.selectedIndex,
          current.selectedIndex};
}

bool UiMenu::open() {
  if (visible()) return false;
  menuId_.store(MenuId::MAIN, std::memory_order_relaxed);
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

MenuLevel UiMenu::level() const {
  return menuLevel(menuId());
}

MenuId UiMenu::menuId() const {
  return menuId_.load(std::memory_order_acquire);
}

uint8_t UiMenu::itemCount() const {
  return ui_menu::itemCount(menuId());
}

uint8_t UiMenu::selectedIndex() const {
  return selectedIndex_.load(std::memory_order_acquire);
}

const char* UiMenu::selectedLabel() const {
  return itemLabel(menuId(), selectedIndex());
}

bool UiMenu::selectedHasSubmenu() const {
  return itemHasSubmenu(menuId(), selectedIndex());
}

bool UiMenu::selectedDlnaFolder() const {
  if (menuId() != MenuId::DLNA) return false;
  const DlnaMenuPage& page =
      dlnaPages[activeDlnaPage.load(std::memory_order_acquire)];
  const uint8_t selected = selectedIndex();
  return selected < page.count && page.entries[selected].folder;
}

bool UiMenu::selectedDlnaPlayable() const {
  if (menuId() != MenuId::DLNA) return false;
  const DlnaMenuPage& page =
      dlnaPages[activeDlnaPage.load(std::memory_order_acquire)];
  const uint8_t selected = selectedIndex();
  return selected < page.count && page.entries[selected].playable;
}

void UiMenu::setDlnaStatus(const char* label) {
  DlnaMenuEntry entry{};
  const char* text = label && label[0] ? label : "DLNA";
  const size_t length = strlen(text) < sizeof(entry.label) - 1
                            ? strlen(text) : sizeof(entry.label) - 1;
  memcpy(entry.label, text, length);
  entry.label[length] = '\0';
  setDlnaItems(&entry, 1);
}

void UiMenu::setDlnaItems(const DlnaMenuEntry* entries, uint8_t count) {
  const uint8_t current = activeDlnaPage.load(std::memory_order_acquire);
  const uint8_t next = current ^ 1U;
  DlnaMenuPage& page = dlnaPages[next];
  page = DlnaMenuPage{};
  if (count > kMaxDlnaMenuItems) count = kMaxDlnaMenuItems;
  if (entries && count) {
    for (uint8_t index = 0; index < count; ++index)
      page.entries[index] = entries[index];
    page.count = count;
  }
  selectedIndex_.store(0, std::memory_order_relaxed);
  activeDlnaPage.store(next, std::memory_order_release);
  markChanged();
}

MenuSnapshot UiMenu::snapshot() const {
  MenuSnapshot result{};
  uint32_t before = 0;
  do {
    before = revision_.load(std::memory_order_acquire);
    result.state = state_.load(std::memory_order_acquire);
    result.menuId = menuId_.load(std::memory_order_acquire);
    result.level = menuLevel(result.menuId);
    result.selectedIndex = selectedIndex_.load(std::memory_order_acquire);
    result.dynamicBank = activeDlnaPage.load(std::memory_order_acquire);
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

  if (command == MenuCommand::BACK) {
    const MenuId current = menuId();
    if (current == MenuId::MAIN) return MenuAction::NONE;
    selectedIndex_.store(parentIndex(current), std::memory_order_relaxed);
    menuId_.store(MenuId::MAIN, std::memory_order_relaxed);
    state_.store(MenuState::OPEN, std::memory_order_release);
    markChanged();
    return MenuAction::BACKED;
  }

  if (command == MenuCommand::SELECT) {
    const MenuId current = menuId();
    if (current == MenuId::DLNA) {
      const DlnaMenuPage& page =
          dlnaPages[activeDlnaPage.load(std::memory_order_acquire)];
      const uint8_t selected = selectedIndex();
      if (selected >= page.count ||
          (!page.entries[selected].folder && !page.entries[selected].playable))
        return MenuAction::NONE;
      state_.store(MenuState::OPEN, std::memory_order_release);
      return MenuAction::SELECTED;
    }
    const auto menu = menuDefinition(current);
    const uint8_t selected = selectedIndex();
    if (selected >= menu.count) return MenuAction::NONE;
    const MenuId submenu = menu.items[selected].submenu;
    if (submenu != MenuId::NONE) {
      menuId_.store(submenu, std::memory_order_relaxed);
      selectedIndex_.store(0, std::memory_order_relaxed);
      state_.store(MenuState::OPEN, std::memory_order_release);
      markChanged();
      return MenuAction::ENTERED_SUBMENU;
    }
    if (state() != MenuState::EDITING) {
      state_.store(MenuState::EDITING, std::memory_order_release);
      markChanged();
    }
    return MenuAction::SELECTED;
  }

  int16_t magnitude = steps;
  if (magnitude < 0) magnitude = -magnitude;
  if (magnitude == 0) magnitude = 1;
  const int16_t direction = command == MenuCommand::NEXT ? 1 : -1;
  const int16_t count = itemCount();
  if (count == 0) return MenuAction::NONE;
  int16_t next = static_cast<int16_t>(selectedIndex()) +
                 direction * (magnitude % count);
  next %= count;
  if (next < 0) next += count;
  const uint8_t nextIndex = static_cast<uint8_t>(next);
  if (nextIndex == selectedIndex()) return MenuAction::NONE;
  selectedIndex_.store(nextIndex, std::memory_order_relaxed);
  state_.store(MenuState::OPEN, std::memory_order_release);
  markChanged();
  return MenuAction::NAVIGATED;
}

void UiMenu::markChanged() {
  revision_.fetch_add(1, std::memory_order_release);
}

uint8_t dlnaItemCount(uint8_t bank) {
  return dlnaPages[bank & 1U].count;
}

const char* dlnaItemLabel(uint8_t bank, uint8_t index) {
  const DlnaMenuPage& page = dlnaPages[bank & 1U];
  return index < page.count ? page.entries[index].label : "";
}

bool dlnaItemFolder(uint8_t bank, uint8_t index) {
  const DlnaMenuPage& page = dlnaPages[bank & 1U];
  return index < page.count && page.entries[index].folder;
}

}  // namespace ui_menu
}  // namespace voxone

voxone::ui_menu::UiMenu uiMenu;
