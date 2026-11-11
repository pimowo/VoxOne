#include "ui_menu.h"

#include <stddef.h>
#include <stdio.h>
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
    {"LINE OUT", MenuId::NONE},
    {"Poziom LINE", MenuId::NONE},
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
    {"Odwrócony enkoder", MenuId::NONE},
};

constexpr MenuItemDefinition kSaveItems[] = {
    {"TAK", MenuId::NONE},
    {"NIE", MenuId::NONE},
};

struct DlnaMenuPage {
  voxone::ui_menu::DlnaMenuEntry
      entries[voxone::ui_menu::kMaxDlnaMenuItems];
  uint8_t count = 0;
};

constexpr uint8_t kDlnaMenuPageCount = 3;
DlnaMenuPage dlnaPages[kDlnaMenuPageCount];
std::atomic<uint8_t> activeDlnaPage{0};
std::atomic<uint8_t> dlnaPageReaders[kDlnaMenuPageCount];
std::atomic<uint8_t> availableSourceMask{kCoreSourceMask};
std::atomic<SourceId> activeMenuSource{SourceId::Web};

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
    case MenuId::SOURCE: break;
    case MenuId::DLNA: break;
    case MenuId::SAVE_CONFIRM: return definition(kSaveItems);
    case MenuId::NONE: break;
  }
  return {nullptr, 0};
}

uint8_t parentIndex(MenuId menuId) {
  switch (menuId) {
    case MenuId::SOURCE: return 0;
    case MenuId::AUDIO: return 0;
    case MenuId::SCREEN: return 1;
    case MenuId::NETWORK: return 2;
    case MenuId::SYSTEM: return 3;
    case MenuId::DLNA: return 4;
    case MenuId::SAVE_CONFIRM: return 0;
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
    case MenuId::SOURCE: return "Źródło";
    case MenuId::DLNA: return "DLNA";
    case MenuId::SAVE_CONFIRM: return "Zapisać zmiany?";
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
    case MenuId::SOURCE: return "ŹRÓDŁO";
    case MenuId::DLNA: return "DLNA";
    case MenuId::SAVE_CONFIRM: return "ZAPISAĆ ZMIANY?";
    case MenuId::NONE: return "";
  }
  return "";
}

uint8_t itemCount(MenuId menuId) {
  if (menuId == MenuId::SOURCE)
    return sourceItemCount(availableSourceMask.load(std::memory_order_acquire));
  if (menuId == MenuId::DLNA)
    return dlnaPages[activeDlnaPage.load(std::memory_order_acquire)].count;
  return menuDefinition(menuId).count;
}

const char* itemLabel(MenuId menuId, uint8_t index) {
  if (menuId == MenuId::SOURCE)
    return sourceMenuLabel(sourceItemAt(
        availableSourceMask.load(std::memory_order_acquire), index));
  if (menuId == MenuId::DLNA) {
    const DlnaMenuPage& page =
        dlnaPages[activeDlnaPage.load(std::memory_order_acquire)];
    return index < page.count ? page.entries[index].label : "";
  }
  const auto menu = menuDefinition(menuId);
  return index < menu.count ? menu.items[index].label : "";
}

bool itemHasSubmenu(MenuId menuId, uint8_t index) {
  if (menuId == MenuId::SOURCE) return false;
  if (menuId == MenuId::DLNA) {
    const DlnaMenuPage& page =
        dlnaPages[activeDlnaPage.load(std::memory_order_acquire)];
    return index < page.count && page.entries[index].folder;
  }
  const auto menu = menuDefinition(menuId);
  return index < menu.count && menu.items[index].submenu != MenuId::NONE;
}

MenuSetting itemSetting(MenuId menuId, uint8_t index) {
  if (menuId == MenuId::AUDIO) {
    if (index == 0) return MenuSetting::AUDIO_BALANCE;
    if (index == 1) return MenuSetting::AUDIO_BASS;
    if (index == 2) return MenuSetting::AUDIO_TREBLE;
    if (index == 3) return MenuSetting::LINE_OUT_ENABLED;
    if (index == 4) return MenuSetting::LINE_OUT_VOLUME;
  }
  if (menuId == MenuId::SCREEN) {
    if (index == 0) return MenuSetting::DISPLAY_BRIGHTNESS;
    if (index == 1) return MenuSetting::DISPLAY_THEME;
    if (index == 2) return MenuSetting::SCREENSAVER_ENABLED;
  }
  if (menuId == MenuId::SYSTEM && index == 3)
    return MenuSetting::ENCODER_UI_REVERSED;
  return MenuSetting::NONE;
}

bool formatItemValue(const MenuSnapshot& snapshot, uint8_t index,
                     char* output, size_t outputSize) {
  if (!output || outputSize == 0) return false;
  output[0] = '\0';
  switch (itemSetting(snapshot.menuId, index)) {
    case MenuSetting::AUDIO_BALANCE:
      snprintf(output, outputSize, "%+d", snapshot.settings.balance);
      break;
    case MenuSetting::AUDIO_BASS:
      snprintf(output, outputSize, "%+d", snapshot.settings.bass);
      break;
    case MenuSetting::AUDIO_TREBLE:
      snprintf(output, outputSize, "%+d", snapshot.settings.treble);
      break;
    case MenuSetting::LINE_OUT_ENABLED:
      snprintf(output, outputSize, "%s",
               snapshot.settings.lineOutEnabled ? "TAK" : "NIE");
      break;
    case MenuSetting::LINE_OUT_VOLUME:
      snprintf(output, outputSize, "%u%%", snapshot.settings.lineOutVolume);
      break;
    case MenuSetting::DISPLAY_BRIGHTNESS:
      snprintf(output, outputSize, "%u%%", snapshot.settings.brightness);
      break;
    case MenuSetting::DISPLAY_THEME:
      snprintf(output, outputSize, "%s",
               snapshot.settings.tftTheme == 1 ? "Yamaha" : "Domyślny");
      break;
    case MenuSetting::SCREENSAVER_ENABLED:
      snprintf(output, outputSize, "%s",
               snapshot.settings.screensaverEnabled ? "TAK" : "NIE");
      break;
    case MenuSetting::ENCODER_UI_REVERSED:
      snprintf(output, outputSize, "%s",
               snapshot.settings.encoderUiReversed ? "TAK" : "NIE");
      break;
    case MenuSetting::NONE:
      return false;
  }
  return true;
}

MenuRenderPlan menuRenderPlan(const MenuSnapshot& previous,
                              const MenuSnapshot& current,
                              bool previousRendered) {
  if (!previousRendered || previous.menuId != current.menuId ||
      previous.state != current.state ||
      (current.menuId == MenuId::DLNA &&
       previous.dynamicBank != current.dynamicBank) ||
      (current.menuId == MenuId::SOURCE &&
       (previous.sourceMask != current.sourceMask ||
        previous.activeSource != current.activeSource)) ||
      (current.menuId == MenuId::SYSTEM &&
       previous.settings.encoderUiReversed !=
           current.settings.encoderUiReversed) ||
      previous.settings != current.settings)
    return {MenuRenderKind::FULL, previous.selectedIndex,
            current.selectedIndex};
  if (previous.selectedIndex != current.selectedIndex)
    return {MenuRenderKind::SELECTION, previous.selectedIndex,
            current.selectedIndex};
  return {MenuRenderKind::NONE, previous.selectedIndex,
          current.selectedIndex};
}

bool UiMenu::open(MenuId initialMenu) {
  if (visible()) return false;
  if (initialMenu == MenuId::NONE) initialMenu = MenuId::MAIN;
  menuId_.store(initialMenu, std::memory_order_relaxed);
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
  if (menuId() == MenuId::DLNA) return snapshot().dynamicCount;
  return ui_menu::itemCount(menuId());
}

uint8_t UiMenu::selectedIndex() const {
  return selectedIndex_.load(std::memory_order_acquire);
}

const char* UiMenu::selectedLabel() const {
  if (menuId() == MenuId::DLNA) {
    const MenuSnapshot view = snapshot();
    snprintf(selectedDlnaLabel_, sizeof(selectedDlnaLabel_), "%s",
             itemLabel(view, view.selectedIndex));
    return selectedDlnaLabel_;
  }
  return itemLabel(menuId(), selectedIndex());
}

bool UiMenu::selectedHasSubmenu() const {
  if (menuId() == MenuId::DLNA) {
    const MenuSnapshot view = snapshot();
    return itemHasSubmenu(view, view.selectedIndex);
  }
  return itemHasSubmenu(menuId(), selectedIndex());
}

bool UiMenu::selectedDlnaFolder() const {
  if (menuId() != MenuId::DLNA) return false;
  const MenuSnapshot view = snapshot();
  return itemHasSubmenu(view, view.selectedIndex);
}

bool UiMenu::selectedDlnaPlayable() const {
  if (menuId() != MenuId::DLNA) return false;
  const MenuSnapshot view = snapshot();
  return view.selectedIndex < view.dynamicCount &&
         view.dynamicItems[view.selectedIndex].playable;
}

SourceId UiMenu::selectedSource() const {
  return sourceItemAt(availableSourceMask.load(std::memory_order_acquire),
                      selectedIndex());
}

MenuSetting UiMenu::selectedSetting() const {
  return itemSetting(menuId(), selectedIndex());
}

void UiMenu::configureSources(uint8_t availableMask, SourceId activeSource) {
  availableMask |= sourceBit(SourceId::Web);
  const uint8_t previous =
      availableSourceMask.exchange(availableMask, std::memory_order_acq_rel);
  const SourceId previousActive =
      activeMenuSource.exchange(activeSource, std::memory_order_acq_rel);
  if (menuId() == MenuId::SOURCE) {
    uint8_t activeIndex = 0;
    const uint8_t count = sourceItemCount(availableMask);
    for (uint8_t index = 0; index < count; ++index) {
      if (sourceItemAt(availableMask, index) == activeSource) {
        activeIndex = index;
        break;
      }
    }
    selectedIndex_.store(activeIndex, std::memory_order_relaxed);
  }
  if (previous != availableMask || previousActive != activeSource)
    markChanged();
}

void UiMenu::configureEncoderUiReversed(bool reversed) {
  const bool previous =
      encoderUiReversed_.exchange(reversed, std::memory_order_acq_rel);
  if (previous != reversed) markChanged();
}

void UiMenu::beginSettingsTransaction(const MenuSettings& persisted) {
  persistedSettings_ = persisted;
  draftBalance_.store(persisted.balance, std::memory_order_relaxed);
  draftBass_.store(persisted.bass, std::memory_order_relaxed);
  draftTreble_.store(persisted.treble, std::memory_order_relaxed);
  draftBrightness_.store(persisted.brightness, std::memory_order_relaxed);
  draftTftTheme_.store(persisted.tftTheme, std::memory_order_relaxed);
  draftScreensaverEnabled_.store(persisted.screensaverEnabled,
                                 std::memory_order_relaxed);
  draftLineOutEnabled_.store(persisted.lineOutEnabled,
                             std::memory_order_relaxed);
  draftLineOutVolume_.store(persisted.lineOutVolume,
                            std::memory_order_relaxed);
  encoderUiReversed_.store(persisted.encoderUiReversed,
                           std::memory_order_release);
  markChanged();
}

MenuSettings UiMenu::settings() const {
  MenuSettings result{};
  result.balance = draftBalance_.load(std::memory_order_acquire);
  result.bass = draftBass_.load(std::memory_order_acquire);
  result.treble = draftTreble_.load(std::memory_order_acquire);
  result.brightness = draftBrightness_.load(std::memory_order_acquire);
  result.tftTheme = draftTftTheme_.load(std::memory_order_acquire);
  result.screensaverEnabled =
      draftScreensaverEnabled_.load(std::memory_order_acquire);
  result.lineOutEnabled =
      draftLineOutEnabled_.load(std::memory_order_acquire);
  result.lineOutVolume =
      draftLineOutVolume_.load(std::memory_order_acquire);
  result.encoderUiReversed =
      encoderUiReversed_.load(std::memory_order_acquire);
  return result;
}

bool UiMenu::settingsDirty() const {
  return settings() != persistedSettings_;
}

bool UiMenu::finishTransaction() {
  if (!visible()) return false;
  state_.store(MenuState::CLOSED, std::memory_order_release);
  menuId_.store(MenuId::MAIN, std::memory_order_relaxed);
  selectedIndex_.store(0, std::memory_order_relaxed);
  markChanged();
  return true;
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
  uint8_t next = current;
  for (uint8_t offset = 1; offset < kDlnaMenuPageCount; ++offset) {
    const uint8_t candidate =
        static_cast<uint8_t>((current + offset) % kDlnaMenuPageCount);
    if (dlnaPageReaders[candidate].load(std::memory_order_acquire) == 0) {
      next = candidate;
      break;
    }
  }
  // One display task pins at most one retired page while copying it. With
  // three banks a writable page is therefore always available; refusing an
  // update is safer than mutating a page still visible to a reader.
  if (next == current) return;
  DlnaMenuPage& page = dlnaPages[next];
  page.count = 0;
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
    if (result.menuId == MenuId::DLNA) {
      uint8_t bank = 0;
      do {
        bank = activeDlnaPage.load(std::memory_order_acquire);
        dlnaPageReaders[bank].fetch_add(1, std::memory_order_acq_rel);
        if (bank == activeDlnaPage.load(std::memory_order_acquire)) break;
        dlnaPageReaders[bank].fetch_sub(1, std::memory_order_release);
      } while (true);
      result.dynamicBank = bank;
      const DlnaMenuPage& page = dlnaPages[bank];
      result.dynamicCount = page.count;
      for (uint8_t index = 0; index < page.count; ++index)
        result.dynamicItems[index] = page.entries[index];
      dlnaPageReaders[bank].fetch_sub(1, std::memory_order_release);
    } else {
      result.dynamicBank = activeDlnaPage.load(std::memory_order_acquire);
      result.dynamicCount = 0;
    }
    result.sourceMask =
        availableSourceMask.load(std::memory_order_acquire);
    result.activeSource = activeMenuSource.load(std::memory_order_acquire);
    result.settings = settings();
    result.settingsDirty = settingsDirty();
    result.revision = revision_.load(std::memory_order_acquire);
  } while (before != result.revision);
  return result;
}

MenuAction UiMenu::handle(MenuCommand command, int8_t steps) {
  if (!visible()) return MenuAction::NONE;

  if (state() == MenuState::CONFIRM_SAVE) {
    if (command == MenuCommand::SELECT)
      return selectedIndex() == 0 ? MenuAction::SAVE_REQUESTED
                                  : MenuAction::DISCARD_REQUESTED;
    if (command == MenuCommand::PREVIOUS || command == MenuCommand::NEXT) {
      const uint8_t next = selectedIndex() == 0 ? 1 : 0;
      selectedIndex_.store(next, std::memory_order_relaxed);
      markChanged();
      return MenuAction::NAVIGATED;
    }
    return MenuAction::NONE;
  }

  if (command == MenuCommand::CLOSE) {
    if (!settingsDirty()) {
      close();
      return MenuAction::CLOSED;
    }
    menuId_.store(MenuId::SAVE_CONFIRM, std::memory_order_relaxed);
    selectedIndex_.store(0, std::memory_order_relaxed);
    state_.store(MenuState::CONFIRM_SAVE, std::memory_order_release);
    markChanged();
    return MenuAction::CONFIRMATION_REQUIRED;
  }

  if (command == MenuCommand::BACK) {
    if (state() == MenuState::EDITING) {
      state_.store(MenuState::OPEN, std::memory_order_release);
      markChanged();
      return MenuAction::EDITING_FINISHED;
    }
    const MenuId current = menuId();
    if (current == MenuId::MAIN) return MenuAction::NONE;
    selectedIndex_.store(parentIndex(current), std::memory_order_relaxed);
    menuId_.store(MenuId::MAIN, std::memory_order_relaxed);
    state_.store(MenuState::OPEN, std::memory_order_release);
    markChanged();
    return MenuAction::BACKED;
  }

  if (command == MenuCommand::SELECT) {
    if (state() == MenuState::EDITING) {
      state_.store(MenuState::OPEN, std::memory_order_release);
      markChanged();
      return MenuAction::EDITING_FINISHED;
    }
    const MenuId current = menuId();
    if (current == MenuId::SOURCE) {
      if (selectedIndex() >= itemCount()) return MenuAction::NONE;
      state_.store(MenuState::OPEN, std::memory_order_release);
      return MenuAction::SELECTED;
    }
    if (current == MenuId::DLNA) {
      const MenuSnapshot view = snapshot();
      const uint8_t selected = view.selectedIndex;
      if (selected >= view.dynamicCount ||
          (!view.dynamicItems[selected].folder &&
           !view.dynamicItems[selected].playable))
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
      uint8_t initialIndex = 0;
      if (submenu == MenuId::SOURCE) {
        const uint8_t mask =
            availableSourceMask.load(std::memory_order_acquire);
        const SourceId active =
            activeMenuSource.load(std::memory_order_acquire);
        const uint8_t count = sourceItemCount(mask);
        for (uint8_t index = 0; index < count; ++index) {
          if (sourceItemAt(mask, index) == active) {
            initialIndex = index;
            break;
          }
        }
      }
      selectedIndex_.store(initialIndex, std::memory_order_relaxed);
      state_.store(MenuState::OPEN, std::memory_order_release);
      markChanged();
      return MenuAction::ENTERED_SUBMENU;
    }
    if (itemSetting(current, selected) != MenuSetting::NONE &&
        state() != MenuState::EDITING) {
      state_.store(MenuState::EDITING, std::memory_order_release);
      markChanged();
      return MenuAction::EDITING_STARTED;
    }
    return MenuAction::SELECTED;
  }

  if (state() == MenuState::EDITING) {
    int16_t magnitude = steps;
    if (magnitude < 0) magnitude = -magnitude;
    if (magnitude == 0) magnitude = 1;
    const int16_t direction = command == MenuCommand::NEXT ? 1 : -1;
    const MenuSetting setting = selectedSetting();
    switch (setting) {
      case MenuSetting::AUDIO_BALANCE: {
        int16_t value = draftBalance_.load(std::memory_order_relaxed) +
                        direction * magnitude;
        if (value < -16) value = -16;
        if (value > 16) value = 16;
        draftBalance_.store(static_cast<int8_t>(value),
                            std::memory_order_release);
        break;
      }
      case MenuSetting::AUDIO_BASS: {
        int16_t value = draftBass_.load(std::memory_order_relaxed) +
                        direction * magnitude;
        if (value < -6) value = -6;
        if (value > 6) value = 6;
        draftBass_.store(static_cast<int8_t>(value),
                         std::memory_order_release);
        break;
      }
      case MenuSetting::AUDIO_TREBLE: {
        int16_t value = draftTreble_.load(std::memory_order_relaxed) +
                        direction * magnitude;
        if (value < -6) value = -6;
        if (value > 6) value = 6;
        draftTreble_.store(static_cast<int8_t>(value),
                           std::memory_order_release);
        break;
      }
      case MenuSetting::LINE_OUT_ENABLED:
        draftLineOutEnabled_.store(direction > 0,
                                   std::memory_order_release);
        break;
      case MenuSetting::LINE_OUT_VOLUME: {
        int16_t value = draftLineOutVolume_.load(std::memory_order_relaxed) +
                        direction * magnitude * 5;
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        draftLineOutVolume_.store(static_cast<uint8_t>(value),
                                  std::memory_order_release);
        break;
      }
      case MenuSetting::DISPLAY_BRIGHTNESS: {
        int16_t value = draftBrightness_.load(std::memory_order_relaxed) +
                        direction * magnitude * 5;
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        draftBrightness_.store(static_cast<uint8_t>(value),
                               std::memory_order_release);
        break;
      }
      case MenuSetting::DISPLAY_THEME:
        draftTftTheme_.store(direction > 0 ? 1 : 0,
                             std::memory_order_release);
        break;
      case MenuSetting::SCREENSAVER_ENABLED:
        draftScreensaverEnabled_.store(direction > 0,
                                       std::memory_order_release);
        break;
      case MenuSetting::ENCODER_UI_REVERSED:
        encoderUiReversed_.store(direction > 0, std::memory_order_release);
        break;
      case MenuSetting::NONE:
        return MenuAction::NONE;
    }
    markChanged();
    return MenuAction::SETTING_CHANGED;
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

uint8_t sourceItemCount(uint8_t mask) {
  uint8_t count = 0;
  for (uint8_t value = 0; value <= static_cast<uint8_t>(SourceId::Aux); ++value)
    if (sourceIncluded(mask, static_cast<SourceId>(value))) ++count;
  return count;
}

SourceId sourceItemAt(uint8_t mask, uint8_t index) {
  for (uint8_t value = 0; value <= static_cast<uint8_t>(SourceId::Aux); ++value) {
    const SourceId source = static_cast<SourceId>(value);
    if (!sourceIncluded(mask, source)) continue;
    if (index == 0) return source;
    --index;
  }
  return SourceId::Web;
}

}  // namespace ui_menu
}  // namespace voxone

voxone::ui_menu::UiMenu uiMenu;
