#include "../src/core/ui_menu.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

using voxone::ui_menu::MenuAction;
using voxone::ui_menu::MenuCommand;
using voxone::ui_menu::MenuId;
using voxone::ui_menu::MenuLevel;
using voxone::ui_menu::MenuRenderKind;
using voxone::ui_menu::MenuState;
using voxone::ui_menu::UiMenu;

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void expectSubmenu(UiMenu& menu, MenuId expectedId,
                   const char* expectedTitle, uint8_t parentIndex) {
  assert(menu.selectedIndex() == parentIndex);
  assert(menu.selectedHasSubmenu());
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::ENTERED_SUBMENU);
  assert(menu.level() == MenuLevel::SUBMENU);
  assert(menu.menuId() == expectedId);
  assert(std::string(voxone::ui_menu::menuTitle(menu.menuId())) ==
         expectedTitle);
  assert(menu.itemCount() == 3);
  assert(menu.selectedIndex() == 0);
  assert(!menu.selectedHasSubmenu());
  assert(menu.handle(MenuCommand::BACK) == MenuAction::BACKED);
  assert(menu.level() == MenuLevel::MAIN);
  assert(menu.menuId() == MenuId::MAIN);
  assert(menu.selectedIndex() == parentIndex);
}
}  // namespace

int main() {
  UiMenu menu;
  assert(menu.state() == MenuState::CLOSED);
  assert(!menu.visible());
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NONE);

  assert(voxone::ui_menu::itemCount(MenuId::MAIN) ==
         UiMenu::kMainItemCount);
  assert(std::string(voxone::ui_menu::itemLabel(MenuId::MAIN, 0)) ==
         "Audio");
  assert(std::string(voxone::ui_menu::itemLabel(MenuId::MAIN, 1)) ==
         "Wyświetlacz");
  assert(std::string(voxone::ui_menu::itemLabel(MenuId::MAIN, 2)) ==
         "Sieć");
  assert(std::string(voxone::ui_menu::itemLabel(MenuId::MAIN, 3)) ==
         "System");
  assert(std::string(voxone::ui_menu::itemLabel(MenuId::MAIN, 4)).empty());
  for (uint8_t index = 0; index < UiMenu::kMainItemCount; ++index)
    assert(voxone::ui_menu::itemHasSubmenu(MenuId::MAIN, index));
  assert(std::string(voxone::ui_menu::menuContextHeader(MenuId::MAIN)) ==
         "MENU");
  assert(std::string(voxone::ui_menu::menuContextHeader(MenuId::AUDIO)) ==
         "AUDIO");
  assert(std::string(voxone::ui_menu::menuContextHeader(MenuId::SCREEN)) ==
         "WYŚWIETLACZ");

  assert(menu.open());
  assert(!menu.open());
  assert(menu.state() == MenuState::OPEN);
  assert(menu.level() == MenuLevel::MAIN);
  assert(menu.menuId() == MenuId::MAIN);
  assert(menu.visible());
  assert(menu.selectedIndex() == 0);
  const uint32_t openedRevision = menu.snapshot().revision;

  // Enter Audio, navigate its leaves, select one and return to the parent.
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::ENTERED_SUBMENU);
  assert(menu.menuId() == MenuId::AUDIO);
  assert(menu.level() == MenuLevel::SUBMENU);
  assert(menu.itemCount() == 3);
  assert(std::string(menu.selectedLabel()) == "Balans");
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(std::string(menu.selectedLabel()) == "Bass");
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::SELECTED);
  assert(menu.state() == MenuState::EDITING);
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(menu.state() == MenuState::OPEN);
  assert(std::string(menu.selectedLabel()) == "Treble");
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(menu.selectedIndex() == 0);
  assert(menu.handle(MenuCommand::PREVIOUS) == MenuAction::NAVIGATED);
  assert(menu.selectedIndex() == 2);
  assert(menu.handle(MenuCommand::BACK) == MenuAction::BACKED);
  assert(menu.menuId() == MenuId::MAIN);
  assert(menu.selectedIndex() == 0);
  assert(menu.snapshot().revision > openedRevision);

  // Encoder navigation repaints only the old and new selection. Entering a
  // different branch requires a complete menu-layout refresh.
  const voxone::ui_menu::MenuSnapshot firstPaint{
      MenuState::OPEN, MenuLevel::MAIN, MenuId::MAIN, 0, 1};
  const voxone::ui_menu::MenuSnapshot nextSelection{
      MenuState::OPEN, MenuLevel::MAIN, MenuId::MAIN, 1, 2};
  auto renderPlan = voxone::ui_menu::menuRenderPlan(
      firstPaint, nextSelection, true);
  assert(renderPlan.kind == MenuRenderKind::SELECTION);
  assert(renderPlan.previousIndex == 0);
  assert(renderPlan.selectedIndex == 1);
  renderPlan = voxone::ui_menu::menuRenderPlan(
      nextSelection, nextSelection, true);
  assert(renderPlan.kind == MenuRenderKind::NONE);
  const voxone::ui_menu::MenuSnapshot audioMenu{
      MenuState::OPEN, MenuLevel::SUBMENU, MenuId::AUDIO, 0, 3};
  assert(voxone::ui_menu::menuRenderPlan(
             nextSelection, audioMenu, true).kind == MenuRenderKind::FULL);
  assert(voxone::ui_menu::menuRenderPlan(
             firstPaint, firstPaint, false).kind == MenuRenderKind::FULL);

  // Every root item is a real branch, and Back restores its parent selection.
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  expectSubmenu(menu, MenuId::SCREEN, "Wyświetlacz", 1);
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  expectSubmenu(menu, MenuId::NETWORK, "Sieć", 2);
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  expectSubmenu(menu, MenuId::SYSTEM, "System", 3);
  assert(menu.handle(MenuCommand::BACK) == MenuAction::NONE);

  assert(menu.handle(MenuCommand::CLOSE) == MenuAction::CLOSED);
  assert(menu.state() == MenuState::CLOSED);
  assert(!menu.close());
  assert(menu.open());
  assert(menu.menuId() == MenuId::MAIN);
  assert(menu.selectedIndex() == 0);

  // Menu input is consumed before normal volume/source actions. Double click
  // navigates to the parent; long press closes the menu.
  const std::string input = readFile("src/core/ui_input.cpp");
  const size_t menuDispatch = input.find("dispatchMenuInput(event, steps)");
  const size_t normalDispatch = input.find(
      "resolveUiInput(currentUiInputContext(), event)");
  assert(menuDispatch != std::string::npos);
  assert(normalDispatch != std::string::npos);
  assert(menuDispatch < normalDispatch);
  assert(input.find("MenuCommand::PREVIOUS") != std::string::npos);
  assert(input.find("MenuCommand::NEXT") != std::string::npos);
  assert(input.find("MenuCommand::SELECT") != std::string::npos);
  assert(input.find("UiInputEvent::Double") != std::string::npos);
  assert(input.find("MenuCommand::BACK") != std::string::npos);
  assert(input.find("MenuCommand::CLOSE") != std::string::npos);

  const std::string power = readFile("src/core/power_manager.cpp");
  assert(power.find("ButtonEvent::BUTTON_DOUBLE_CLICK") != std::string::npos);
  assert(power.find("uiMenu.open()") != std::string::npos);
  assert(power.find("MenuCommand::BACK") != std::string::npos);
  assert(power.find("transitionUiMode(MENU)") != std::string::npos);
  assert(power.find("Selected: %s") != std::string::npos);

  const std::string display = readFile("src/core/display.cpp");
  assert(display.find("class UiMenuWidget") != std::string::npos);
  assert(display.find("_pager->setPage(_menuPage, true)") !=
         std::string::npos);
  assert(display.find("itemHasSubmenu(menu, index)") != std::string::npos);
  assert(display.find("dsp.print(\">") != std::string::npos);
  const size_t menuWidgetStart = display.find("class UiMenuWidget");
  const size_t menuWidgetEnd = display.find(
      "#if VOXONE_DISPLAY_COMPILES_SMALL_OLED", menuWidgetStart);
  assert(menuWidgetStart != std::string::npos);
  assert(menuWidgetEnd != std::string::npos);
  const std::string menuRenderer = display.substr(
      menuWidgetStart, menuWidgetEnd - menuWidgetStart);
  assert(menuRenderer.find("dsp.fillScreen") == std::string::npos);
  assert(menuRenderer.find("MenuRenderKind::SELECTION") !=
         std::string::npos);
  assert(menuRenderer.find("menuContextHeader(menu.menuId)") !=
         std::string::npos);
  return 0;
}
