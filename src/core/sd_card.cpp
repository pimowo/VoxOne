#include "sd_card.h"
#include "options.h"

#if VOXONE_HAS_SD

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <freertos/semphr.h>

#include "../hardware/hardware_descriptor.h"

namespace voxone {
namespace sd_card {
namespace {

constexpr uint32_t kSdSpiFrequency = 4000000UL;
constexpr uint32_t kSdFallbackSpiFrequency = 1000000UL;
SemaphoreHandle_t sdMutex = nullptr;
bool sdMounted = false;
uint32_t sdFrequency = 0;

class Lock {
 public:
  explicit Lock(TickType_t wait = pdMS_TO_TICKS(750))
      : locked_(sdMutex && xSemaphoreTakeRecursive(sdMutex, wait) == pdTRUE) {}
  ~Lock() { if (locked_) xSemaphoreGiveRecursive(sdMutex); }
  explicit operator bool() const { return locked_; }
 private:
  bool locked_;
};

bool supported() {
  const auto& descriptor = hardware::currentHardware();
  return descriptor.capabilities.supportsSdCard &&
         hardware::hasPin(descriptor.sdCard.cs) &&
         hardware::hasPin(descriptor.sdCard.spi.sck) &&
         hardware::hasPin(descriptor.sdCard.spi.mosi) &&
         hardware::hasPin(descriptor.sdCard.spi.miso);
}

bool mountLocked() {
  if (!supported()) return false;
  if (sdMounted && SD.cardType() != CARD_NONE) return true;
  if (sdMounted) SD.end();
  const auto& pins = hardware::currentHardware().sdCard;
  const auto& display = hardware::currentHardware().display;
  if (hardware::hasPin(display.cs)) {
    pinMode(static_cast<uint8_t>(display.cs), OUTPUT);
    digitalWrite(static_cast<uint8_t>(display.cs), HIGH);
  }
  pinMode(static_cast<uint8_t>(pins.cs), OUTPUT);
  digitalWrite(static_cast<uint8_t>(pins.cs), HIGH);
  SPI.begin(static_cast<int8_t>(pins.spi.sck),
            static_cast<int8_t>(pins.spi.miso),
            static_cast<int8_t>(pins.spi.mosi),
            static_cast<int8_t>(pins.cs));
  sdMounted = SD.begin(static_cast<uint8_t>(pins.cs), SPI, kSdSpiFrequency) &&
              SD.cardType() != CARD_NONE;
  sdFrequency = sdMounted ? kSdSpiFrequency : 0;
  if (!sdMounted) {
    SD.end();
    delay(20);
    digitalWrite(static_cast<uint8_t>(pins.cs), HIGH);
    sdMounted = SD.begin(static_cast<uint8_t>(pins.cs), SPI,
                         kSdFallbackSpiFrequency) &&
                SD.cardType() != CARD_NONE;
    sdFrequency = sdMounted ? kSdFallbackSpiFrequency : 0;
  }
  return sdMounted;
}

const char* baseName(const char* path) {
  const char* slash = path ? strrchr(path, '/') : nullptr;
  return slash && slash[1] ? slash + 1 : (path ? path : "");
}

bool childPath(const char* parent, const char* child, char* output,
               size_t capacity) {
  if (!parent || !child || !output || !capacity) return false;
  const char* name = baseName(child);
  if (!name[0] || strchr(name, '/')) return false;
  const int written = strcmp(parent, "/") == 0
                          ? snprintf(output, capacity, "/%s", name)
                          : snprintf(output, capacity, "%s/%s", parent, name);
  return written > 0 && static_cast<size_t>(written) < capacity &&
         sd_path::valid(output);
}

}  // namespace

bool begin() {
  if (!supported()) return false;
  if (!sdMutex) sdMutex = xSemaphoreCreateRecursiveMutex();
  Lock lock;
  if (!lock) return false;
  const bool ready = mountLocked();
  Serial.printf("##[SD]# %s cs=%d sck=%d mosi=%d miso=%d frequency=%lu\n",
                ready ? "mounted" : "not mounted",
                static_cast<int>(hardware::currentHardware().sdCard.cs),
                static_cast<int>(hardware::currentHardware().sdCard.spi.sck),
                static_cast<int>(hardware::currentHardware().sdCard.spi.mosi),
                static_cast<int>(hardware::currentHardware().sdCard.spi.miso),
                static_cast<unsigned long>(sdFrequency));
  return ready;
}

bool ensureMounted() {
  if (!supported()) return false;
  if (!sdMutex) sdMutex = xSemaphoreCreateRecursiveMutex();
  Lock lock;
  return lock && mountLocked();
}

bool mounted() {
  if (!supported() || !sdMutex) return false;
  Lock lock(pdMS_TO_TICKS(100));
  if (!lock) return false;
  if (!sdMounted || SD.cardType() == CARD_NONE) sdMounted = false;
  return sdMounted;
}

Status browse(const char* path, BrowseListing& listing) {
  memset(&listing, 0, sizeof(listing));
  if (!supported()) return Status::Unsupported;
  if (!sd_path::valid(path)) return Status::InvalidPath;
  if (!ensureMounted()) return Status::NotMounted;
  Lock lock;
  if (!lock) return Status::Busy;
  File directory = SD.open(path);
  if (!directory) return Status::NotFound;
  if (!directory.isDirectory()) {
    directory.close();
    return Status::NotDirectory;
  }
  strlcpy(listing.path, path, sizeof(listing.path));
  for (File item = directory.openNextFile(); item;
       item = directory.openNextFile()) {
    char itemPath[sd_path::kPathCapacity]{};
    if (!childPath(path, item.name(), itemPath, sizeof(itemPath))) {
      item.close();
      continue;
    }
    ++listing.total;
    if (listing.count >= kMaxBrowseItems) {
      listing.truncated = true;
      item.close();
      continue;
    }
    BrowseItem& output = listing.items[listing.count++];
    output.folder = item.isDirectory();
    output.playable = !output.folder && sd_path::audioFile(itemPath);
    strlcpy(output.path, itemPath, sizeof(output.path));
    strlcpy(output.parent, path, sizeof(output.parent));
    strlcpy(output.title, baseName(itemPath), sizeof(output.title));
    strlcpy(output.mime, output.folder ? "" : sd_path::mimeType(itemPath),
            sizeof(output.mime));
    item.close();
  }
  directory.close();
  return Status::Ok;
}

Status validatePlayableFile(const char* path) {
  if (!supported()) return Status::Unsupported;
  if (!sd_path::valid(path) || !sd_path::audioFile(path))
    return Status::InvalidPath;
  if (!ensureMounted()) return Status::NotMounted;
  Lock lock;
  if (!lock) return Status::Busy;
  File file = SD.open(path, FILE_READ);
  if (!file) return Status::NotFound;
  const bool regular = !file.isDirectory();
  file.close();
  return regular ? Status::Ok : Status::InvalidPath;
}

const char* statusName(Status status) {
  switch (status) {
    case Status::Ok: return "ok";
    case Status::Unsupported: return "sd_unsupported";
    case Status::NotMounted: return "sd_not_mounted";
    case Status::InvalidPath: return "invalid_sd_path";
    case Status::NotFound: return "sd_path_not_found";
    case Status::NotDirectory: return "sd_path_not_directory";
    case Status::Busy: return "sd_busy";
  }
  return "sd_error";
}

fs::FS* filesystem() { return supported() ? static_cast<fs::FS*>(&SD) : nullptr; }

}  // namespace sd_card
}  // namespace voxone

#else

namespace voxone {
namespace sd_card {

bool begin() { return false; }
bool ensureMounted() { return false; }
bool mounted() { return false; }
Status browse(const char*, BrowseListing& listing) {
  memset(&listing, 0, sizeof(listing));
  return Status::Unsupported;
}
Status validatePlayableFile(const char*) { return Status::Unsupported; }
const char* statusName(Status status) {
  return status == Status::Unsupported ? "sd_unsupported" : "sd_error";
}
fs::FS* filesystem() { return nullptr; }

}  // namespace sd_card
}  // namespace voxone

#endif
