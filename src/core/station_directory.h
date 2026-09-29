#ifndef VOXONE_STATION_DIRECTORY_H
#define VOXONE_STATION_DIRECTORY_H

#include <Arduino.h>

enum class DirectoryStartResult : uint8_t {
  STARTED,
  BUSY,
  OFFLINE,
  NO_MEMORY
};

bool stationDirectoryInit();
DirectoryStartResult stationDirectoryStart(const String& query, const String& country,
                                           unsigned limit, uint32_t& job);
bool stationDirectoryPoll(uint32_t job, int& status, String& body, bool& ready);

#endif