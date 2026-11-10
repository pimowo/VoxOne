#include "dlna_menu_controller.h"

#include <Arduino.h>
#include <cstring>

#include "player.h"
#include "temporary_audio_request.h"
#include "ui_menu.h"
#include "ui_state.h"
#include "source_manager.h"
#include "../network/dlna_client.h"

namespace voxone {
namespace dlna_menu {
namespace {

constexpr uint8_t kMaximumDepth = 8;

uint32_t browseJob = 0;
bool browseReady = false;
char currentParent[sizeof(dlna::BrowseItem::id)] = "0";
char parentHistory[kMaximumDepth][sizeof(dlna::BrowseItem::id)]{};
uint8_t parentDepth = 0;
ui_menu::DlnaMenuEntry menuEntries[ui_menu::kMaxDlnaMenuItems];
char playingObjectId[sizeof(dlna::BrowseItem::id)]{};

bool beginBrowse(const char* parent) {
  uint32_t job = 0;
  const dlna::StartResult result = dlna::startBrowse(parent, job);
  if (result != dlna::StartResult::Started) {
    browseJob = 0;
    browseReady = false;
    uiMenu.setDlnaStatus(result == dlna::StartResult::Busy
                             ? "DLNA zajęte"
                             : (result == dlna::StartResult::Offline
                                    ? "Brak sieci"
                                    : "Błąd pamięci"));
    return false;
  }
  strlcpy(currentParent, parent, sizeof(currentParent));
  browseJob = job;
  browseReady = false;
  uiMenu.setDlnaStatus("Ładowanie...");
  return true;
}

}  // namespace

void entered() {
  if (!sourceManagerSourceAvailable(SourceId::Dlna)) {
    uiMenu.setDlnaStatus("DLNA niedostępne");
    return;
  }
  parentDepth = 0;
  currentParent[0] = '0';
  currentParent[1] = '\0';
  beginBrowse("0");
}

void select() {
  if (!browseReady || uiMenu.menuId() != ui_menu::MenuId::DLNA) return;
  dlna::BrowseItem item{};
  if (!dlna::copyBrowseItem(browseJob, uiMenu.selectedIndex(), item)) return;
  if (item.container) {
    if (!dlna::validObjectId(item.id) || parentDepth >= kMaximumDepth) {
      uiMenu.setDlnaStatus("Brak miejsca w historii");
      return;
    }
    char previous[sizeof(currentParent)];
    strlcpy(previous, currentParent, sizeof(previous));
    if (beginBrowse(item.id))
      strlcpy(parentHistory[parentDepth++], previous,
              sizeof(parentHistory[0]));
    return;
  }
  if (!item.resourceUrl[0]) {
    uiMenu.setDlnaStatus("Brak URL utworu");
    return;
  }
  temporary_audio::Request request{};
  strlcpy(request.url, item.resourceUrl, sizeof(request.url));
  strlcpy(request.title, item.title[0] ? item.title : "DLNA",
          sizeof(request.title));
  request.source = temporary_audio::Source::Dlna;
  strlcpy(playingObjectId, item.id, sizeof(playingObjectId));
  player.requestTemporaryUrl(request);
  uiMenu.close();
  transitionUiMode(PLAYER);
}

bool back() {
  if (uiMenu.menuId() != ui_menu::MenuId::DLNA || parentDepth == 0)
    return false;
  const char* parent = parentHistory[parentDepth - 1];
  if (beginBrowse(parent)) --parentDepth;
  return true;
}

void service() {
  if (!browseJob || browseReady) return;
  int status = 0;
  bool ready = false;
  size_t count = 0;
  if (!dlna::pollBrowseItems(browseJob, status, ready, count)) {
    browseJob = 0;
    uiMenu.setDlnaStatus("Zadanie DLNA wygasło");
    return;
  }
  if (!ready) return;
  if (status != 200) {
    browseJob = 0;
    browseReady = false;
    uiMenu.setDlnaStatus(status == 504 ? "Serwer DLNA niedostępny"
                                       : "Błąd przeglądania DLNA");
    return;
  }
  uint8_t copied = 0;
  for (size_t index = 0;
       index < count && copied < ui_menu::kMaxDlnaMenuItems; ++index) {
    dlna::BrowseItem item{};
    if (!dlna::copyBrowseItem(browseJob, index, item)) break;
    ui_menu::DlnaMenuEntry& entry = menuEntries[copied++];
    entry = ui_menu::DlnaMenuEntry{};
    strlcpy(entry.label,
            item.title[0] ? item.title
                          : (item.container ? "Folder" : "Utwór"),
            sizeof(entry.label));
    entry.folder = item.container;
    entry.playable = !item.container && item.resourceUrl[0];
    entry.playing = entry.playable && playingObjectId[0] &&
        sourceManagerActiveSource() == SourceId::Dlna &&
        strcmp(playingObjectId, item.id) == 0;
  }
  browseReady = true;
  if (copied) uiMenu.setDlnaItems(menuEntries, copied);
  else uiMenu.setDlnaStatus("Folder jest pusty");
}

}  // namespace dlna_menu
}  // namespace voxone
