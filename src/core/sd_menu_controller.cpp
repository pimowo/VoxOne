#include "sd_menu_controller.h"

#include <Arduino.h>
#include <cstring>
#include <esp_log.h>

#include "player.h"
#include "sd_card.h"
#include "source_manager.h"
#include "source_media_request.h"
#include "ui_menu.h"
#include "ui_state.h"

namespace voxone {
namespace sd_menu {
namespace {

constexpr uint8_t kMaximumDepth = 8;
constexpr char kSdMenuTag[] = "SdMenu";

sd_card::BrowseListing listing;
ui_menu::DlnaMenuEntry menuEntries[ui_menu::kMaxDlnaMenuItems];
char currentPath[sd_path::kPathCapacity] = "/";
char parentHistory[kMaximumDepth][sd_path::kPathCapacity]{};
uint8_t parentDepth = 0;
char playingPath[sd_path::kPathCapacity]{};

bool load(const char* path) {
  uiMenu.setDlnaStatus("Ładowanie karty SD...");
  const sd_card::Status status = sd_card::browse(path, listing);
  if (status != sd_card::Status::Ok) {
    uiMenu.setDlnaStatus(status == sd_card::Status::NotMounted
                             ? "Brak karty SD"
                             : "Błąd odczytu karty SD");
    ESP_LOGW(kSdMenuTag, "Browse failed: %s", sd_card::statusName(status));
    return false;
  }
  strlcpy(currentPath, path, sizeof(currentPath));
  memset(menuEntries, 0, sizeof(menuEntries));
  uint8_t count = 0;
  for (size_t index = 0;
       index < listing.count && count < ui_menu::kMaxDlnaMenuItems; ++index) {
    const sd_card::BrowseItem& item = listing.items[index];
    ui_menu::DlnaMenuEntry& entry = menuEntries[count++];
    strlcpy(entry.label, item.title, sizeof(entry.label));
    entry.folder = item.folder;
    entry.playable = item.playable;
    entry.playing = item.playable && player.sourceMediaActive() &&
                    sourceManagerActiveSource() == SourceId::Local &&
                    strcmp(playingPath, item.path) == 0;
  }
  if (count) uiMenu.setDlnaItems(menuEntries, count);
  else uiMenu.setDlnaStatus("Folder jest pusty");
  return true;
}

}  // namespace

void entered() {
  if (!sourceManagerSourceAvailable(SourceId::Local)) {
    uiMenu.setDlnaStatus("Karta SD niedostępna");
    return;
  }
  parentDepth = 0;
  strlcpy(currentPath, "/", sizeof(currentPath));
  load("/");
}

void select() {
  if (uiMenu.menuId() != ui_menu::MenuId::SD) return;
  const uint8_t index = uiMenu.selectedIndex();
  if (index >= listing.count) return;
  const sd_card::BrowseItem& item = listing.items[index];
  if (item.folder) {
    if (parentDepth >= kMaximumDepth) {
      uiMenu.setDlnaStatus("Brak miejsca w historii");
      return;
    }
    char previous[sizeof(currentPath)];
    strlcpy(previous, currentPath, sizeof(previous));
    if (load(item.path))
      strlcpy(parentHistory[parentDepth++], previous,
              sizeof(parentHistory[0]));
    return;
  }
  if (!item.playable ||
      sd_card::validatePlayableFile(item.path) != sd_card::Status::Ok) {
    uiMenu.setDlnaStatus("Nieobsługiwany plik");
    return;
  }
  source_media::RequestStorage storage{};
  strlcpy(storage.url, item.path, sizeof(storage.url));
  strlcpy(storage.title, item.title[0] ? item.title : "Karta SD",
          sizeof(storage.title));
  strlcpy(playingPath, item.path, sizeof(playingPath));
  player.requestSourceMedia(storage.view(source_media::Source::Local, true));
  uiMenu.close();
  transitionUiMode(PLAYER);
}

bool back() {
  if (uiMenu.menuId() != ui_menu::MenuId::SD || parentDepth == 0)
    return false;
  const char* parent = parentHistory[parentDepth - 1];
  if (load(parent)) --parentDepth;
  return true;
}

}  // namespace sd_menu
}  // namespace voxone
