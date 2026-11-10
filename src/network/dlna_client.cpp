#include "dlna_client.h"

#include "../core/utf8_text.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#ifdef ARDUINO
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_heap_caps.h>
#include <freertos/semphr.h>
#include <memory>
#include <new>

#include "../core/version.h"
#include "../hardware/hardware_descriptor.h"
#endif

namespace voxone {
namespace dlna {
namespace {

bool equalsIgnoreCase(const char* left, const char* right) {
  if (!left || !right) return false;
  while (*left && *right) {
    if (std::tolower(static_cast<unsigned char>(*left++)) !=
        std::tolower(static_cast<unsigned char>(*right++))) return false;
  }
  return *left == *right;
}

bool containsIgnoreCase(const char* text, const char* needle) {
  if (!text || !needle || !*needle) return false;
  const size_t needleLength = std::strlen(needle);
  for (; *text; ++text) {
    size_t index = 0;
    while (index < needleLength && text[index] &&
           std::tolower(static_cast<unsigned char>(text[index])) ==
               std::tolower(static_cast<unsigned char>(needle[index]))) ++index;
    if (index == needleLength) return true;
  }
  return false;
}

const char* localName(const char* name) {
  const char* colon = name ? std::strrchr(name, ':') : nullptr;
  return colon ? colon + 1 : name;
}

void trim(char* value) {
  if (!value) return;
  char* first = value;
  while (*first && std::isspace(static_cast<unsigned char>(*first))) ++first;
  if (first != value) std::memmove(value, first, std::strlen(first) + 1);
  size_t length = std::strlen(value);
  while (length && std::isspace(static_cast<unsigned char>(value[length - 1])))
    value[--length] = '\0';
}

void copyBounded(char* output, size_t capacity, const char* value) {
  if (!output || !capacity) return;
  if (!value) value = "";
  const size_t length = std::min(std::strlen(value), capacity - 1);
  std::memcpy(output, value, length);
  output[length] = '\0';
}

void appendBounded(char* output, size_t capacity, const char* data, size_t length) {
  if (!output || !capacity || !data || !length) return;
  const size_t used = std::strlen(output);
  if (used >= capacity - 1) return;
  const size_t copied = std::min(length, capacity - used - 1);
  std::memcpy(output + used, data, copied);
  output[used + copied] = '\0';
}

bool parseUnsigned(const char* value, uint32_t& output) {
  if (!value || !*value) return false;
  errno = 0;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (errno || !end || *end || parsed > UINT32_MAX) return false;
  output = static_cast<uint32_t>(parsed);
  return true;
}

size_t decodeEntity(const char* entity, char output[4]) {
  if (!output) return 0;
  if (equalsIgnoreCase(entity, "lt")) output[0] = '<';
  else if (equalsIgnoreCase(entity, "gt")) output[0] = '>';
  else if (equalsIgnoreCase(entity, "amp")) output[0] = '&';
  else if (equalsIgnoreCase(entity, "quot")) output[0] = '"';
  else if (equalsIgnoreCase(entity, "apos")) output[0] = '\'';
  else if (entity && entity[0] == '#') {
    const bool hex = entity[1] == 'x' || entity[1] == 'X';
    char* end = nullptr;
    const unsigned long code = std::strtoul(entity + (hex ? 2 : 1), &end,
                                            hex ? 16 : 10);
    if (!end || *end || code > 0x10ffff) return 0;
    return voxone::utf8::encodeCodepoint(static_cast<uint32_t>(code), output);
  } else return 0;
  return 1;
}

void decodeAttribute(const char* input, char* output, size_t capacity) {
  if (!output || !capacity) return;
  size_t used = 0;
  for (size_t pos = 0; input && input[pos] && used + 1 < capacity;) {
    if (input[pos] != '&') {
      output[used++] = input[pos++];
      continue;
    }
    const char* semicolon = std::strchr(input + pos + 1, ';');
    if (!semicolon || static_cast<size_t>(semicolon - input - pos) > 15) {
      output[used++] = input[pos++];
      continue;
    }
    char entity[16] = {};
    const size_t length = static_cast<size_t>(semicolon - input - pos - 1);
    std::memcpy(entity, input + pos + 1, length);
    char decoded[4] = {};
    const size_t decodedLength = decodeEntity(entity, decoded);
    if (decodedLength && decodedLength <= capacity - used - 1) {
      std::memcpy(output + used, decoded, decodedLength);
      used += decodedLength;
    } else {
      output[used++] = input[pos];
    }
    pos = static_cast<size_t>(semicolon - input) + 1;
  }
  output[used] = '\0';
}

}  // namespace

bool SsdpResponseParser::finishLine() {
  line_[length_] = '\0';
  while (length_ && (line_[length_ - 1] == '\r' || line_[length_ - 1] == '\n'))
    line_[--length_] = '\0';
  char* colon = std::strchr(line_, ':');
  if (colon) {
    *colon = '\0';
    char* value = colon + 1;
    while (*value && std::isspace(static_cast<unsigned char>(*value))) ++value;
    trim(line_);
    trim(value);
    if (equalsIgnoreCase(line_, "location"))
      copyBounded(location_, sizeof(location_), value);
    else if ((equalsIgnoreCase(line_, "st") || equalsIgnoreCase(line_, "usn")) &&
             containsIgnoreCase(value, "MediaServer"))
      mediaServer_ = true;
  }
  length_ = 0;
  line_[0] = '\0';
  return true;
}

bool SsdpResponseParser::write(const char* data, size_t length) {
  if (invalid_ || !data) return false;
  for (size_t i = 0; i < length; ++i) {
    if (data[i] == '\n') {
      finishLine();
    } else if (length_ + 1 < sizeof(line_)) {
      line_[length_++] = data[i];
    } else {
      invalid_ = true;
      return false;
    }
  }
  return true;
}

bool SsdpResponseParser::finish(SsdpServer& server) {
  if (invalid_) return false;
  if (length_) finishLine();
  if (!mediaServer_ || !location_[0]) return false;
  copyBounded(server.location, sizeof(server.location), location_);
  return true;
}

namespace detail {

XmlStreamParser::XmlStreamParser(XmlCallbacks& callbacks) : callbacks_(callbacks) {}

bool XmlStreamParser::emit(char ch) {
  callbacks_.xmlText(&ch, 1);
  return true;
}

bool XmlStreamParser::appendToken(char ch) {
  if (tokenLength_ + 1 >= sizeof(token_)) {
    invalid_ = true;
    return false;
  }
  token_[tokenLength_++] = ch;
  token_[tokenLength_] = '\0';
  return true;
}

bool XmlStreamParser::finishEntity() {
  token_[tokenLength_] = '\0';
  char decoded[4] = {};
  const size_t decodedLength = decodeEntity(token_, decoded);
  if (!decodedLength) {
    emit('&');
    callbacks_.xmlText(token_, tokenLength_);
    emit(';');
  } else {
    callbacks_.xmlText(decoded, decodedLength);
  }
  tokenLength_ = 0;
  token_[0] = '\0';
  state_ = State::Text;
  return true;
}

bool XmlStreamParser::finishMarkup() {
  token_[tokenLength_] = '\0';
  char* cursor = token_;
  while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
  if (!*cursor || *cursor == '?' || *cursor == '!') return true;
  bool closing = false;
  if (*cursor == '/') {
    closing = true;
    ++cursor;
    while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
  }
  char* end = cursor + std::strlen(cursor);
  while (end > cursor && std::isspace(static_cast<unsigned char>(end[-1]))) *--end = '\0';
  bool selfClosing = false;
  if (!closing && end > cursor && end[-1] == '/') {
    selfClosing = true;
    *--end = '\0';
    while (end > cursor && std::isspace(static_cast<unsigned char>(end[-1]))) *--end = '\0';
  }
  char* nameEnd = cursor;
  while (*nameEnd && !std::isspace(static_cast<unsigned char>(*nameEnd))) ++nameEnd;
  const char saved = *nameEnd;
  *nameEnd = '\0';
  if (!*cursor) return false;
  if (closing) {
    callbacks_.xmlClose(cursor);
    if (depth_ <= 0) return false;
    --depth_;
    return true;
  }
  callbacks_.xmlOpen(cursor);
  *nameEnd = saved;
  char* attr = nameEnd;
  while (*attr) {
    while (*attr && std::isspace(static_cast<unsigned char>(*attr))) ++attr;
    if (!*attr) break;
    char* attrEnd = attr;
    while (*attrEnd && !std::isspace(static_cast<unsigned char>(*attrEnd)) &&
           *attrEnd != '=') ++attrEnd;
    char attrName[96] = {};
    const size_t attrNameLength = static_cast<size_t>(attrEnd - attr);
    if (!attrNameLength || attrNameLength >= sizeof(attrName)) return false;
    std::memcpy(attrName, attr, attrNameLength);
    attrName[attrNameLength] = '\0';
    attr = attrEnd;
    while (*attr && std::isspace(static_cast<unsigned char>(*attr))) ++attr;
    if (*attr != '=') return false;
    ++attr;
    while (*attr && std::isspace(static_cast<unsigned char>(*attr))) ++attr;
    char delimiter = 0;
    if (*attr == '\'' || *attr == '"') delimiter = *attr++;
    char* value = attr;
    if (delimiter) {
      while (*attr && *attr != delimiter) ++attr;
      if (!*attr) return false;
    } else {
      while (*attr && !std::isspace(static_cast<unsigned char>(*attr))) ++attr;
    }
    const char valueEnd = *attr;
    *attr = '\0';
    char decoded[512] = {};
    decodeAttribute(value, decoded, sizeof(decoded));
    callbacks_.xmlAttribute(attrName, decoded);
    *attr = valueEnd;
    if (delimiter && *attr == delimiter) ++attr;
  }
  ++depth_;
  if (selfClosing) {
    // cursor remains NUL terminated because attributes are no longer needed.
    *nameEnd = '\0';
    callbacks_.xmlClose(cursor);
    --depth_;
  }
  return true;
}

bool XmlStreamParser::writeChar(char ch) {
  if (invalid_) return false;
  switch (state_) {
    case State::Text:
      if (ch == '<') {
        state_ = State::Markup;
        tokenLength_ = 0;
        quote_ = 0;
      } else if (ch == '&') {
        state_ = State::Entity;
        tokenLength_ = 0;
      } else emit(ch);
      return true;
    case State::Entity:
      if (ch == ';') return finishEntity();
      if (ch == '<' || tokenLength_ + 1 >= sizeof(token_)) {
        emit('&');
        callbacks_.xmlText(token_, tokenLength_);
        tokenLength_ = 0;
        state_ = State::Text;
        return writeChar(ch);
      }
      return appendToken(ch);
    case State::Markup:
      if (!quote_ && ch == '>' && tokenLength_ > 0) {
        const bool ok = finishMarkup();
        tokenLength_ = 0;
        token_[0] = '\0';
        state_ = State::Text;
        if (!ok) invalid_ = true;
        return ok;
      }
      if ((ch == '\'' || ch == '"')) {
        if (!quote_) quote_ = ch;
        else if (quote_ == ch) quote_ = 0;
      }
      if (!appendToken(ch)) return false;
      if (tokenLength_ == 3 && std::memcmp(token_, "!--", 3) == 0) {
        state_ = State::Comment;
        tokenLength_ = 0;
        closeMatch_ = 0;
      } else if (tokenLength_ == 8 && std::memcmp(token_, "![CDATA[", 8) == 0) {
        state_ = State::Cdata;
        tokenLength_ = 0;
        closeMatch_ = 0;
      }
      return true;
    case State::Comment:
      if (closeMatch_ == 0) closeMatch_ = ch == '-' ? 1 : 0;
      else if (closeMatch_ == 1) closeMatch_ = ch == '-' ? 2 : 0;
      else if (ch == '>') { state_ = State::Text; closeMatch_ = 0; }
      else closeMatch_ = ch == '-' ? 2 : 0;
      return true;
    case State::Cdata:
      if (closeMatch_ == 0) {
        if (ch == ']') closeMatch_ = 1;
        else emit(ch);
      } else if (closeMatch_ == 1) {
        if (ch == ']') closeMatch_ = 2;
        else { emit(']'); closeMatch_ = 0; emit(ch); }
      } else if (ch == '>') {
        closeMatch_ = 0;
        state_ = State::Text;
      } else {
        emit(']');
        emit(']');
        closeMatch_ = ch == ']' ? 1 : 0;
        if (ch != ']') emit(ch);
      }
      return true;
  }
  return false;
}

bool XmlStreamParser::write(const char* data, size_t length) {
  if (!data || invalid_) return false;
  for (size_t i = 0; i < length; ++i)
    if (!writeChar(data[i])) return false;
  return true;
}

bool XmlStreamParser::finish() {
  if (state_ == State::Text && depth_ == 0 && !invalid_) return true;
  invalid_ = true;
  return false;
}

}  // namespace detail

DeviceDescriptionParser::DeviceDescriptionParser() : parser_(*this) {}

void DeviceDescriptionParser::startCapture(Capture capture, char* target,
                                           size_t capacity) {
  capture_ = capture;
  captureTarget_ = target;
  captureCapacity_ = capacity;
  captureLength_ = 0;
  if (target && capacity) target[0] = '\0';
}

void DeviceDescriptionParser::xmlOpen(const char* name) {
  const char* local = localName(name);
  if (equalsIgnoreCase(local, "service")) {
    ++serviceDepth_;
    serviceType_[0] = '\0';
    serviceControl_[0] = '\0';
  } else if (equalsIgnoreCase(local, "friendlyName")) {
    startCapture(Capture::FriendlyName, result_.friendlyName,
                 sizeof(result_.friendlyName));
  } else if (equalsIgnoreCase(local, "URLBase")) {
    startCapture(Capture::UrlBase, result_.urlBase, sizeof(result_.urlBase));
  } else if (serviceDepth_ && equalsIgnoreCase(local, "serviceType")) {
    startCapture(Capture::ServiceType, serviceType_, sizeof(serviceType_));
  } else if (serviceDepth_ && equalsIgnoreCase(local, "controlURL")) {
    startCapture(Capture::ControlUrl, serviceControl_, sizeof(serviceControl_));
  }
}

void DeviceDescriptionParser::xmlText(const char* data, size_t length) {
  if (!captureTarget_ || !captureCapacity_) return;
  const size_t copied = std::min(length, captureCapacity_ - 1 - captureLength_);
  if (copied) {
    std::memcpy(captureTarget_ + captureLength_, data, copied);
    captureLength_ += copied;
    captureTarget_[captureLength_] = '\0';
  }
}

void DeviceDescriptionParser::xmlClose(const char* name) {
  const char* local = localName(name);
  if (equalsIgnoreCase(local, "service")) {
    trim(serviceType_);
    trim(serviceControl_);
    if (!result_.controlUrl[0] &&
        containsIgnoreCase(serviceType_, "ContentDirectory") &&
        serviceControl_[0])
      copyBounded(result_.controlUrl, sizeof(result_.controlUrl), serviceControl_);
    if (serviceDepth_) --serviceDepth_;
  }
  const bool closingCapture =
      (capture_ == Capture::FriendlyName && equalsIgnoreCase(local, "friendlyName")) ||
      (capture_ == Capture::UrlBase && equalsIgnoreCase(local, "URLBase")) ||
      (capture_ == Capture::ServiceType && equalsIgnoreCase(local, "serviceType")) ||
      (capture_ == Capture::ControlUrl && equalsIgnoreCase(local, "controlURL"));
  if (closingCapture) {
    trim(captureTarget_);
    capture_ = Capture::None;
    captureTarget_ = nullptr;
    captureCapacity_ = captureLength_ = 0;
  }
}

bool DeviceDescriptionParser::write(const char* data, size_t length) {
  return parser_.write(data, length);
}

bool DeviceDescriptionParser::finish(DeviceDescription& description) {
  if (!parser_.finish() || !result_.controlUrl[0]) return false;
  description = result_;
  return true;
}

BrowseResponseParser::BrowseResponseParser()
    : didlCallbacks_(*this), soapParser_(*this), didlParser_(didlCallbacks_) {}

void BrowseResponseParser::xmlOpen(const char* name) {
  const char* local = localName(name);
  if (equalsIgnoreCase(local, "Result")) {
    soapCapture_ = SoapCapture::Result;
    sawResult_ = true;
  } else if (equalsIgnoreCase(local, "NumberReturned")) {
    soapCapture_ = SoapCapture::NumberReturned;
    numberLength_ = 0;
    number_[0] = '\0';
  } else if (equalsIgnoreCase(local, "TotalMatches")) {
    soapCapture_ = SoapCapture::TotalMatches;
    numberLength_ = 0;
    number_[0] = '\0';
  }
}

void BrowseResponseParser::xmlText(const char* data, size_t length) {
  if (soapCapture_ == SoapCapture::Result) {
    if (!didlParser_.write(data, length)) listing_.truncated = true;
  } else if (soapCapture_ == SoapCapture::NumberReturned ||
             soapCapture_ == SoapCapture::TotalMatches) {
    const size_t copied = std::min(length, sizeof(number_) - 1 - numberLength_);
    if (copied) {
      std::memcpy(number_ + numberLength_, data, copied);
      numberLength_ += copied;
      number_[numberLength_] = '\0';
    }
  }
}

void BrowseResponseParser::xmlClose(const char* name) {
  const char* local = localName(name);
  if (soapCapture_ == SoapCapture::Result && equalsIgnoreCase(local, "Result")) {
    soapCapture_ = SoapCapture::None;
  } else if (soapCapture_ == SoapCapture::NumberReturned &&
             equalsIgnoreCase(local, "NumberReturned")) {
    trim(number_);
    parseUnsigned(number_, listing_.numberReturned);
    soapCapture_ = SoapCapture::None;
  } else if (soapCapture_ == SoapCapture::TotalMatches &&
             equalsIgnoreCase(local, "TotalMatches")) {
    trim(number_);
    parseUnsigned(number_, listing_.totalMatches);
    soapCapture_ = SoapCapture::None;
  }
}

void BrowseResponseParser::DidlCallbacks::xmlOpen(const char* name) {
  const char* local = localName(name);
  if (equalsIgnoreCase(local, "container") || equalsIgnoreCase(local, "item")) {
    owner_.current_ = BrowseItem{};
    owner_.current_.container = equalsIgnoreCase(local, "container");
    owner_.inItem_ = true;
    owner_.didlCapture_ = DidlCapture::None;
  } else if (owner_.inItem_ && equalsIgnoreCase(local, "title")) {
    owner_.current_.title[0] = '\0';
    owner_.didlCapture_ = DidlCapture::Title;
  } else if (owner_.inItem_ && !owner_.current_.container &&
             equalsIgnoreCase(local, "res")) {
    owner_.current_.resourceUrl[0] = '\0';
    owner_.didlCapture_ = DidlCapture::Resource;
  }
}

void BrowseResponseParser::DidlCallbacks::xmlAttribute(const char* name,
                                                       const char* value) {
  if (!owner_.inItem_) return;
  const char* local = localName(name);
  if (equalsIgnoreCase(local, "id"))
    copyBounded(owner_.current_.id, sizeof(owner_.current_.id), value);
  else if (equalsIgnoreCase(local, "parentID"))
    copyBounded(owner_.current_.parentId, sizeof(owner_.current_.parentId), value);
  else if (owner_.didlCapture_ == DidlCapture::Resource &&
           equalsIgnoreCase(local, "protocolInfo")) {
    const char* first = std::strchr(value, ':');
    const char* second = first ? std::strchr(first + 1, ':') : nullptr;
    const char* third = second ? std::strchr(second + 1, ':') : nullptr;
    if (second && third && third > second + 1) {
      const size_t length = std::min(static_cast<size_t>(third - second - 1),
                                     sizeof(owner_.current_.mime) - 1);
      std::memcpy(owner_.current_.mime, second + 1, length);
      owner_.current_.mime[length] = '\0';
    }
  }
}

void BrowseResponseParser::DidlCallbacks::xmlText(const char* data, size_t length) {
  if (!owner_.inItem_) return;
  if (owner_.didlCapture_ == DidlCapture::Title)
    appendBounded(owner_.current_.title, sizeof(owner_.current_.title), data, length);
  else if (owner_.didlCapture_ == DidlCapture::Resource)
    appendBounded(owner_.current_.resourceUrl,
                  sizeof(owner_.current_.resourceUrl), data, length);
}

void BrowseResponseParser::DidlCallbacks::xmlClose(const char* name) {
  const char* local = localName(name);
  if (equalsIgnoreCase(local, "title") || equalsIgnoreCase(local, "res"))
    owner_.didlCapture_ = DidlCapture::None;
  if (owner_.inItem_ &&
      (equalsIgnoreCase(local, "container") || equalsIgnoreCase(local, "item")))
    owner_.finishItem();
}

void BrowseResponseParser::finishItem() {
  voxone::utf8::trimIncompleteTail(current_.title);
  trim(current_.title);
  trim(current_.resourceUrl);
  if (listing_.count < kMaxBrowseItems) listing_.items[listing_.count++] = current_;
  else listing_.truncated = true;
  inItem_ = false;
  didlCapture_ = DidlCapture::None;
}

bool BrowseResponseParser::write(const char* data, size_t length) {
  return soapParser_.write(data, length);
}

bool BrowseResponseParser::finish(BrowseListing& listing) {
  if (!soapParser_.finish() || !sawResult_ || !didlParser_.finish()) return false;
  if (listing_.numberReturned == 0) listing_.numberReturned = listing_.count;
  if (listing_.totalMatches == 0) listing_.totalMatches = listing_.numberReturned;
  listing = listing_;
  return true;
}

bool validObjectId(const char* value) {
  if (!value || !*value || std::strlen(value) > 95) return false;
  for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value);
       *cursor; ++cursor)
    if (*cursor < 0x20 || *cursor == 0x7f) return false;
  return true;
}

bool resolveUrl(const char* descriptionUrl, const char* urlBase,
                const char* controlUrl, char* output, size_t capacity) {
  if (!descriptionUrl || !controlUrl || !output || !capacity) return false;
  if (std::strncmp(controlUrl, "http://", 7) == 0) {
    if (std::strlen(controlUrl) >= capacity) return false;
    copyBounded(output, capacity, controlUrl);
    return true;
  }
  const char* base = urlBase && *urlBase ? urlBase : descriptionUrl;
  if (std::strncmp(base, "http://", 7) != 0) return false;
  const char* authority = base + 7;
  const char* slash = std::strchr(authority, '/');
  const size_t originLength = slash ? static_cast<size_t>(slash - base)
                                    : std::strlen(base);
  size_t prefixLength = 0;
  bool addSlash = false;
  if (controlUrl[0] == '/') {
    prefixLength = originLength;
  } else {
    const char* pathEnd = std::strrchr(base, '/');
    if (!pathEnd || pathEnd < authority) {
      prefixLength = originLength;
      addSlash = true;
    } else {
      prefixLength = static_cast<size_t>(pathEnd - base + 1);
    }
  }
  const size_t controlLength = std::strlen(controlUrl);
  const size_t required = prefixLength + (addSlash ? 1U : 0U) + controlLength;
  if (required >= capacity) return false;
  std::memcpy(output, base, prefixLength);
  size_t used = prefixLength;
  if (addSlash) output[used++] = '/';
  std::memcpy(output + used, controlUrl, controlLength);
  output[used + controlLength] = '\0';
  return true;
}

}  // namespace dlna
}  // namespace voxone

#ifdef ARDUINO
namespace voxone {
namespace dlna {
namespace {

const IPAddress kSsdpAddress(239, 255, 255, 250);
constexpr uint16_t kSsdpPort = 1900;
constexpr uint32_t kDiscoveryTimeoutMs = 3200;
constexpr uint32_t kConnectTimeoutMs = 2500;
constexpr uint16_t kReadTimeoutMs = 5000;
constexpr size_t kMaxXmlBytes = 256 * 1024;
constexpr uint32_t kWorkerStackBytes = 12288;
constexpr char kUserAgent[] = "VoxOne/" VOXONE_VERSION;

SemaphoreHandle_t stateMutex = nullptr;
uint32_t nextJob = 0;
uint32_t activeJob = 0;
bool running = false;
bool completed = false;
int completedStatus = 0;
std::string completedBody;
BrowseListing* completedListing = nullptr;

struct BrowseParams {
  char parent[96];
  uint32_t job;
};

template <typename T>
struct RuntimeObjectDeleter {
  void operator()(T* object) const {
    if (!object) return;
    object->~T();
    heap_caps_free(object);
  }
};

template <typename T>
using RuntimeObject = std::unique_ptr<T, RuntimeObjectDeleter<T>>;

template <typename T>
RuntimeObject<T> makeRuntimeObject() {
  void* memory = nullptr;
  if (voxone::hardware::hardwareCapabilities().supportsPsram && psramFound())
    memory = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) memory = heap_caps_malloc(sizeof(T), MALLOC_CAP_8BIT);
  return RuntimeObject<T>(memory ? new (memory) T() : nullptr);
}

void destroyBrowseListing(BrowseListing* listing) {
  if (!listing) return;
  listing->~BrowseListing();
  heap_caps_free(listing);
}

template <typename Parser>
class ParserStream : public Stream {
 public:
  explicit ParserStream(Parser& parser) : parser_(parser) {}
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    if (invalid_ || bytes_ + length > kMaxXmlBytes ||
        !parser_.write(reinterpret_cast<const char*>(data), length)) {
      invalid_ = true;
      return 0;
    }
    bytes_ += length;
    return length;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  bool invalid() const { return invalid_; }

 private:
  Parser& parser_;
  size_t bytes_ = 0;
  bool invalid_ = false;
};

bool sendDiscovery(WiFiUDP& udp) {
  static const char request[] =
      "M-SEARCH * HTTP/1.1\r\n"
      "HOST: 239.255.255.250:1900\r\n"
      "MAN: \"ssdp:discover\"\r\n"
      "MX: 2\r\n"
      "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n\r\n";
  return udp.beginPacket(kSsdpAddress, kSsdpPort) == 1 &&
         udp.write(reinterpret_cast<const uint8_t*>(request),
                   sizeof(request) - 1) == sizeof(request) - 1 &&
         udp.endPacket() == 1;
}

bool discoverMediaServer(SsdpServer& server) {
  WiFiUDP udp;
  if (!udp.begin(0)) return false;
  if (!sendDiscovery(udp)) {
    udp.stop();
    return false;
  }
  const uint32_t started = millis();
  bool repeated = false;
  while (millis() - started < kDiscoveryTimeoutMs) {
    if (!repeated && millis() - started >= 1200) {
      sendDiscovery(udp);
      repeated = true;
    }
    const int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      SsdpResponseParser parser;
      char chunk[192];
      while (udp.available() > 0) {
        const int received = udp.read(chunk, sizeof(chunk));
        if (received <= 0 || !parser.write(chunk, static_cast<size_t>(received))) break;
      }
      if (parser.finish(server)) {
        udp.stop();
        return true;
      }
    }
    delay(10);
  }
  udp.stop();
  return false;
}

template <typename Parser, typename Result>
bool httpGetXml(const char* url, Parser& parser, Result& result, int& status) {
  status = 502;
  if (!url || std::strncmp(url, "http://", 7) != 0) return false;
  WiFiClient client;
  client.setTimeout(kReadTimeoutMs / 1000);
  HTTPClient http;
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kReadTimeoutMs);
  http.setReuse(false);
  if (!http.begin(client, url)) return false;
  http.setUserAgent(kUserAgent);
  http.addHeader("Accept", "application/xml, text/xml");
  http.addHeader("Accept-Encoding", "identity");
  const int code = http.GET();
  if (code != 200) {
    status = code == HTTPC_ERROR_READ_TIMEOUT ? 504 : 502;
    http.end();
    client.stop();
    return false;
  }
  if (http.getSize() > static_cast<int>(kMaxXmlBytes)) {
    http.end();
    client.stop();
    return false;
  }
  ParserStream<Parser> sink(parser);
  const int received = http.writeToStream(&sink);
  const bool ok = received >= 0 && !sink.invalid() && parser.finish(result);
  status = received == HTTPC_ERROR_READ_TIMEOUT ? 504 : (ok ? 200 : 502);
  http.end();
  client.stop();
  return ok;
}

bool xmlEscape(const char* value, char* output, size_t capacity) {
  if (!output || !capacity) return false;
  size_t used = 0;
  output[0] = '\0';
  if (!value) return true;
  for (; *value; ++value) {
    const char* escaped = nullptr;
    switch (*value) {
      case '&': escaped = "&amp;"; break;
      case '<': escaped = "&lt;"; break;
      case '>': escaped = "&gt;"; break;
      case '"': escaped = "&quot;"; break;
      case '\'': escaped = "&apos;"; break;
      default:
        if (used + 1 >= capacity) return false;
        output[used++] = *value;
        continue;
    }
    const size_t escapedLength = std::strlen(escaped);
    if (used + escapedLength >= capacity) return false;
    std::memcpy(output + used, escaped, escapedLength);
    used += escapedLength;
  }
  output[used] = '\0';
  return true;
}

bool browseContentDirectory(const char* url, const char* parent,
                            BrowseListing& listing, int& status) {
  status = 502;
  if (!url || std::strncmp(url, "http://", 7) != 0) return false;
  char escapedParent[576]{};
  if (!xmlEscape(parent, escapedParent, sizeof(escapedParent))) return false;
  static constexpr char kRequestPrefix[] =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
      "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
      "<s:Body><u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
      "<ObjectID>";
  static constexpr char kRequestSuffix[] =
      "</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag>"
      "<Filter>*</Filter><StartingIndex>0</StartingIndex>"
      "<RequestedCount>0</RequestedCount><SortCriteria></SortCriteria>"
      "</u:Browse></s:Body></s:Envelope>";
  char request[1200];
  const int requestLength = std::snprintf(
      request, sizeof(request), "%s%s%s", kRequestPrefix, escapedParent,
      kRequestSuffix);
  if (requestLength <= 0 || static_cast<size_t>(requestLength) >= sizeof(request))
    return false;

  WiFiClient client;
  client.setTimeout(kReadTimeoutMs / 1000);
  HTTPClient http;
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kReadTimeoutMs);
  http.setReuse(false);
  if (!http.begin(client, url)) return false;
  http.setUserAgent(kUserAgent);
  http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
  http.addHeader("SOAPAction",
                 "\"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"");
  http.addHeader("Accept-Encoding", "identity");
  const int code = http.POST(reinterpret_cast<uint8_t*>(request),
                             static_cast<size_t>(requestLength));
  if (code != 200) {
    status = code == HTTPC_ERROR_READ_TIMEOUT ? 504 : 502;
    http.end();
    client.stop();
    return false;
  }
  if (http.getSize() > static_cast<int>(kMaxXmlBytes)) {
    http.end();
    client.stop();
    return false;
  }
  RuntimeObject<BrowseResponseParser> parser =
      makeRuntimeObject<BrowseResponseParser>();
  if (!parser) {
    status = 503;
    http.end();
    client.stop();
    return false;
  }
  ParserStream<BrowseResponseParser> sink(*parser);
  const int received = http.writeToStream(&sink);
  const bool ok = received >= 0 && !sink.invalid() && parser->finish(listing);
  status = received == HTTPC_ERROR_READ_TIMEOUT ? 504 : (ok ? 200 : 502);
  http.end();
  client.stop();
  return ok;
}

void appendJsonString(std::string& output, const char* value) {
  output += '"';
  for (const unsigned char* cursor =
           reinterpret_cast<const unsigned char*>(value ? value : "");
       *cursor; ++cursor) {
    switch (*cursor) {
      case '"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\b': output += "\\b"; break;
      case '\f': output += "\\f"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default:
        if (*cursor < 0x20) {
          char escaped[7];
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", *cursor);
          output += escaped;
        } else output += static_cast<char>(*cursor);
    }
  }
  output += '"';
}

size_t jsonEscapedSize(const char* value) {
  size_t size = 0;
  for (const unsigned char* cursor =
           reinterpret_cast<const unsigned char*>(value ? value : "");
       *cursor; ++cursor) {
    if (*cursor == '"' || *cursor == '\\' || *cursor == '\b' ||
        *cursor == '\f' || *cursor == '\n' || *cursor == '\r' ||
        *cursor == '\t')
      size += 2;
    else
      size += *cursor < 0x20 ? 6 : 1;
  }
  return size;
}

void appendUnsigned(std::string& output, uint32_t value) {
  char number[11];
  const int length = std::snprintf(number, sizeof(number), "%lu",
                                   static_cast<unsigned long>(value));
  if (length > 0) output.append(number, static_cast<size_t>(length));
}

std::string formatBrowseJson(const BrowseParams& params, const SsdpServer& server,
                             const DeviceDescription& description,
                             const char* controlUrl,
                             const BrowseListing& listing) {
  std::string output;
  size_t capacity = 256 + jsonEscapedSize(params.parent) +
      jsonEscapedSize(description.friendlyName) +
      jsonEscapedSize(server.location) + jsonEscapedSize(controlUrl);
  for (size_t index = 0; index < listing.count; ++index) {
    const BrowseItem& item = listing.items[index];
    capacity += 128 + jsonEscapedSize(item.id) +
        jsonEscapedSize(item.parentId) + jsonEscapedSize(item.title) +
        jsonEscapedSize(item.resourceUrl) + jsonEscapedSize(item.mime);
  }
  // One accurately bounded allocation replaces geometric growth while the
  // response is assembled after streaming XML parsing has completed.
  output.reserve(capacity);
  output = "{\"status\":\"ok\",\"job\":";
  appendUnsigned(output, params.job);
  output += ",\"parent\":";
  appendJsonString(output, params.parent);
  output += ",\"server\":{\"name\":";
  appendJsonString(output, description.friendlyName);
  output += ",\"location\":";
  appendJsonString(output, server.location);
  output += ",\"contentDirectory\":";
  appendJsonString(output, controlUrl);
  output += "},\"numberReturned\":";
  appendUnsigned(output, listing.numberReturned);
  output += ",\"totalMatches\":";
  appendUnsigned(output, listing.totalMatches);
  output += ",\"truncated\":";
  output += listing.truncated ? "true" : "false";
  output += ",\"items\":[";
  for (size_t index = 0; index < listing.count; ++index) {
    const BrowseItem& item = listing.items[index];
    if (index) output += ',';
    output += "{\"type\":\"";
    output += item.container ? "folder" : "file";
    output += "\",\"id\":";
    appendJsonString(output, item.id);
    output += ",\"parentId\":";
    appendJsonString(output, item.parentId);
    output += ",\"title\":";
    appendJsonString(output, item.title);
    output += ",\"url\":";
    appendJsonString(output, item.resourceUrl);
    output += ",\"mime\":";
    appendJsonString(output, item.mime);
    output += '}';
  }
  output += "]}";
  return output;
}

void finishJob(uint32_t job, int status, std::string&& body,
               BrowseListing* listing) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (running && activeJob == job) {
    destroyBrowseListing(completedListing);
    completedListing = listing;
    listing = nullptr;
    completedStatus = status;
    completedBody = std::move(body);
    running = false;
    completed = true;
  }
  xSemaphoreGive(stateMutex);
  destroyBrowseListing(listing);
}

void browseWorker(void* context) {
  std::unique_ptr<BrowseParams> params(static_cast<BrowseParams*>(context));
  int status = 502;
  std::string body;
  SsdpServer server;
  DeviceDescription description;
  RuntimeObject<BrowseListing> listing = makeRuntimeObject<BrowseListing>();
  char controlUrl[384] = {};
  if (!listing) {
    status = 503;
    body = "{\"error\":\"insufficient_memory\"}";
  } else if (WiFi.status() != WL_CONNECTED) {
    status = 503;
    body = "{\"error\":\"offline\"}";
  } else if (!discoverMediaServer(server)) {
    status = 504;
    body = "{\"error\":\"media_server_not_found\"}";
  } else {
    Serial.printf("##[DLNA]# MediaServer location=%s\n", server.location);
    RuntimeObject<DeviceDescriptionParser> parser =
        makeRuntimeObject<DeviceDescriptionParser>();
    if (!parser) {
      status = 503;
      body = "{\"error\":\"insufficient_memory\"}";
    } else if (!httpGetXml(server.location, *parser, description, status)) {
      body = "{\"error\":\"invalid_device_description\"}";
    } else if (!resolveUrl(server.location, description.urlBase,
                           description.controlUrl, controlUrl,
                           sizeof(controlUrl))) {
      status = 502;
      body = "{\"error\":\"invalid_content_directory_url\"}";
    } else if (!browseContentDirectory(controlUrl, params->parent, *listing, status)) {
      if (status == 504) body = "{\"error\":\"browse_timeout\"}";
      else if (status == 503) body = "{\"error\":\"insufficient_memory\"}";
      else body = "{\"error\":\"invalid_browse_response\"}";
    } else {
      status = 200;
      body = formatBrowseJson(*params, server, description, controlUrl, *listing);
      Serial.printf("##[DLNA]# server=%s parent=%s items=%u total=%lu%s\n",
                    description.friendlyName, params->parent,
                    static_cast<unsigned>(listing->count),
                    static_cast<unsigned long>(listing->totalMatches),
                    listing->truncated ? " truncated" : "");
    }
  }
  BrowseListing* completedItems = status == 200 ? listing.release() : nullptr;
  finishJob(params->job, status, std::move(body), completedItems);
  Serial.println("##[DLNA]# browse task done");
  vTaskDelete(nullptr);
}

}  // namespace

bool init() {
  if (!stateMutex) stateMutex = xSemaphoreCreateMutex();
  return stateMutex != nullptr;
}

StartResult startBrowse(const char* parent, uint32_t& job) {
  if (!init()) return StartResult::NoMemory;
  if (WiFi.status() != WL_CONNECTED) return StartResult::Offline;
  if (!validObjectId(parent)) return StartResult::NoMemory;
  BrowseParams* params = new (std::nothrow) BrowseParams{};
  if (!params) return StartResult::NoMemory;
  copyBounded(params->parent, sizeof(params->parent), parent);
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (running) {
    xSemaphoreGive(stateMutex);
    delete params;
    return StartResult::Busy;
  }
  activeJob = ++nextJob;
  if (!activeJob) activeJob = ++nextJob;
  params->job = activeJob;
  job = activeJob;
  running = true;
  completed = false;
  destroyBrowseListing(completedListing);
  completedListing = nullptr;
  // Retain the response capacity for the next bounded browse job. Releasing
  // and immediately reallocating this block was a recurring heap fragmenter.
  completedBody.clear();
  xSemaphoreGive(stateMutex);
  if (xTaskCreatePinnedToCore(browseWorker, "DlnaBrowse", kWorkerStackBytes,
                              params, 1, nullptr, 0) != pdPASS) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    if (activeJob == job) {
      running = false;
      activeJob = 0;
    }
    xSemaphoreGive(stateMutex);
    delete params;
    return StartResult::NoMemory;
  }
  return StartResult::Started;
}

bool pollBrowse(uint32_t job, int& status, String& body, bool& ready) {
  if (!stateMutex || !job) return false;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  const bool found = activeJob == job && (running || completed);
  if (found) {
    ready = completed;
    status = ready ? completedStatus : 202;
    if (ready && !completedBody.empty() &&
        !body.reserve(completedBody.size())) {
      status = 503;
      body = "{\"error\":\"insufficient_memory\"}";
    } else {
      body = ready ? completedBody.c_str() : "{\"status\":\"pending\"}";
    }
    if (ready && body.length() != completedBody.size()) {
      status = 503;
      body = "{\"error\":\"insufficient_memory\"}";
    }
  }
  xSemaphoreGive(stateMutex);
  return found;
}

bool pollBrowseItems(uint32_t job, int& status, bool& ready, size_t& count) {
  if (!stateMutex || !job) return false;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  const bool found = activeJob == job && (running || completed);
  if (found) {
    ready = completed;
    status = ready ? completedStatus : 202;
    count = ready && completedListing ? completedListing->count : 0;
  }
  xSemaphoreGive(stateMutex);
  return found;
}

bool copyBrowseItem(uint32_t job, size_t index, BrowseItem& item) {
  if (!stateMutex || !job) return false;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  const bool found = activeJob == job && completed && completedStatus == 200 &&
                     completedListing && index < completedListing->count;
  if (found) item = completedListing->items[index];
  xSemaphoreGive(stateMutex);
  return found;
}

}  // namespace dlna
}  // namespace voxone
#endif
