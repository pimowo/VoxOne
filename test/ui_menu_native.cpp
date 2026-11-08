#include "../src/core/ui_menu.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

using voxone::ui_menu::MenuAction;
using voxone::ui_menu::MenuCommand;
using voxone::ui_menu::MenuState;
using voxone::ui_menu::UiMenu;

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}
}  // namespace

int main() {
  UiMenu menu;
  assert(menu.state() == MenuState::CLOSED);
  assert(!menu.visible());
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NONE);

  for (uint8_t index = 0; index < UiMenu::kItemCount; ++index)
    assert(voxone::ui_menu::itemLabel(index)[0] != '\0');
  assert(std::string(voxone::ui_menu::itemLabel(0)) == "Polaczenie");
  assert(std::string(voxone::ui_menu::itemLabel(1)) == "Audio");
  assert(std::string(voxone::ui_menu::itemLabel(2)) == "Ekran");
  assert(std::string(voxone::ui_menu::itemLabel(3)) == "System");
  assert(std::string(voxone::ui_menu::itemLabel(4)).empty());

  assert(menu.open());
  assert(!menu.open());
  assert(menu.state() == MenuState::OPEN);
  assert(menu.visible());
  assert(menu.selectedIndex() == 0);
  const uint32_t openedRevision = menu.snapshot().revision;

  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(menu.selectedIndex() == 1);
  assert(std::string(menu.selectedLabel()) == "Audio");
  assert(menu.handle(MenuCommand::NEXT, 2) == MenuAction::NAVIGATED);
  assert(menu.selectedIndex() == 3);
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(menu.selectedIndex() == 0);
  assert(menu.handle(MenuCommand::PREVIOUS) == MenuAction::NAVIGATED);
  assert(menu.selectedIndex() == 3);
  assert(menu.handle(MenuCommand::PREVIOUS, -2) == MenuAction::NAVIGATED);
  assert(menu.selectedIndex() == 1);
  assert(menu.snapshot().revision > openedRevision);

  assert(menu.handle(MenuCommand::SELECT) == MenuAction::SELECTED);
  assert(menu.state() == MenuState::EDITING);
  assert(std::string(menu.selectedLabel()) == "Audio");
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(menu.selectedIndex() == 2);
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::SELECTED);
  assert(menu.state() == MenuState::OPEN);

  assert(menu.handle(MenuCommand::CLOSE) == MenuAction::CLOSED);
  assert(menu.state() == MenuState::CLOSED);
  assert(!menu.close());
  assert(menu.open());
  assert(menu.selectedIndex() == 0);

  // The integration consumes menu input before normal volume/source actions.
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
  assert(input.find("MenuCommand::CLOSE") != std::string::npos);

  const std::string power = readFile("src/core/power_manager.cpp");
  assert(power.find("ButtonEvent::BUTTON_DOUBLE_CLICK") != std::string::npos);
  assert(power.find("uiMenu.open()") != std::string::npos);
  assert(power.find("transitionUiMode(MENU)") != std::string::npos);
  assert(power.find("Selected: %s") != std::string::npos);

  const std::string display = readFile("src/core/display.cpp");
  assert(display.find("class UiMenuWidget") != std::string::npos);
  assert(display.find("_pager->setPage(_menuPage, true)") !=
         std::string::npos);
  assert(display.find("dsp.drawRect(horizontalMargin, top") !=
         std::string::npos);
  return 0;
}
