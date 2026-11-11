#include "../src/core/ui_menu.h"

#include <cassert>
#include <cstring>
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
                   const char* expectedTitle, uint8_t parentIndex,
                   uint8_t expectedItems = 3) {
  assert(menu.selectedIndex() == parentIndex);
  assert(menu.selectedHasSubmenu());
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::ENTERED_SUBMENU);
  assert(menu.level() == MenuLevel::SUBMENU);
  assert(menu.menuId() == expectedId);
  assert(std::string(voxone::ui_menu::menuTitle(menu.menuId())) ==
         expectedTitle);
  assert(menu.itemCount() == expectedItems);
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
  assert(std::string(voxone::ui_menu::itemLabel(MenuId::MAIN, 4)) == "DLNA");
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
  voxone::ui_menu::MenuSettings persisted{};
  persisted.balance = -2;
  persisted.bass = 0;
  persisted.treble = 3;
  persisted.brightness = 75;
  persisted.tftTheme = 0;
  persisted.screensaverEnabled = true;
  persisted.encoderUiReversed = false;
  menu.beginSettingsTransaction(persisted);
  assert(!menu.settingsDirty());
  const uint32_t openedRevision = menu.snapshot().revision;

  // Source entries are capability-filtered and open on the active source.
  const uint8_t allSources = kCoreSourceMask |
      sourceBit(SourceId::Bluetooth) | sourceBit(SourceId::HomeAssistant) |
      sourceBit(SourceId::Dlna) | sourceBit(SourceId::Aux);
  UiMenu sourceMenu;
  assert(sourceMenu.open(MenuId::SOURCE));
  sourceMenu.configureSources(allSources, SourceId::Dlna);
  assert(sourceMenu.itemCount() == 5 && sourceMenu.selectedIndex() == 3);
  assert(std::string(sourceMenu.selectedLabel()) == "DLNA");
  assert(voxone::ui_menu::itemIsActive(sourceMenu.snapshot(), 3));
  assert(sourceMenu.handle(MenuCommand::SELECT) == MenuAction::SELECTED);
  sourceMenu.configureSources(
      kCoreSourceMask | sourceBit(SourceId::HomeAssistant) |
          sourceBit(SourceId::Dlna),
      SourceId::Web);
  assert(sourceMenu.itemCount() == 3);
  assert(std::string(voxone::ui_menu::itemLabel(sourceMenu.snapshot(), 0)) ==
         "WEB");
  assert(std::string(voxone::ui_menu::itemLabel(sourceMenu.snapshot(), 1)) ==
         "HA");
  assert(std::string(voxone::ui_menu::itemLabel(sourceMenu.snapshot(), 2)) ==
         "DLNA");

  // Enter Audio, navigate its leaves, select one and return to the parent.
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::ENTERED_SUBMENU);
  assert(menu.menuId() == MenuId::AUDIO);
  assert(menu.level() == MenuLevel::SUBMENU);
  assert(menu.itemCount() == 5);
  assert(std::string(menu.selectedLabel()) == "Balans");
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(std::string(menu.selectedLabel()) == "Bass");
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::EDITING_STARTED);
  assert(menu.state() == MenuState::EDITING);
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::SETTING_CHANGED);
  assert(menu.state() == MenuState::EDITING);
  assert(menu.settings().bass == 1);
  assert(menu.settingsDirty());
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::EDITING_FINISHED);
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(menu.state() == MenuState::OPEN);
  assert(std::string(menu.selectedLabel()) == "Treble");
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(std::string(menu.selectedLabel()) == "LINE OUT");
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::EDITING_STARTED);
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::SETTING_CHANGED);
  assert(menu.settings().lineOutEnabled);
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::EDITING_FINISHED);
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(std::string(menu.selectedLabel()) == "Poziom LINE");
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::EDITING_STARTED);
  assert(menu.handle(MenuCommand::PREVIOUS) == MenuAction::SETTING_CHANGED);
  assert(menu.settings().lineOutVolume == 95);
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::EDITING_FINISHED);
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(menu.selectedIndex() == 0);
  assert(menu.handle(MenuCommand::PREVIOUS) == MenuAction::NAVIGATED);
  assert(menu.selectedIndex() == 4);
  assert(menu.handle(MenuCommand::BACK) == MenuAction::BACKED);
  assert(menu.menuId() == MenuId::MAIN);
  assert(menu.selectedIndex() == 0);
  assert(menu.snapshot().revision > openedRevision);

  // Encoder navigation repaints only the old and new selection. Entering a
  // different branch requires a complete menu-layout refresh.
  const voxone::ui_menu::MenuSnapshot firstPaint{
      MenuState::OPEN, MenuLevel::MAIN, MenuId::MAIN, 0, 1, 0};
  const voxone::ui_menu::MenuSnapshot nextSelection{
      MenuState::OPEN, MenuLevel::MAIN, MenuId::MAIN, 1, 2, 0};
  auto renderPlan = voxone::ui_menu::menuRenderPlan(
      firstPaint, nextSelection, true);
  assert(renderPlan.kind == MenuRenderKind::SELECTION);
  assert(renderPlan.previousIndex == 0);
  assert(renderPlan.selectedIndex == 1);
  renderPlan = voxone::ui_menu::menuRenderPlan(
      nextSelection, nextSelection, true);
  assert(renderPlan.kind == MenuRenderKind::NONE);
  const voxone::ui_menu::MenuSnapshot audioMenu{
      MenuState::OPEN, MenuLevel::SUBMENU, MenuId::AUDIO, 0, 3, 0};
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
  expectSubmenu(menu, MenuId::SYSTEM, "System", 3, 4);
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(menu.selectedIndex() == 4);
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::ENTERED_SUBMENU);
  assert(menu.menuId() == MenuId::DLNA);
  voxone::ui_menu::DlnaMenuEntry dlnaItems[3]{};
  std::strcpy(dlnaItems[0].label, "Albumy");
  dlnaItems[0].folder = true;
  std::strcpy(dlnaItems[1].label, "Koncert.flac");
  dlnaItems[1].playable = true;
  dlnaItems[1].playing = true;
  std::strcpy(dlnaItems[2].label, "Brak URL");
  menu.setDlnaItems(dlnaItems, 3);
  assert(menu.itemCount() == 3);
  assert(std::string(menu.selectedLabel()) == "Albumy");
  assert(menu.selectedDlnaFolder() && !menu.selectedDlnaPlayable());
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::SELECTED);
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(!menu.selectedDlnaFolder() && menu.selectedDlnaPlayable());
  assert(voxone::ui_menu::itemIsActive(menu.snapshot(), 1));
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::SELECTED);
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::NONE);
  const auto dlnaSnapshot = menu.snapshot();
  assert(voxone::ui_menu::itemCount(dlnaSnapshot) == 3);
  assert(std::string(voxone::ui_menu::itemLabel(dlnaSnapshot, 0)) == "Albumy");
  assert(voxone::ui_menu::itemHasSubmenu(dlnaSnapshot, 0));
  voxone::ui_menu::DlnaMenuEntry replacement{};
  std::strcpy(replacement.label, "Nowa lista");
  replacement.playable = true;
  menu.setDlnaItems(&replacement, 1);
  assert(voxone::ui_menu::itemCount(dlnaSnapshot) == 3);
  assert(std::string(voxone::ui_menu::itemLabel(dlnaSnapshot, 1)) ==
         "Koncert.flac");
  assert(voxone::ui_menu::itemCount(menu.snapshot()) == 1);
  assert(menu.handle(MenuCommand::BACK) == MenuAction::BACKED);
  assert(menu.menuId() == MenuId::MAIN && menu.selectedIndex() == 4);
  assert(menu.handle(MenuCommand::BACK) == MenuAction::NONE);

  // A modified draft cannot close directly. The centered decision screen
  // defaults to TAK, allows encoder selection and reports the requested
  // transaction outcome without closing before persistence is resolved.
  assert(menu.handle(MenuCommand::CLOSE) ==
         MenuAction::CONFIRMATION_REQUIRED);
  assert(menu.state() == MenuState::CONFIRM_SAVE);
  assert(menu.menuId() == MenuId::SAVE_CONFIRM);
  assert(std::string(voxone::ui_menu::menuTitle(menu.snapshot())) ==
         "Zapisać zmiany?");
  assert(menu.itemCount() == 2);
  assert(menu.selectedIndex() == 0);
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::SAVE_REQUESTED);
  assert(menu.visible());
  assert(menu.handle(MenuCommand::NEXT) == MenuAction::NAVIGATED);
  assert(menu.selectedIndex() == 1);
  assert(menu.handle(MenuCommand::SELECT) == MenuAction::DISCARD_REQUESTED);
  assert(menu.finishTransaction());
  assert(menu.state() == MenuState::CLOSED);
  assert(!menu.close());
  assert(menu.open());
  menu.beginSettingsTransaction(persisted);
  assert(menu.menuId() == MenuId::MAIN);
  assert(menu.selectedIndex() == 0);
  assert(menu.handle(MenuCommand::CLOSE) == MenuAction::CLOSED);
  assert(menu.open(MenuId::SYSTEM));
  menu.beginSettingsTransaction(persisted);
  assert(menu.menuId() == MenuId::SYSTEM);
  assert(menu.handle(MenuCommand::PREVIOUS) == MenuAction::NAVIGATED);
  assert(menu.selectedSetting() ==
         voxone::ui_menu::MenuSetting::ENCODER_UI_REVERSED);
  assert(!voxone::ui_menu::itemIsActive(menu.snapshot(), 3));
  menu.configureEncoderUiReversed(true);
  assert(voxone::ui_menu::itemIsActive(menu.snapshot(), 3));

  // Values are exposed to both TFT and OLED renderers without changing the
  // static labels used for navigation.
  char value[16];
  const auto systemSnapshot = menu.snapshot();
  assert(voxone::ui_menu::formatItemValue(
      systemSnapshot, 3, value, sizeof(value)));
  assert(std::string(value) == "TAK");

  // Menu input is consumed before normal volume/source actions. Double click
  // navigates to the parent; long press closes the menu.
  const std::string input = readFile("src/core/ui_input.cpp");
  const size_t menuDispatch = input.find("dispatchMenuInput(event, steps)");
  const size_t normalDispatch = input.find(
      "resolveUiInput(currentUiInputContext(), event, control)");
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
  assert(power.find("dispatchUiInput(UiInputEvent::Double)") !=
         std::string::npos);
  assert(power.find("uiCarouselActive()") != std::string::npos);
  assert(power.find("cancelUiToPlayer()") != std::string::npos);
  const size_t longPress =
      power.find("ButtonEvent::BUTTON_LONG_PRESS");
  const size_t longPressEnd = power.find("if (shutdownNotice_", longPress);
  assert(longPress != std::string::npos && longPressEnd != std::string::npos);
  const std::string longPressHandler =
      power.substr(longPress, longPressEnd - longPress);
  assert(longPressHandler.find("uiState.mode() == PLAYER") !=
         std::string::npos);
  assert(longPressHandler.find("dispatchUiInput(UiInputEvent::Long)") !=
         std::string::npos);
  assert(input.find("case UiInputAction::OpenMenu:") != std::string::npos);
  assert(input.find("uiMenu.open(voxone::ui_menu::MenuId::MAIN)") !=
         std::string::npos);
  assert(input.find("config.beginMenuTransaction()") != std::string::npos);
  assert(input.find("transitionUiMode(MENU)") != std::string::npos);

  const std::string display = readFile("src/core/display.cpp");
  const std::string carousel =
      readFile("src/core/unified_carousel_renderer.cpp");
  assert(display.find("class UiMenuWidget") != std::string::npos);
  assert(display.find("_pager->setPage(_menuPage, true)") !=
         std::string::npos);
  assert(display.find("UnifiedCarouselRenderer renderer_") !=
         std::string::npos);
  assert(display.find("_stationCarousel->setModel(carousel)") !=
         std::string::npos);
  assert(display.find("menuContextHeader(menu.menuId)") !=
         std::string::npos);
  assert(display.find("DLNA - FOLDER") != std::string::npos);
  assert(display.find("ŹRÓDŁA") != std::string::npos);
  assert(display.find("Zapisać zmiany?") != std::string::npos);
  assert(display.find("formatItemValue") != std::string::npos);
  assert(carousel.find("voxone::utf8::ellipsize") != std::string::npos);
  assert(carousel.find("model.playing") != std::string::npos);
  assert(carousel.find("\"GRA\"") != std::string::npos);
  assert(carousel.find("dsp.height() <= 64") != std::string::npos);
  assert(carousel.find("printOledListRow") != std::string::npos);
  assert(carousel.find("model.total ? model.item : model.emptyText") !=
         std::string::npos);

  assert(input.find("config.previewMenuSettings(uiMenu.settings())") !=
         std::string::npos);
  assert(input.find("config.commitMenuTransaction()") != std::string::npos);
  assert(input.find("config.discardMenuTransaction()") != std::string::npos);
  return 0;
}
