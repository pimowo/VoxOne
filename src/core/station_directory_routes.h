#ifndef VOXONE_STATION_DIRECTORY_ROUTES_H
#define VOXONE_STATION_DIRECTORY_ROUTES_H

#include <cstddef>
#include <cstring>

namespace stationDirectory {
constexpr char kStationsSnapshotRoute[] = "/api/stations";
constexpr char kDirectorySearchRoute[] = "/api/directory/search";

enum class ReadRoute { NONE, STATIONS_SNAPSHOT, DIRECTORY_SEARCH };
enum class SearchRequest { POLL, START, MISSING_QUERY };

inline SearchRequest classifySearchRequest(bool hasJob, bool hasQuery) {
  if (hasJob) return SearchRequest::POLL;
  return hasQuery ? SearchRequest::START : SearchRequest::MISSING_QUERY;
}

// AsyncWebServer's static callback routes also match child paths at a slash boundary.
inline bool asyncRouteMatches(const char* route, const char* uri) {
  if (!route || !uri) return false;
  const size_t length = std::strlen(route);
  return std::strncmp(route, uri, length) == 0 &&
         (uri[length] == '\0' || uri[length] == '/');
}

inline ReadRoute resolveReadRoute(const char* uri) {
  // Keep the production registration order: the local snapshot route is first.
  if (asyncRouteMatches(kStationsSnapshotRoute, uri)) return ReadRoute::STATIONS_SNAPSHOT;
  if (asyncRouteMatches(kDirectorySearchRoute, uri)) return ReadRoute::DIRECTORY_SEARCH;
  return ReadRoute::NONE;
}
}  // namespace stationDirectory

#endif
