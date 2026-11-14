#include "../src/core/sd_path.h"

#include <cassert>
#include <cstring>

int main() {
  using namespace voxone::sd_path;
  assert(valid("/"));
  assert(valid("/Muzyka/Album 01/utwór.mp3"));
  assert(!valid("relative/file.mp3"));
  assert(!valid("/Muzyka//plik.mp3"));
  assert(!valid("/Muzyka/../sekret.mp3"));
  assert(!valid("/Muzyka/./plik.mp3"));
  assert(!valid("/Muzyka\\plik.mp3"));

  assert(audioFile("/a.MP3"));
  assert(audioFile("/a.aac"));
  assert(audioFile("/a.m4a"));
  assert(audioFile("/a.wav"));
  assert(audioFile("/a.flac"));
  assert(!audioFile("/a.txt"));
  assert(std::strcmp(mimeType("/a.flac"), "audio/flac") == 0);

  char output[kPathCapacity]{};
  assert(parent("/Muzyka/Album/plik.mp3", output, sizeof(output)));
  assert(std::strcmp(output, "/Muzyka/Album") == 0);
  assert(parent("/Muzyka", output, sizeof(output)));
  assert(std::strcmp(output, "/") == 0);
  assert(parent("/", output, sizeof(output)));
  assert(std::strcmp(output, "/") == 0);
}
