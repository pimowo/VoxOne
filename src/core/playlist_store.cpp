#include "options.h"
#include "playlist_store.h"
#include "playlist_validation.h"
#include "station_format.h"
#include "station_store_state.h"

#include <SPIFFS.h>
#include <freertos/semphr.h>
#include <limits.h>
#include <esp_system.h>
#include "config.h"

namespace {
constexpr char kCsvTmp[] = "/data/stations.tmp";
constexpr char kCsvBak[] = "/data/stations.bak";
constexpr char kIndexTmp[] = "/data/stations.idx.tmp";
constexpr char kIndexBak[] = "/data/stations.idx.bak";
constexpr char kLegacyPath[] = "/data/playlist.csv";
constexpr char kLegacyIndex[] = "/data/index.dat";
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
  char* name = nullptr;
  char* url = nullptr;
  if (!stationParseFields(line, row.id, name, url, row.ovol, row.metadataMode)) return false;
  row.name = name;
  row.url = url;
  return true;
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
  constexpr size_t headerSize = sizeof(kStationsHeader) - 1;
  if (fileSize < headerSize) return false;
  for (size_t i = 0; i < headerSize; ++i) {
    const int value = file.read();
    if (value < 0 || value != kStationsHeader[i]) return false;
    crc = playlistCrcByte(crc, static_cast<uint8_t>(value));
    ++position;
  }
  start = position;
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
      for (const PlaylistRow& existing : rows)
        if (existing.id == row.id) return false;
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
    for (const PlaylistRow& existing : rows)
      if (existing.id == row.id) return false;
    if (offsets) offsets->push_back(start);
    rows.push_back(row);
  }
  revision = revisionOf(crc);
  return true;
}

bool readLegacyRows(std::vector<PlaylistRow>& rows) {
  File file = SPIFFS.open(kLegacyPath, "r");
  if (!file) return false;
  rows.clear();
  char line[BUFLEN * 3];
  while (file.available()) {
    const size_t length = file.readBytesUntil('\n', line, sizeof(line) - 1);
    if (!length || length >= sizeof(line) - 1 || rows.size() >= UINT16_MAX) return false;
    line[length] = '\0';
    if (strlen(line) != length) return false;
    if (line[length - 1] == '\r') line[length - 1] = '\0';
    char* name = nullptr;
    char* url = nullptr;
    PlaylistRow row;
    if (!stationParseLegacyFields(line, name, url, row.ovol)) return false;
    row.name = name;
    row.url = url;
    rows.push_back(row);
  }
  return true;
}

bool scanRows(const char* csvPath, String* revision,
              const std::vector<PlaylistRow>* expectedRows,
              File* indexOutput, File* indexInput, uint16_t* count = nullptr) {
  File file = SPIFFS.open(csvPath, "r");
  if (!file) return false;
  char line[BUFLEN * 3];
  size_t length = 0;
  uint32_t position = 0, start = 0, crc = 0xFFFFFFFFUL;
  uint16_t rows = 0;
  std::vector<uint64_t> ids;
  constexpr size_t headerSize = sizeof(kStationsHeader) - 1;
  const size_t fileSize = file.size();
  if (fileSize < headerSize) return false;
  for (size_t i = 0; i < headerSize; ++i) {
    const int value = file.read();
    if (value < 0 || value != kStationsHeader[i]) return false;
    crc = playlistCrcByte(crc, static_cast<uint8_t>(value));
    ++position;
  }
  start = position;
  auto accept = [&]() {
    if (!length || rows == UINT16_MAX) return false;
    line[length] = '\0';
    PlaylistRow row;
    if (strlen(line) != length || !parseLine(line, row)) return false;
    for (uint64_t id : ids) if (id == row.id) return false;
    ids.push_back(row.id);
    if (expectedRows &&
        (rows >= expectedRows->size() || row.id != (*expectedRows)[rows].id ||
         row.name != (*expectedRows)[rows].name ||
         row.url != (*expectedRows)[rows].url ||
         row.ovol != (*expectedRows)[rows].ovol ||
         row.metadataMode != (*expectedRows)[rows].metadataMode)) return false;
    if (indexOutput && indexOutput->write(
            reinterpret_cast<const uint8_t*>(&start), sizeof(start)) != sizeof(start))
      return false;
    if (indexInput) {
      uint32_t indexed = 0;
      if (indexInput->readBytes(reinterpret_cast<char*>(&indexed), sizeof(indexed)) !=
              sizeof(indexed) || indexed != start) return false;
    }
    ++rows;
    length = 0;
    start = position;
    return true;
  };
  while (position < fileSize) {
    const int value = file.read();
    if (value < 0) return false;
    crc = playlistCrcByte(crc, static_cast<uint8_t>(value));
    ++position;
    if (value == '\n') {
      if (!accept()) return false;
    } else {
      if (length >= sizeof(line) - 1) return false;
      line[length++] = static_cast<char>(value);
    }
  }
  if (length && !accept()) return false;
  if (expectedRows && rows != expectedRows->size()) return false;
  if (indexInput && !stationIndexSizeMatches(indexInput->size(), rows)) return false;
  if (revision) *revision = revisionOf(crc);
  if (count) *count = rows;
  return true;
}

bool validIndex(const char* csvPath, const char* indexPath,
                uint16_t* count = nullptr) {
  File index = SPIFFS.open(indexPath, "r");
  if (!index) return false;
  const bool valid = scanRows(csvPath, nullptr, nullptr, nullptr, &index, count);
  index.close();
  return valid;
}

bool prepareIndex(const char* csvPath) {
  if (!removeIfPresent(kIndexTmp)) return false;
  File index = SPIFFS.open(kIndexTmp, "w");
  if (!index) return false;
  const bool written = scanRows(csvPath, nullptr, nullptr, &index, nullptr);
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
PlaylistGuard::~PlaylistGuard() { release(); }

void PlaylistGuard::release() {
  if (!held_) return;
  playlistStore.unlock();
  held_ = false;
}

bool PlaylistStore::parseInteger(const String& text, int& value) {
  return playlistParseInteger(text.c_str(), text.length(), value);
}

bool PlaylistStore::validRecord(const PlaylistRow& row) {
  return row.id != 0 && row.metadataMode <= STATION_META_SWAP &&
         playlistValidField(row.name.c_str(), row.name.length(), true) &&
         playlistValidField(row.url.c_str(), row.url.length(), false) &&
         playlistValidOvol(row.ovol);
}

uint64_t PlaylistStore::generateId(const std::vector<PlaylistRow>& rows) {
  return stationGenerateId(rows, []() { return esp_random(); });
}

bool PlaylistStore::findStationById(uint64_t id, uint16_t& position) {
  if (!id) return false;
  PlaylistGuard guard;
  if (!guard) return false;
  std::vector<PlaylistRow> rows;
  String revision;
  if (!snapshot(rows, revision)) return false;
  return stationFindPositionById(rows, id, position);
}

bool PlaylistStore::snapshot(std::vector<PlaylistRow>& rows, String& revision) {
  PlaylistGuard guard;
  if (!guard) return false;
  if (!SPIFFS.exists(PLAYLIST_PATH)) {
    if (SPIFFS.exists(kLegacyPath)) return false; // Failed migration must stay visible as an error.
    rows.clear();
    revision = "00000000";
    return true;
  }
  return readRows(PLAYLIST_PATH, rows, revision);
}

PlaylistReadError PlaylistStore::readImport(const char* path,
                                            std::vector<PlaylistRow>& rows) {
  PlaylistGuard guard;
  if (!guard) return PlaylistReadError::IO_ERROR;
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
  const bool activeExists = SPIFFS.exists(PLAYLIST_PATH);
  const bool backupExists = SPIFFS.exists(kCsvBak);
  const bool legacyExists = SPIFFS.exists(kLegacyPath);
  const bool activeValid = activeExists &&
                           readRows(PLAYLIST_PATH, rows, revision);
  const bool backupValid = !activeValid && backupExists &&
                           readRows(kCsvBak, rows, revision);
  const StationRecoverySource source = stationRecoverySource(
      activeExists, activeValid, backupExists, backupValid, legacyExists);
  if (source == StationRecoverySource::Backup) {
    if (!removeIfPresent(PLAYLIST_PATH) ||
        !SPIFFS.rename(kCsvBak, PLAYLIST_PATH)) return false;
    Serial.println("##[BOOT]# Playlist restored from backup");
  } else if (source == StationRecoverySource::InitializeEmpty) {
      String emptyRevision;
      if (commit({}, 0, emptyRevision) != PlaylistWriteError::OK) {
        Serial.println("##[ERROR]# Empty station store initialization failed");
        return false;
      }
      Serial.println("##[BOOT]# Initialized empty VoxOne Stations v1 store");
      return true;
  } else if (source == StationRecoverySource::Legacy) {
    if (!readLegacyRows(rows)) {
      Serial.println("##[ERROR]# Legacy station migration: invalid playlist");
      return false;
    }
    if (!stationAssignMigratedIds(rows, [](const std::vector<PlaylistRow>& current) {
          return PlaylistStore::generateId(current);
        })) return false;
    String newRevision;
    const uint16_t current = rows.empty() ? 0 :
        (config.lastStation() > rows.size() ? 1 : config.lastStation());
    if (commit(rows, current, newRevision) != PlaylistWriteError::OK) {
      Serial.println("##[ERROR]# Legacy station migration failed; old playlist retained");
      return false;
    }
    Serial.printf("##[BOOT]# Migrated %u stations to VoxOne Stations v1\n",
                  static_cast<unsigned>(rows.size()));
    removeIfPresent(kLegacyPath);
    removeIfPresent(kLegacyIndex);
    return true;
  } else if (source == StationRecoverySource::Fail) {
    return false;
  }
  if (!validIndex(PLAYLIST_PATH, INDEX_PATH) && !rebuildIndex()) return false;
  // A valid CSV wins over a stale, uncommitted tmp or old backup.
  return removeIfPresent(kCsvTmp) && removeIfPresent(kCsvBak) &&
         removeIfPresent(kIndexTmp) && removeIfPresent(kIndexBak);
}

PlaylistWriteError PlaylistStore::commit(const std::vector<PlaylistRow>& rows,
                                         uint16_t current, String& revision,
                                         PlaylistCommitDiagnostics* diagnostics) {
  PlaylistCommitDiagnostics timing;
  timing.rows = static_cast<uint16_t>(rows.size());
  auto timestamp = [&]() { return diagnostics ? micros() : 0U; };
  const uint32_t totalStartedUs = timestamp();
  auto finish = [&](PlaylistWriteError result) {
    timing.totalUs = timestamp() - totalStartedUs;
    if (diagnostics) *diagnostics = timing;
    return result;
  };

  uint32_t phaseStartedUs = timestamp();
  PlaylistGuard guard;
  timing.lockUs = timestamp() - phaseStartedUs;
  if (!guard) return finish(PlaylistWriteError::IO_ERROR);

  phaseStartedUs = timestamp();
  if (rows.size() > UINT16_MAX || current > rows.size()) {
    timing.validateUs = timestamp() - phaseStartedUs;
    return finish(PlaylistWriteError::INVALID);
  }
  size_t bytesNeeded = sizeof(kStationsHeader) - 1;
  for (const PlaylistRow& row : rows) {
    if (!validRecord(row)) {
      timing.validateUs = timestamp() - phaseStartedUs;
      return finish(PlaylistWriteError::INVALID);
    }
    bytesNeeded += 16 + row.name.length() + row.url.length() +
                   String(row.ovol).length() +
                   (row.metadataMode == STATION_META_SWAP ? 4 : 6) + 5;
  }
  for (size_t i = 0; i < rows.size(); ++i)
    for (size_t j = 0; j < i; ++j)
      if (rows[i].id == rows[j].id) {
        timing.validateUs = timestamp() - phaseStartedUs;
        return finish(PlaylistWriteError::INVALID);
      }
  timing.bytes = bytesNeeded;
  if (!stationCommitHasSpace(SPIFFS.totalBytes(), SPIFFS.usedBytes(),
                             bytesNeeded, rows.size())) {
    timing.validateUs = timestamp() - phaseStartedUs;
    return finish(PlaylistWriteError::NO_SPACE);
  }
  timing.validateUs = timestamp() - phaseStartedUs;

  phaseStartedUs = timestamp();
  if (!removeIfPresent(kCsvTmp) || !removeIfPresent(kIndexTmp)) {
    timing.stageUs = timestamp() - phaseStartedUs;
    return finish(PlaylistWriteError::IO_ERROR);
  }
  timing.stageUs = timestamp() - phaseStartedUs;

  phaseStartedUs = timestamp();
  File tmp = SPIFFS.open(kCsvTmp, "w");
  if (!tmp) {
    timing.writeUs = timestamp() - phaseStartedUs;
    return finish(PlaylistWriteError::NO_SPACE);
  }
  bool written = tmp.write(reinterpret_cast<const uint8_t*>(kStationsHeader),
                           sizeof(kStationsHeader) - 1) == sizeof(kStationsHeader) - 1;
  for (const PlaylistRow& row : rows) {
    char id[17];
    stationFormatId(row.id, id);
    String line = String(id) + '\t' + row.name + '\t' + row.url + '\t' +
                  String(row.ovol) + '\t' +
                  (row.metadataMode == STATION_META_SWAP ? "swap\n" : "normal\n");
    if (tmp.write(reinterpret_cast<const uint8_t*>(line.c_str()), line.length()) != line.length()) {
      written = false;
      break;
    }
  }
  tmp.flush();
  tmp.close();
  timing.writeUs = timestamp() - phaseStartedUs;
  if (!written) return finish(PlaylistWriteError::NO_SPACE);

  phaseStartedUs = timestamp();
  String newRevision;
  if (!scanRows(kCsvTmp, &newRevision, &rows, nullptr, nullptr)) {
    timing.verifyUs = timestamp() - phaseStartedUs;
    return finish(PlaylistWriteError::IO_ERROR);
  }
  File sizeCheck = SPIFFS.open(kCsvTmp, "r");
  if (!sizeCheck || sizeCheck.size() != bytesNeeded) {
    timing.verifyUs = timestamp() - phaseStartedUs;
    return finish(PlaylistWriteError::IO_ERROR);
  }
  sizeCheck.close();
  timing.verifyUs = timestamp() - phaseStartedUs;

  phaseStartedUs = timestamp();
  if (!prepareIndex(kCsvTmp)) {
    timing.indexUs = timestamp() - phaseStartedUs;
    return finish(PlaylistWriteError::NO_SPACE);
  }
  timing.indexUs = timestamp() - phaseStartedUs;

  phaseStartedUs = timestamp();
  const bool hadCsv = SPIFFS.exists(PLAYLIST_PATH);
  if (!removeIfPresent(kCsvBak)) {
    timing.installUs = timestamp() - phaseStartedUs;
    return finish(PlaylistWriteError::IO_ERROR);
  }
  if (hadCsv && !SPIFFS.rename(PLAYLIST_PATH, kCsvBak)) {
    timing.installUs = timestamp() - phaseStartedUs;
    return finish(PlaylistWriteError::IO_ERROR);
  }
  if (!SPIFFS.rename(kCsvTmp, PLAYLIST_PATH)) {
    if (hadCsv && !rollbackPlaylist()) Serial.println("##[ERROR]# Playlist restore failed");
    timing.installUs = timestamp() - phaseStartedUs;
    return finish(PlaylistWriteError::IO_ERROR);
  }
  if (!installIndex() || !validIndex(PLAYLIST_PATH, INDEX_PATH)) {
    rollbackPlaylist();
    timing.installUs = timestamp() - phaseStartedUs;
    return finish(PlaylistWriteError::IO_ERROR);
  }
  timing.installUs = timestamp() - phaseStartedUs;

  phaseStartedUs = timestamp();
  const uint16_t previousCurrent = config.lastStation();
  const bool previousZeroIntentional =
      config.store._reserved == VOXONE_NO_STATION_MARKER;
  if (!config.setLastStationChecked(current)) {
    rollbackPlaylist();
    timing.persistUs = timestamp() - phaseStartedUs;
    return finish(PlaylistWriteError::IO_ERROR);
  }
  timing.persistUs = timestamp() - phaseStartedUs;

  phaseStartedUs = timestamp();
  if (!removeIfPresent(kIndexBak) || !removeIfPresent(kCsvBak)) {
    rollbackPlaylist();
    config.setLastStationChecked(previousCurrent, previousZeroIntentional);
    timing.cleanupUs = timestamp() - phaseStartedUs;
    return finish(PlaylistWriteError::IO_ERROR);
  }
  timing.cleanupUs = timestamp() - phaseStartedUs;
  revision = newRevision;
  return finish(PlaylistWriteError::OK);
}
