#ifndef VOXONE_UI_MENU_H
#define VOXONE_UI_MENU_H

#include <atomic>
#include <stdint.h>

#include "source_catalog.h"

namespace voxone {
namespace ui_menu {

enum class MenuState : uint8_t { CLOSED, OPEN, EDITING };

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
};

enum class MenuSetting : uint8_t {
  NONE,
  ENCODER_UI_REVERSED,
};

struct MenuSnapshot {
  MenuState state;
  MenuLevel level;
  MenuId menuId;
  uint8_t selectedIndex;
  uint32_t revision;
  uint8_t dynamicBank;
  uint8_t sourceMask;
  SourceId activeSource;
  bool encoderUiReversed;

  constexpr MenuSnapshot(MenuState menuState = MenuState::CLOSED,
                         MenuLevel menuLevel = MenuLevel::MAIN,
                         MenuId id = MenuId::MAIN, uint8_t selected = 0,
                         uint32_t menuRevision = 0, uint8_t bank = 0,
                         uint8_t sources = kCoreSourceMask,
                         SourceId active = SourceId::Web,
                         bool encoderReversed = false)
      : state(menuState), level(menuLevel), menuId(id),
        selectedIndex(selected), revision(menuRevision), dynamicBank(bank),
        sourceMask(sources), activeSource(active),
        encoderUiReversed(encoderReversed) {}
};

struct DlnaMenuEntry {
  char label[96]{};
  bool folder = false;
  bool playable = false;
};

constexpr uint8_t kMaxDlnaMenuItems = 24;

enum class MenuRenderKind : uint8_t { NONE, FULL, SELECTION };

struct MenuRenderPlan {
  MenuRenderKind kind;
  uint8_t previousIndex;
  uint8_t selectedIndex;
};

class UiMenu {
 public:
  static constexpr uint8_t kMainItemCount = 6;

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
};

MenuLevel menuLevel(MenuId menuId);
const char* menuTitle(MenuId menuId);
const char* menuContextHeader(MenuId menuId);
uint8_t itemCount(MenuId menuId);
const char* itemLabel(MenuId menuId, uint8_t index);
bool itemHasSubmenu(MenuId menuId, uint8_t index);
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
  extern uint8_t dlnaItemCount(uint8_t bank);
  return dlnaItemCount(snapshot.dynamicBank);
}

inline const char* itemLabel(const MenuSnapshot& snapshot, uint8_t index) {
  if (snapshot.menuId == MenuId::SOURCE) {
    return sourceMenuLabel(sourceItemAt(snapshot.sourceMask, index));
  }
  if (snapshot.menuId != MenuId::DLNA) return itemLabel(snapshot.menuId, index);
  extern const char* dlnaItemLabel(uint8_t bank, uint8_t index);
  return dlnaItemLabel(snapshot.dynamicBank, index);
}

inline bool itemIsActive(const MenuSnapshot& snapshot, uint8_t index) {
  if (snapshot.menuId == MenuId::SOURCE)
    return sourceItemAt(snapshot.sourceMask, index) == snapshot.activeSource;
  return snapshot.menuId == MenuId::SYSTEM && index == 3 &&
         snapshot.encoderUiReversed;
}

inline bool itemHasSubmenu(const MenuSnapshot& snapshot, uint8_t index) {
  if (snapshot.menuId != MenuId::DLNA)
    return itemHasSubmenu(snapshot.menuId, index);
  extern bool dlnaItemFolder(uint8_t bank, uint8_t index);
  return dlnaItemFolder(snapshot.dynamicBank, index);
}

}  // namespace ui_menu
}  // namespace voxone

extern voxone::ui_menu::UiMenu uiMenu;

#endif
