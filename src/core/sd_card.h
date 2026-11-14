#ifndef VOXONE_SD_CARD_H
#define VOXONE_SD_CARD_H

#include <stddef.h>
#include <stdint.h>

#include "sd_path.h"

#ifdef ARDUINO
#include <FS.h>
#endif

namespace voxone {
namespace sd_card {

constexpr size_t kMaxBrowseItems = 24;

enum class Status : uint8_t {
  Ok,
  Unsupported,
  NotMounted,
  InvalidPath,
  NotFound,
  NotDirectory,
  Busy,
};

struct BrowseItem {
  bool folder = false;
  bool playable = false;
  char path[sd_path::kPathCapacity]{};
  char parent[sd_path::kPathCapacity]{};
  char title[128]{};
  char mime[32]{};
};

struct BrowseListing {
  char path[sd_path::kPathCapacity]{};
  BrowseItem items[kMaxBrowseItems]{};
  size_t count = 0;
  size_t total = 0;
  bool truncated = false;
};

bool begin();
bool ensureMounted();
bool mounted();
Status browse(const char* path, BrowseListing& listing);
Status validatePlayableFile(const char* path);
const char* statusName(Status status);

#ifdef ARDUINO
fs::FS* filesystem();
#endif

}  // namespace sd_card
}  // namespace voxone

#endif
