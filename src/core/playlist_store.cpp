#include "options.h"
#include "playlist_store.h"
#include "playlist_validation.h"

#include <SPIFFS.h>
#include <freertos/semphr.h>
#include <limits.h>
#include "config.h"

namespace {
constexpr char kCsvTmp[] = "/data/playlist.tmp";
constexpr char kCsvBak[] = "/data/playlist.bak";
constexpr char kIndexTmp[] = "/data/index.tmp";
constexpr char kIndexBak[] = "/data/index.bak";
SemaphoreHandle_t playlistMutex = nullptr;

bool removeIfPresent(const char* path) {
  return !SPIFFS.exists(path) || SPIFFS.remove(path);
}

String revisionOf(uint32_t crc) {
  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%08lX", static_cast<unsigned long>(crc ^ 0xFFFFFFFFUL));
  return String(buffer);
}

bool parseLine(char* line, PlaylistRow& row) {
  const size_t length = strlen(line);
  if (length && line[length - 1] == '\r') line[length - 1] = '\0';
  char* first = strchr(line, '\t');
  char* second = first ? strchr(first + 1, '\t') : nullptr;
  if (!first || !second || strchr(second + 1, '\t')) return false;
  *first = *second = '\0';
  row.name = line;
  row.url = first + 1;
  return PlaylistStore::parseInteger(second + 1, row.ovol) &&
         PlaylistStore::validRecord(row);
}

bool readRows(const char* path, std::vector<PlaylistRow>& rows, String& revision,
              std::vector<uint32_t>* offsets = nullptr, bool* ioError = nullptr) {
  File file = SPIFFS.open(path, "r");
  if (!file) {
    if (ioError) *ioError = true;
    return false;
  }
  rows.clear();
  if (offsets) offsets->clear();
  char line[BUFLEN * 3];
  size_t length = 0;
  uint32_t position = 0, start = 0, crc = 0xFFFFFFFFUL;
  const size_t fileSize = file.size();
  while (position < fileSize) {
    const int value = file.read();
    if (value < 0) {
      if (ioError) *ioError = true;
      return false;
    }
    crc = playlistCrcByte(crc, static_cast<uint8_t>(value));
    ++position;
    if (value == '\n') {
      if (!length || rows.size() >= UINT16_MAX) return false;
      line[length] = '\0';
      PlaylistRow row;
      if (strlen(line) != length) return false;
      if (!parseLine(line, row)) return false;
      if (offsets) offsets->push_back(start);
      rows.push_back(row);
      length = 0;
      start = position;
    } else {
      if (length >= sizeof(line) - 1) return false;
      line[length++] = static_cast<char>(value);
    }
  }
  if (length) {
    line[length] = '\0';
    PlaylistRow row;
    if (strlen(line) != length) return false;
    if (rows.size() >= UINT16_MAX || !parseLine(line, row)) return false;
    if (offsets) offsets->push_back(start);
    rows.push_back(row);
  }
  revision = revisionOf(crc);
  return true;
}

bool validIndex(const char* csvPath, const char* indexPath, uint16_t* count = nullptr) {
  std::vector<PlaylistRow> rows;
  std::vector<uint32_t> offsets;
  String revision;
  if (!readRows(csvPath, rows, revision, &offsets)) return false;
  File index = SPIFFS.open(indexPath, "r");
  if (!index || index.size() != offsets.size() * sizeof(uint32_t)) return false;
  for (uint32_t expected : offsets) {
    uint32_t actual = 0;
    if (index.readBytes(reinterpret_cast<char*>(&actual), sizeof(actual)) != sizeof(actual) ||
        actual != expected) return false;
  }
  if (count) *count = static_cast<uint16_t>(rows.size());
  return true;
}

bool prepareIndex(const char* csvPath) {
  if (!removeIfPresent(kIndexTmp)) return false;
  std::vector<PlaylistRow> rows;
  std::vector<uint32_t> offsets;
  String revision;
  if (!readRows(csvPath, rows, revision, &offsets)) return false;
  File index = SPIFFS.open(kIndexTmp, "w");
  if (!index) return false;
  bool written = true;
  for (uint32_t offset : offsets) {
    if (index.write(reinterpret_cast<const uint8_t*>(&offset), sizeof(offset)) != sizeof(offset)) {
      written = false;
      break;
    }
  }
  index.flush();
  index.close();
  return written && validIndex(csvPath, kIndexTmp);
}
bool installIndex() {
  if (!removeIfPresent(kIndexBak)) return false;
  const bool hadIndex = SPIFFS.exists(INDEX_PATH);
  if (hadIndex && !SPIFFS.rename(INDEX_PATH, kIndexBak)) return false;
  if (!SPIFFS.rename(kIndexTmp, INDEX_PATH)) {
    if (hadIndex && !SPIFFS.rename(kIndexBak, INDEX_PATH))
      Serial.println("##[ERROR]# Playlist index rollback failed");
    return false;
  }
  return true;
}

bool rollbackPlaylist() {
  bool restored = true;
  if (SPIFFS.exists(kCsvBak)) {
    if (!removeIfPresent(PLAYLIST_PATH) || !SPIFFS.rename(kCsvBak, PLAYLIST_PATH))
      restored = false;
  } else if (!removeIfPresent(PLAYLIST_PATH)) {
    restored = false;
  }
  if (SPIFFS.exists(kIndexBak)) {
    if (!removeIfPresent(INDEX_PATH) || !SPIFFS.rename(kIndexBak, INDEX_PATH))
      restored = false;
  }
  if (restored && SPIFFS.exists(PLAYLIST_PATH) &&
      !validIndex(PLAYLIST_PATH, INDEX_PATH)) {
    if (!playlistStore.rebuildIndex()) restored = false;
  }
  if (!restored) Serial.println("##[ERROR]# Playlist rollback incomplete; recovery will retry on boot");
  return restored;
}
}  // namespace

PlaylistStore playlistStore;

bool PlaylistStore::begin() {
  if (!playlistMutex) playlistMutex = xSemaphoreCreateRecursiveMutex();
  return playlistMutex != nullptr;
}

bool PlaylistStore::lock() {
  return playlistMutex &&
         xSemaphoreTakeRecursive(playlistMutex, pdMS_TO_TICKS(3000)) == pdTRUE;
}

void PlaylistStore::unlock() {
  if (playlistMutex) xSemaphoreGiveRecursive(playlistMutex);
}

PlaylistGuard::PlaylistGuard() : held_(playlistStore.lock()) {}
PlaylistGuard::~PlaylistGuard() { if (held_) playlistStore.unlock(); }

bool PlaylistStore::parseInteger(const String& text, int& value) {
  return playlistParseInteger(text.c_str(), text.length(), value);
}

bool PlaylistStore::validRecord(const PlaylistRow& row) {
  return playlistValidField(row.name.c_str(), row.name.length(), true) &&
         playlistValidField(row.url.c_str(), row.url.length(), false) &&
         playlistValidOvol(row.ovol);
}

bool PlaylistStore::snapshot(std::vector<PlaylistRow>& rows, String& revision) {
  if (!SPIFFS.exists(PLAYLIST_PATH)) {
    rows.clear();
    revision = "00000000";
    return true;
  }
  return readRows(PLAYLIST_PATH, rows, revision);
}

PlaylistReadError PlaylistStore::readImport(const char* path,
                                            std::vector<PlaylistRow>& rows) {
  bool ioError = false;
  String ignoredRevision;
  if (readRows(path, rows, ignoredRevision, nullptr, &ioError))
    return PlaylistReadError::OK;
  return ioError ? PlaylistReadError::IO_ERROR : PlaylistReadError::INVALID;
}

bool PlaylistStore::rebuildIndex() {
  PlaylistGuard guard;
  if (!guard || !SPIFFS.exists(PLAYLIST_PATH)) return false;
  if (!prepareIndex(PLAYLIST_PATH) || !installIndex() ||
      !validIndex(PLAYLIST_PATH, INDEX_PATH)) return false;
  return removeIfPresent(kIndexBak);
}

bool PlaylistStore::recover() {
  PlaylistGuard guard;
  if (!guard) return false;
  std::vector<PlaylistRow> rows;
  String revision;
  bool csvValid = SPIFFS.exists(PLAYLIST_PATH) &&
                  readRows(PLAYLIST_PATH, rows, revision);
  if (!csvValid && SPIFFS.exists(kCsvBak)) {
    if (!readRows(kCsvBak, rows, revision) ||
        !removeIfPresent(PLAYLIST_PATH) ||
        !SPIFFS.rename(kCsvBak, PLAYLIST_PATH)) return false;
    csvValid = true;
    Serial.println("##[BOOT]# Playlist restored from backup");
  }
  if (!csvValid) return !SPIFFS.exists(PLAYLIST_PATH);
  if (!validIndex(PLAYLIST_PATH, INDEX_PATH) && !rebuildIndex()) return false;
  // A valid CSV wins over a stale, uncommitted tmp or old backup.
  return removeIfPresent(kCsvTmp) && removeIfPresent(kCsvBak) &&
         removeIfPresent(kIndexTmp) && removeIfPresent(kIndexBak);
}

PlaylistWriteError PlaylistStore::commit(const std::vector<PlaylistRow>& rows,
                                         uint16_t current, String& revision) {
  if (rows.size() > UINT16_MAX || current > rows.size()) return PlaylistWriteError::INVALID;
  size_t bytesNeeded = 0;
  for (const PlaylistRow& row : rows) {
    if (!validRecord(row)) return PlaylistWriteError::INVALID;
    bytesNeeded += row.name.length() + row.url.length() + String(row.ovol).length() + 3;
  }
  if (SPIFFS.totalBytes() < SPIFFS.usedBytes() ||
      SPIFFS.totalBytes() - SPIFFS.usedBytes() <
          bytesNeeded + rows.size() * sizeof(uint32_t) + 1024)
    return PlaylistWriteError::NO_SPACE;
  if (!removeIfPresent(kCsvTmp) || !removeIfPresent(kIndexTmp))
    return PlaylistWriteError::IO_ERROR;
  File tmp = SPIFFS.open(kCsvTmp, "w");
  if (!tmp) return PlaylistWriteError::NO_SPACE;
  bool written = true;
  for (const PlaylistRow& row : rows) {
    String line = row.name + '\t' + row.url + '\t' + String(row.ovol) + '\n';
    if (tmp.write(reinterpret_cast<const uint8_t*>(line.c_str()), line.length()) != line.length()) {
      written = false;
      break;
    }
  }
  tmp.flush();
  tmp.close();
  if (!written) return PlaylistWriteError::NO_SPACE;
  std::vector<PlaylistRow> verified;
  String newRevision;
  if (!readRows(kCsvTmp, verified, newRevision) || verified.size() != rows.size())
    return PlaylistWriteError::IO_ERROR;
  for (size_t i = 0; i < rows.size(); ++i) {
    if (verified[i].name != rows[i].name || verified[i].url != rows[i].url ||
        verified[i].ovol != rows[i].ovol) return PlaylistWriteError::IO_ERROR;
  }
  File sizeCheck = SPIFFS.open(kCsvTmp, "r");
  if (!sizeCheck || sizeCheck.size() != bytesNeeded) return PlaylistWriteError::IO_ERROR;
  sizeCheck.close();
  if (!prepareIndex(kCsvTmp)) return PlaylistWriteError::NO_SPACE;

  const bool hadCsv = SPIFFS.exists(PLAYLIST_PATH);
  if (!removeIfPresent(kCsvBak)) return PlaylistWriteError::IO_ERROR;
  if (hadCsv && !SPIFFS.rename(PLAYLIST_PATH, kCsvBak))
    return PlaylistWriteError::IO_ERROR;
  if (!SPIFFS.rename(kCsvTmp, PLAYLIST_PATH)) {
    if (hadCsv && !rollbackPlaylist()) Serial.println("##[ERROR]# Playlist restore failed");
    return PlaylistWriteError::IO_ERROR;
  }
  if (!installIndex() || !validIndex(PLAYLIST_PATH, INDEX_PATH)) {
    rollbackPlaylist();
    return PlaylistWriteError::IO_ERROR;
  }
  const uint16_t previousCurrent = config.lastStation();
  const bool previousZeroIntentional =
      config.store._reserved == VOXONE_NO_STATION_MARKER;
  if (!config.setLastStationChecked(current)) {
    rollbackPlaylist();
    return PlaylistWriteError::IO_ERROR;
  }
  if (!removeIfPresent(kIndexBak) || !removeIfPresent(kCsvBak)) {
    rollbackPlaylist();
    config.setLastStationChecked(previousCurrent, previousZeroIntentional);
    return PlaylistWriteError::IO_ERROR;
  }
  revision = newRevision;
  return PlaylistWriteError::OK;
}
