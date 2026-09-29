#include "station_directory_format.h"
#include "playlist_validation.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace stationDirectory {
namespace {
struct Station {
  std::string id, name, url, resolved, country, countryCode, codec, favicon;
  int bitrate = 0;
  bool hls = false;
  bool working = true;
};

bool httpUrl(const std::string& url, size_t maxLength) {
  const size_t start = url.compare(0, 7, "http://") == 0 ? 7 :
                       url.compare(0, 8, "https://") == 0 ? 8 : 0;
  if (!start || url.size() <= start || url.size() > maxLength) return false;
  for (size_t i = start; i < url.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(url[i]);
    if (ch <= 0x20 || ch == 0x7f) return false;
  }
  return url[start] != '/' && url[start] != '?' && url[start] != '#';
}

bool streamUrl(const std::string& url) { return httpUrl(url, 169); }

void appendJsonString(std::string& output, const std::string& value) {
  static const char hex[] = "0123456789abcdef";
  output.push_back('"');
  for (unsigned char ch : value) {
    if (ch == '"' || ch == '\\') {
      output.push_back('\\');
      output.push_back(static_cast<char>(ch));
    } else if (ch < 0x20) {
      output += "\\u00";
      output.push_back(hex[ch >> 4]);
      output.push_back(hex[ch & 15]);
    } else {
      output.push_back(static_cast<char>(ch));
    }
  }
  output.push_back('"');
}

class Parser {
 public:
  Parser(const char* input, size_t length) : input_(input), length_(length) {}
  void space() {
    while (pos_ < length_ && (input_[pos_] == ' ' || input_[pos_] == '\n' ||
                              input_[pos_] == '\r' || input_[pos_] == '\t')) ++pos_;
  }
  bool take(char ch) {
    space();
    if (pos_ >= length_ || input_[pos_] != ch) return false;
    ++pos_;
    return true;
  }
  char next() {
    space();
    return pos_ < length_ ? input_[pos_] : '\0';
  }
  bool finished() {
    space();
    return pos_ == length_;
  }
  bool string(std::string& out, size_t maxBytes = 1024) {
    if (!take('"')) return false;
    out.clear();
    while (pos_ < length_) {
      unsigned char ch = static_cast<unsigned char>(input_[pos_++]);
      if (ch == '"') return true;
      if (ch < 0x20) return false;
      if (ch == '\\') {
        if (pos_ >= length_) return false;
        const char escaped = input_[pos_++];
        switch (escaped) {
          case '"': ch = '"'; break;
          case '\\': ch = '\\'; break;
          case '/': ch = '/'; break;
          case 'b': ch = '\b'; break;
          case 'f': ch = '\f'; break;
          case 'n': ch = '\n'; break;
          case 'r': ch = '\r'; break;
          case 't': ch = '\t'; break;
          case 'u': {
            uint32_t point = 0;
            if (!hex4(point)) return false;
            if (point >= 0xd800 && point <= 0xdbff) {
              if (pos_ + 2 > length_ || input_[pos_++] != '\\' ||
                  input_[pos_++] != 'u') return false;
              uint32_t low = 0;
              if (!hex4(low) || low < 0xdc00 || low > 0xdfff) return false;
              point = 0x10000 + ((point - 0xd800) << 10) + (low - 0xdc00);
            } else if (point >= 0xdc00 && point <= 0xdfff) return false;
            if (maxBytes) {
              if (point < 0x80) out.push_back(static_cast<char>(point));
              else if (point < 0x800) {
                out.push_back(static_cast<char>(0xc0 | (point >> 6)));
                out.push_back(static_cast<char>(0x80 | (point & 0x3f)));
              } else if (point < 0x10000) {
                out.push_back(static_cast<char>(0xe0 | (point >> 12)));
                out.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (point & 0x3f)));
              } else {
                out.push_back(static_cast<char>(0xf0 | (point >> 18)));
                out.push_back(static_cast<char>(0x80 | ((point >> 12) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (point & 0x3f)));
              }
            }
            if (out.size() > maxBytes) return false;
            continue;
          }
          default: return false;
        }
      }
      if (maxBytes) out.push_back(static_cast<char>(ch));
      if (out.size() > maxBytes) return false;
    }
    return false;
  }
  bool scalar(std::string& out) {
    space();
    const size_t start = pos_;
    while (pos_ < length_ && input_[pos_] != ',' && input_[pos_] != '}' &&
           input_[pos_] != ']' && !std::isspace(static_cast<unsigned char>(input_[pos_]))) ++pos_;
    if (pos_ == start) return false;
    out.assign(input_ + start, pos_ - start);
    if (out == "true" || out == "false" || out == "null") return true;
    const char* p = out.c_str();
    if (*p == '-') ++p;
    if (!*p) return false;
    if (*p == '0') ++p;
    else {
      if (*p < '1' || *p > '9') return false;
      while (*p >= '0' && *p <= '9') ++p;
    }
    if (*p == '.') {
      ++p;
      if (*p < '0' || *p > '9') return false;
      while (*p >= '0' && *p <= '9') ++p;
    }
    if (*p == 'e' || *p == 'E') {
      ++p;
      if (*p == '+' || *p == '-') ++p;
      if (*p < '0' || *p > '9') return false;
      while (*p >= '0' && *p <= '9') ++p;
    }
    return *p == '\0';
  }
  bool skip(unsigned depth = 0) {
    if (depth > 8) return false;
    const char ch = next();
    if (ch == '"') {
      std::string ignored;
      return string(ignored, 0);
    }
    if (ch == '{' || ch == '[') {
      take(ch);
      const char end = ch == '{' ? '}' : ']';
      if (take(end)) return true;
      do {
        if (ch == '{') {
          std::string ignored;
          if (!string(ignored, 0) || !take(':')) return false;
        }
        if (!skip(depth + 1)) return false;
        if (take(end)) return true;
      } while (take(','));
      return false;
    }
    std::string ignored;
    return scalar(ignored);
  }
 private:
  bool hex4(uint32_t& value) {
    if (pos_ + 4 > length_) return false;
    for (unsigned i = 0; i < 4; ++i) {
      const char ch = input_[pos_++];
      const int digit = ch >= '0' && ch <= '9' ? ch - '0' :
                        ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 :
                        ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
      if (digit < 0) return false;
      value = (value << 4) | static_cast<unsigned>(digit);
    }
    return true;
  }
  const char* input_;
  size_t length_;
  size_t pos_ = 0;
};

bool parseStation(Parser& parser, Station& station) {
  if (!parser.take('{')) return false;
  if (parser.take('}')) return true;
  do {
    std::string key, value;
    if (!parser.string(key, 40) || !parser.take(':')) return false;
    std::string* target = nullptr;
    if (key == "stationuuid") target = &station.id;
    else if (key == "name") target = &station.name;
    else if (key == "url") target = &station.url;
    else if (key == "url_resolved") target = &station.resolved;
    else if (key == "country") target = &station.country;
    else if (key == "countrycode") target = &station.countryCode;
    else if (key == "codec") target = &station.codec;
    else if (key == "favicon") target = &station.favicon;
    if (target) {
      if (parser.next() != '"' || !parser.string(*target)) return false;
    } else if (key == "bitrate" || key == "hls" || key == "lastcheckok") {
      if (!parser.scalar(value)) return false;
      if (key == "bitrate") {
        char* end = nullptr;
        const long parsed = std::strtol(value.c_str(), &end, 10);
        station.bitrate = end && !*end && parsed > 0 && parsed <= 100000 ? parsed : 0;
      } else if (key == "hls") station.hls = value == "1" || value == "true";
      else station.working = value == "1" || value == "true";
    } else if (!parser.skip()) return false;
    if (parser.take('}')) return true;
  } while (parser.take(','));
  return false;
}

bool usable(const Station& station) {
  if (!station.working || station.id.empty() || station.id.size() > 64 ||
      !playlistValidField(station.name.c_str(), station.name.size(), true)) return false;
  for (unsigned char ch : station.name)
    if (ch < 0x20 || ch == 0x7f) return false;
  const std::string& url = streamUrl(station.resolved) ? station.resolved : station.url;
  return streamUrl(url) && playlistValidField(url.c_str(), url.size(), false);
}

void appendStation(std::string& output, const Station& station) {
  const std::string& url = streamUrl(station.resolved) ? station.resolved : station.url;
  output += "{\"directoryId\":";
  appendJsonString(output, station.id);
  output += ",\"name\":";
  appendJsonString(output, station.name);
  output += ",\"url\":";
  appendJsonString(output, url);
  output += ",\"country\":";
  appendJsonString(output, station.country.size() <= 64 ? station.country : std::string());
  output += ",\"countryCode\":";
  appendJsonString(output, station.countryCode.size() <= 2 ? station.countryCode : std::string());
  output += ",\"codec\":";
  appendJsonString(output, station.codec.size() <= 32 ? station.codec : std::string());
  output += ",\"bitrate\":" + std::to_string(station.bitrate);
  output += station.hls ? ",\"hls\":true" : ",\"hls\":false";
  output += ",\"favicon\":";
  appendJsonString(output, httpUrl(station.favicon, 255) ? station.favicon : "");
  output += "}";
}
}  // namespace

bool validQuery(const std::string& query) {
  if (query.empty() || query.size() > kMaxQueryBytes) return false;
  bool visible = false;
  for (unsigned char ch : query) {
    if (ch < 0x20 || ch == 0x7f) return false;
    if (ch != ' ') visible = true;
  }
  return visible;
}

bool validCountry(const std::string& country) {
  return country == "PL" || country == "ALL";
}

bool validLimit(unsigned limit) {
  return limit >= 1 && limit <= kMaxLimit;
}

std::string urlEncode(const std::string& text) {
  static const char hex[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(text.size() * 3);
  for (unsigned char ch : text) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      result.push_back(static_cast<char>(ch));
    } else {
      result.push_back('%');
      result.push_back(hex[ch >> 4]);
      result.push_back(hex[ch & 15]);
    }
  }
  return result;
}

std::string searchPath(const std::string& query, const std::string& country, unsigned limit) {
  std::string path = "/json/stations/search?name=" + urlEncode(query);
  if (country == "PL") path += "&countrycode=PL";
  path += "&limit=" + std::to_string(limit) + "&hidebroken=true";
  return path;
}

bool parseResponse(const char* input, size_t length, unsigned limit, std::string& output) {
  if (!input || length > kMaxResponseBytes || !validLimit(limit)) return false;
  Parser parser(input, length);
  if (!parser.take('[')) return false;
  output = "{\"results\":[";
  unsigned count = 0;
  bool closed = parser.take(']');
  if (!closed) {
    do {
      Station station;
      if (!parseStation(parser, station)) return false;
      if (usable(station) && count < limit) {
        if (count) output.push_back(',');
        appendStation(output, station);
        if (output.size() > kMaxOutputBytes) return false;
        ++count;
      }
      closed = parser.take(']');
      if (closed) break;
    } while (parser.take(','));
  }
  if (!closed || !parser.finished()) return false;
  output += "]}";
  return output.size() <= kMaxOutputBytes;
}

bool isSearchResultEnvelope(const char* body, size_t length) {
  constexpr char prefix[] = "{\"results\":[";
  constexpr size_t prefixLength = sizeof(prefix) - 1;
  if (!body || length < prefixLength + 2 || std::memcmp(body, prefix, prefixLength) != 0)
    return false;
  unsigned arrayDepth = 1;
  bool inString = false;
  bool escaped = false;
  for (size_t i = prefixLength; i < length; ++i) {
    const char ch = body[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (ch == '\\') escaped = true;
      else if (ch == '"') inString = false;
      continue;
    }
    if (ch == '"') inString = true;
    else if (ch == '[') ++arrayDepth;
    else if (ch == ']') {
      if (--arrayDepth == 0) return i + 2 == length && body[i + 1] == '}';
    }
  }
  return false;
}

ResponseStream::ResponseStream(unsigned limit) : limit_(limit) {
  output_.reserve(8192);
  object_.reserve(2048);
}

bool ResponseStream::addObject() {
  const std::string wrapped = "[" + object_ + "]";
  std::string mapped;
  if (!parseResponse(wrapped.data(), wrapped.size(), 1, mapped)) return false;
  constexpr size_t prefixLength = sizeof("{\"results\":[") - 1;
  if (mapped.size() < prefixLength + 2) return false;
  const size_t itemLength = mapped.size() - prefixLength - 2;
  if (itemLength && added_ < limit_) {
    if (added_) output_.push_back(',');
    output_.append(mapped, prefixLength, itemLength);
    if (output_.size() > kMaxOutputBytes - 2) return false;
    ++added_;
  }
  object_.clear();
  return true;
}

bool ResponseStream::write(const char* data, size_t length) {
  if (!data || !validLimit(limit_) || length > kMaxResponseBytes - rawBytes_) return false;
  rawBytes_ += length;
  for (size_t i = 0; i < length; ++i) {
    const char ch = data[i];
    if (state_ == State::IN_ITEM) {
      if (object_.size() >= 4096) return false;
      object_.push_back(ch);
      if (inString_) {
        if (escaped_) escaped_ = false;
        else if (ch == '\\') escaped_ = true;
        else if (ch == '"') inString_ = false;
      } else if (ch == '"') {
        inString_ = true;
      } else if (ch == '{') {
        ++objectDepth_;
      } else if (ch == '}') {
        if (--objectDepth_ == 0) {
          if (!addObject()) return false;
          state_ = State::SEPARATOR_OR_END;
        }
      }
      continue;
    }
    if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') continue;
    if (state_ == State::START && ch == '[') state_ = State::ITEM_OR_END;
    else if (state_ == State::ITEM_OR_END && ch == ']') state_ = State::DONE;
    else if (state_ == State::ITEM_OR_END && ch == '{') {
      object_ = "{";
      objectDepth_ = 1;
      inString_ = false;
      escaped_ = false;
      state_ = State::IN_ITEM;
    } else if (state_ == State::SEPARATOR_OR_END && ch == ',')
      state_ = State::ITEM_OR_END;
    else if (state_ == State::SEPARATOR_OR_END && ch == ']')
      state_ = State::DONE;
    else return false;
  }
  return true;
}

bool ResponseStream::finish(std::string& output) {
  if (state_ != State::DONE) return false;
  output_ += "]}";
  output.swap(output_);
  return output.size() <= kMaxOutputBytes;
}
}  // namespace stationDirectory
