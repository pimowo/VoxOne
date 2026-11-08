#ifndef VOXONE_UI_MENU_H
#define VOXONE_UI_MENU_H

#include <atomic>
#include <stdint.h>

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

struct MenuSnapshot {
  MenuState state;
  MenuLevel level;
  MenuId menuId;
  uint8_t selectedIndex;
  uint32_t revision;
};

class UiMenu {
 public:
  static constexpr uint8_t kMainItemCount = 4;

  bool open();
  bool close();
  bool visible() const;
  MenuState state() const;
  MenuLevel level() const;
  MenuId menuId() const;
  uint8_t itemCount() const;
  uint8_t selectedIndex() const;
  const char* selectedLabel() const;
  bool selectedHasSubmenu() const;
  MenuSnapshot snapshot() const;
  MenuAction handle(MenuCommand command, int8_t steps = 1);

 private:
  void markChanged();

  std::atomic<MenuState> state_{MenuState::CLOSED};
  std::atomic<MenuId> menuId_{MenuId::MAIN};
  std::atomic<uint8_t> selectedIndex_{0};
  std::atomic<uint32_t> revision_{0};
};

MenuLevel menuLevel(MenuId menuId);
const char* menuTitle(MenuId menuId);
uint8_t itemCount(MenuId menuId);
const char* itemLabel(MenuId menuId, uint8_t index);
bool itemHasSubmenu(MenuId menuId, uint8_t index);

inline const char* menuTitle(const MenuSnapshot& snapshot) {
  return menuTitle(snapshot.menuId);
}

inline uint8_t itemCount(const MenuSnapshot& snapshot) {
  return itemCount(snapshot.menuId);
}

inline const char* itemLabel(const MenuSnapshot& snapshot, uint8_t index) {
  return itemLabel(snapshot.menuId, index);
}

inline bool itemHasSubmenu(const MenuSnapshot& snapshot, uint8_t index) {
  return itemHasSubmenu(snapshot.menuId, index);
}

}  // namespace ui_menu
}  // namespace voxone

extern voxone::ui_menu::UiMenu uiMenu;

#endif
