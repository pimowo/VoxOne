#ifndef VOXONE_PLAYLIST_STORE_H
#define VOXONE_PLAYLIST_STORE_H

#include <Arduino.h>
#include <vector>

struct PlaylistRow {
  String name;
  String url;
  int ovol = 0;
};

enum class PlaylistWriteError : uint8_t { OK, INVALID, NO_SPACE, IO_ERROR };

class PlaylistStore {
 public:
  bool begin();
  bool recover();
  bool rebuildIndex();
  bool snapshot(std::vector<PlaylistRow>& rows, String& revision);
  PlaylistWriteError commit(const std::vector<PlaylistRow>& rows, uint16_t current,
                            String& revision);
  static bool validRecord(const PlaylistRow& row);
  static bool parseInteger(const String& text, int& value);
  bool lock();
  void unlock();
};

class PlaylistGuard {
 public:
  PlaylistGuard();
  ~PlaylistGuard();
  explicit operator bool() const { return held_; }
  PlaylistGuard(const PlaylistGuard&) = delete;
  PlaylistGuard& operator=(const PlaylistGuard&) = delete;
 private:
  bool held_;
};

extern PlaylistStore playlistStore;

#endif
