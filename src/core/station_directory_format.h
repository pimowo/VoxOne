#ifndef VOXONE_STATION_DIRECTORY_FORMAT_H
#define VOXONE_STATION_DIRECTORY_FORMAT_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace stationDirectory {
constexpr unsigned kDefaultLimit = 20;
constexpr unsigned kMaxLimit = 20;
constexpr size_t kMaxQueryBytes = 80;
constexpr size_t kMaxResponseBytes = 32768;
constexpr size_t kMaxOutputBytes = 16384;

bool validQuery(const std::string& query);
bool validCountry(const std::string& country);
bool validLimit(unsigned limit);
std::string urlEncode(const std::string& text);
std::string searchPath(const std::string& query, const std::string& country, unsigned limit);
bool parseResponse(const char* input, size_t length, unsigned limit, std::string& output);
bool isSearchResultEnvelope(const char* body, size_t length);

class ResponseStream {
 public:
  explicit ResponseStream(unsigned limit);
  bool write(const char* data, size_t length);
  bool finish(std::string& output);
  unsigned resultCount() const { return added_; }
 private:
  enum class State : uint8_t { START, ITEM_OR_END, IN_ITEM, SEPARATOR_OR_END, DONE };
  bool addObject();
  State state_ = State::START;
  unsigned limit_;
  unsigned added_ = 0;
  size_t rawBytes_ = 0;
  int objectDepth_ = 0;
  bool inString_ = false;
  bool escaped_ = false;
  std::string object_;
  std::string output_ = "{\"results\":[";
};

}  // namespace stationDirectory
#endif
