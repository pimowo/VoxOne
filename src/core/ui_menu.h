#ifndef VOXONE_UI_MENU_H
#define VOXONE_UI_MENU_H

#include <atomic>
#include <stdint.h>

#include "source_catalog.h"

namespace voxone {
namespace ui_menu {

enum class MenuState : uint8_t { CLOSED, OPEN, EDITING, CONFIRM_SAVE };

enum class MenuLevel : uint8_t { MAIN, SUBMENU };

enum class MenuId : uint8_t {
  NONE,
  MAIN,
  AUDIO,
  SCREEN,
  NETWORK,
  SYSTEM,
  SOURCE,
  DLNA,
  SAVE_CONFIRM,
};

enum class MenuCommand : uint8_t {
  PREVIOUS,
  NEXT,
  SELECT,
  BACK,
  CLOSE,
};

enum class MenuAction : uint8_t {
  NONE,
  NAVIGATED,
  ENTERED_SUBMENU,
  SELECTED,
  BACKED,
  CLOSED,
  EDITING_STARTED,
  EDITING_FINISHED,
  SETTING_CHANGED,
  CONFIRMATION_REQUIRED,
  SAVE_REQUESTED,
  DISCARD_REQUESTED,
};

enum class MenuSetting : uint8_t {
  NONE,
  AUDIO_BALANCE,
  AUDIO_BASS,
  AUDIO_TREBLE,
  LINE_OUT_ENABLED,
  LINE_OUT_VOLUME,
  DISPLAY_BRIGHTNESS,
  DISPLAY_THEME,
  SCREENSAVER_ENABLED,
  ENCODER_UI_REVERSED,
};

struct MenuSettings {
  int8_t balance = 0;
  int8_t bass = 0;
  int8_t treble = 0;
  uint8_t brightness = 100;
  uint8_t tftTheme = 0;
  bool screensaverEnabled = false;
  bool encoderUiReversed = false;
  bool lineOutEnabled = false;
  uint8_t lineOutVolume = 100;
  uint8_t maximumVolume = 100;
};

inline bool operator==(const MenuSettings& left, const MenuSettings& right) {
  return left.balance == right.balance && left.bass == right.bass &&
         left.treble == right.treble &&
         left.brightness == right.brightness &&
         left.tftTheme == right.tftTheme &&
         left.screensaverEnabled == right.screensaverEnabled &&
         left.encoderUiReversed == right.encoderUiReversed &&
         left.lineOutEnabled == right.lineOutEnabled &&
         left.lineOutVolume == right.lineOutVolume;
}

inline bool operator!=(const MenuSettings& left, const MenuSettings& right) {
  return !(left == right);
}

struct DlnaMenuEntry {
  char label[96]{};
  bool folder = false;
  bool playable = false;
  bool playing = false;
};

constexpr uint8_t kMaxDlnaMenuItems = 24;

struct MenuSnapshot {
  MenuState state;
  MenuLevel level;
  MenuId menuId;
  uint8_t selectedIndex;
  uint32_t revision;
  uint8_t dynamicBank;
  uint8_t sourceMask;
  SourceId activeSource;
  MenuSettings settings;
  bool settingsDirty;
  uint8_t dynamicCount = 0;
  DlnaMenuEntry dynamicItems[kMaxDlnaMenuItems]{};

  MenuSnapshot(MenuState menuState = MenuState::CLOSED,
               MenuLevel menuLevel = MenuLevel::MAIN,
               MenuId id = MenuId::MAIN, uint8_t selected = 0,
               uint32_t menuRevision = 0, uint8_t bank = 0,
               uint8_t sources = kCoreSourceMask,
               SourceId active = SourceId::Web,
               bool encoderReversed = false)
      : state(menuState), level(menuLevel), menuId(id),
        selectedIndex(selected), revision(menuRevision), dynamicBank(bank),
        sourceMask(sources), activeSource(active), settings{},
        settingsDirty(false) {
    settings.encoderUiReversed = encoderReversed;
  }
};

enum class MenuRenderKind : uint8_t { NONE, FULL, SELECTION };

struct MenuRenderPlan {
  MenuRenderKind kind;
  uint8_t previousIndex;
  uint8_t selectedIndex;
};

class UiMenu {
 public:
  static constexpr uint8_t kMainItemCount = 5;

  bool open(MenuId initialMenu = MenuId::MAIN);
  bool close();
  bool visible() const;
  MenuState state() const;
  MenuLevel level() const;
  MenuId menuId() const;
  uint8_t itemCount() const;
  uint8_t selectedIndex() const;
  const char* selectedLabel() const;
  bool selectedHasSubmenu() const;
  bool selectedDlnaFolder() const;
  bool selectedDlnaPlayable() const;
  SourceId selectedSource() const;
  MenuSetting selectedSetting() const;
  void configureSources(uint8_t availableMask, SourceId activeSource);
  void configureEncoderUiReversed(bool reversed);
  void beginSettingsTransaction(const MenuSettings& persisted);
  MenuSettings settings() const;
  bool settingsDirty() const;
  bool finishTransaction();
  void setDlnaStatus(const char* label);
  void setDlnaItems(const DlnaMenuEntry* entries, uint8_t count);
  MenuSnapshot snapshot() const;
  MenuAction handle(MenuCommand command, int8_t steps = 1);

 private:
  void markChanged();

  std::atomic<MenuState> state_{MenuState::CLOSED};
  std::atomic<MenuId> menuId_{MenuId::MAIN};
  std::atomic<uint8_t> selectedIndex_{0};
  std::atomic<uint32_t> revision_{0};
  std::atomic<bool> encoderUiReversed_{false};
  MenuSettings persistedSettings_{};
  std::atomic<int8_t> draftBalance_{0};
  std::atomic<int8_t> draftBass_{0};
  std::atomic<int8_t> draftTreble_{0};
  std::atomic<uint8_t> draftBrightness_{100};
  std::atomic<uint8_t> draftTftTheme_{0};
  std::atomic<bool> draftScreensaverEnabled_{false};
  std::atomic<bool> draftLineOutEnabled_{false};
  std::atomic<uint8_t> draftLineOutVolume_{100};
  mutable char selectedDlnaLabel_[96]{};
};

MenuLevel menuLevel(MenuId menuId);
const char* menuTitle(MenuId menuId);
const char* menuContextHeader(MenuId menuId);
uint8_t itemCount(MenuId menuId);
const char* itemLabel(MenuId menuId, uint8_t index);
bool itemHasSubmenu(MenuId menuId, uint8_t index);
MenuSetting itemSetting(MenuId menuId, uint8_t index);
bool formatItemValue(const MenuSnapshot& snapshot, uint8_t index,
                     char* output, size_t outputSize);
uint8_t sourceItemCount(uint8_t mask);
SourceId sourceItemAt(uint8_t mask, uint8_t index);
MenuRenderPlan menuRenderPlan(const MenuSnapshot& previous,
                              const MenuSnapshot& current,
                              bool previousRendered);

inline const char* menuTitle(const MenuSnapshot& snapshot) {
  return menuTitle(snapshot.menuId);
}

inline uint8_t itemCount(const MenuSnapshot& snapshot) {
  if (snapshot.menuId == MenuId::SOURCE) {
    return sourceItemCount(snapshot.sourceMask);
  }
  if (snapshot.menuId != MenuId::DLNA) return itemCount(snapshot.menuId);
  return snapshot.dynamicCount;
}

inline const char* itemLabel(const MenuSnapshot& snapshot, uint8_t index) {
  if (snapshot.menuId == MenuId::SOURCE) {
    return sourceMenuLabel(sourceItemAt(snapshot.sourceMask, index));
  }
  if (snapshot.menuId != MenuId::DLNA) return itemLabel(snapshot.menuId, index);
  return index < snapshot.dynamicCount
             ? snapshot.dynamicItems[index].label : "";
}

inline bool itemIsActive(const MenuSnapshot& snapshot, uint8_t index) {
  if (snapshot.menuId == MenuId::SOURCE)
    return sourceItemAt(snapshot.sourceMask, index) == snapshot.activeSource;
  if (snapshot.menuId == MenuId::MAIN)
    return index == 4 && snapshot.activeSource == SourceId::Dlna;
  if (snapshot.menuId == MenuId::DLNA) {
    return index < snapshot.dynamicCount &&
           snapshot.dynamicItems[index].playing;
  }
  return snapshot.menuId == MenuId::SYSTEM && index == 3 &&
         snapshot.settings.encoderUiReversed;
}

inline bool itemHasSubmenu(const MenuSnapshot& snapshot, uint8_t index) {
  if (snapshot.menuId != MenuId::DLNA)
    return itemHasSubmenu(snapshot.menuId, index);
  return index < snapshot.dynamicCount &&
         snapshot.dynamicItems[index].folder;
}

}  // namespace ui_menu
}  // namespace voxone

extern voxone::ui_menu::UiMenu uiMenu;

#endif
