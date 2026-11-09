#include "source_menu_controller.h"

#include "source_manager.h"
#include "ui_menu.h"
#include "ui_state.h"

namespace voxone {
namespace source_menu {

void entered() {
  uiMenu.configureSources(sourceManagerSourceMask(),
                          sourceManagerActiveSource());
}

void select() {
  if (uiMenu.menuId() != ui_menu::MenuId::SOURCE) return;
  sourceManagerSelectSource(uiMenu.selectedSource());
  uiMenu.close();
  transitionUiMode(PLAYER);
}

}  // namespace source_menu
}  // namespace voxone
