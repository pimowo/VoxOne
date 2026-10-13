#ifndef VOXONE_PLAYLIST_STORE_H
#define VOXONE_PLAYLIST_STORE_H

#include <Arduino.h>
#include <vector>

struct PlaylistRow {
  uint64_t id = 0;
  String name;
  String url;
  int ovol = 0;
  uint8_t metadataMode = 0;
};

enum class PlaylistWriteError : uint8_t { OK, INVALID, NO_SPACE, IO_ERROR };
enum class PlaylistReadError : uint8_t { OK, INVALID, IO_ERROR };

struct PlaylistCommitDiagnostics {
  uint32_t totalUs = 0;
  uint32_t lockUs = 0;
  uint32_t validateUs = 0;
  uint32_t stageUs = 0;
  uint32_t writeUs = 0;
  uint32_t verifyUs = 0;
  uint32_t indexUs = 0;
  uint32_t installUs = 0;
  uint32_t persistUs = 0;
  uint32_t cleanupUs = 0;
  size_t bytes = 0;
  uint16_t rows = 0;
};

class PlaylistStore {
 public:
  bool begin();
  bool recover();
  bool rebuildIndex();
  bool snapshot(std::vector<PlaylistRow>& rows, String& revision);
  PlaylistReadError readImport(const char* path, std::vector<PlaylistRow>& rows);
  PlaylistWriteError commit(const std::vector<PlaylistRow>& rows, uint16_t current,
                            String& revision,
                            PlaylistCommitDiagnostics* diagnostics = nullptr);
  static bool validRecord(const PlaylistRow& row);
  static bool parseInteger(const String& text, int& value);
  static uint64_t generateId(const std::vector<PlaylistRow>& rows);
  bool findStationById(uint64_t id, uint16_t& position);
  bool lock(uint32_t waitMs = 3000);
  void unlock();
};

class PlaylistGuard {
 public:
  explicit PlaylistGuard(uint32_t waitMs = 3000);
  ~PlaylistGuard();
  explicit operator bool() const { return held_; }
  void release();
  PlaylistGuard(const PlaylistGuard&) = delete;
  PlaylistGuard& operator=(const PlaylistGuard&) = delete;
 private:
  bool held_;
};

extern PlaylistStore playlistStore;

#endif
