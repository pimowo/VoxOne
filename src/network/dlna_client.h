#ifndef VOXONE_DLNA_CLIENT_H
#define VOXONE_DLNA_CLIENT_H

#include <cstddef>
#include <cstdint>

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace voxone {
namespace dlna {

constexpr char kBrowseRoute[] = "/api/dlna/browse";
constexpr char kPlayRoute[] = "/api/dlna/play";
constexpr size_t kMaxBrowseItems = 24;

struct SsdpServer {
  char location[384] = {};
};

class SsdpResponseParser {
 public:
  bool write(const char* data, size_t length);
  bool finish(SsdpServer& server);

 private:
  bool finishLine();
  char line_[512] = {};
  size_t length_ = 0;
  bool invalid_ = false;
  bool mediaServer_ = false;
  char location_[sizeof(SsdpServer::location)] = {};
};

struct DeviceDescription {
  char friendlyName[128] = {};
  char urlBase[384] = {};
  char controlUrl[384] = {};
};

struct BrowseItem {
  bool container = false;
  char id[96] = {};
  char parentId[96] = {};
  char title[192] = {};
  char resourceUrl[384] = {};
  char mime[96] = {};
};

struct BrowseListing {
  BrowseItem items[kMaxBrowseItems];
  size_t count = 0;
  uint32_t numberReturned = 0;
  uint32_t totalMatches = 0;
  bool truncated = false;
};

namespace detail {

class XmlCallbacks {
 public:
  virtual ~XmlCallbacks() = default;
  virtual void xmlOpen(const char* name) = 0;
  virtual void xmlAttribute(const char* name, const char* value) = 0;
  virtual void xmlText(const char* data, size_t length) = 0;
  virtual void xmlClose(const char* name) = 0;
};

// A bounded SAX tokenizer. It retains only the current tag/entity and emits
// character data immediately, so response size does not determine RAM use.
class XmlStreamParser {
 public:
  explicit XmlStreamParser(XmlCallbacks& callbacks);
  bool write(const char* data, size_t length);
  bool finish();

 private:
  enum class State : uint8_t { Text, Markup, Entity, Comment, Cdata };
  bool writeChar(char ch);
  bool finishMarkup();
  bool finishEntity();
  bool emit(char ch);
  bool appendToken(char ch);

  XmlCallbacks& callbacks_;
  State state_ = State::Text;
  char token_[768] = {};
  size_t tokenLength_ = 0;
  char quote_ = 0;
  uint8_t closeMatch_ = 0;
  int depth_ = 0;
  bool invalid_ = false;
};

}  // namespace detail

class DeviceDescriptionParser : private detail::XmlCallbacks {
 public:
  DeviceDescriptionParser();
  bool write(const char* data, size_t length);
  bool finish(DeviceDescription& description);

 private:
  enum class Capture : uint8_t { None, FriendlyName, UrlBase, ServiceType, ControlUrl };
  void xmlOpen(const char* name) override;
  void xmlAttribute(const char*, const char*) override {}
  void xmlText(const char* data, size_t length) override;
  void xmlClose(const char* name) override;
  void startCapture(Capture capture, char* target, size_t capacity);

  detail::XmlStreamParser parser_;
  DeviceDescription result_;
  Capture capture_ = Capture::None;
  char* captureTarget_ = nullptr;
  size_t captureCapacity_ = 0;
  size_t captureLength_ = 0;
  unsigned serviceDepth_ = 0;
  char serviceType_[160] = {};
  char serviceControl_[384] = {};
};

class BrowseResponseParser : private detail::XmlCallbacks {
 public:
  BrowseResponseParser();
  bool write(const char* data, size_t length);
  bool finish(BrowseListing& listing);

 private:
  class DidlCallbacks : public detail::XmlCallbacks {
   public:
    explicit DidlCallbacks(BrowseResponseParser& owner) : owner_(owner) {}
    void xmlOpen(const char* name) override;
    void xmlAttribute(const char* name, const char* value) override;
    void xmlText(const char* data, size_t length) override;
    void xmlClose(const char* name) override;

   private:
    BrowseResponseParser& owner_;
  };

  enum class SoapCapture : uint8_t { None, Result, NumberReturned, TotalMatches };
  enum class DidlCapture : uint8_t { None, Title, Resource };
  void xmlOpen(const char* name) override;
  void xmlAttribute(const char*, const char*) override {}
  void xmlText(const char* data, size_t length) override;
  void xmlClose(const char* name) override;
  void appendDidlText(const char* data, size_t length);
  void finishItem();

  DidlCallbacks didlCallbacks_;
  detail::XmlStreamParser soapParser_;
  detail::XmlStreamParser didlParser_;
  BrowseListing listing_;
  BrowseItem current_;
  SoapCapture soapCapture_ = SoapCapture::None;
  DidlCapture didlCapture_ = DidlCapture::None;
  bool inItem_ = false;
  bool sawResult_ = false;
  char number_[16] = {};
  size_t numberLength_ = 0;
};

bool resolveUrl(const char* descriptionUrl, const char* urlBase,
                const char* controlUrl, char* output, size_t capacity);
bool validObjectId(const char* value);

#ifdef ARDUINO
enum class StartResult : uint8_t { Started, Busy, Offline, NoMemory };
bool init();
StartResult startBrowse(const char* parent, uint32_t& job);
bool pollBrowse(uint32_t job, int& status, String& body, bool& ready);
bool pollBrowseItems(uint32_t job, int& status, bool& ready, size_t& count);
bool copyBrowseItem(uint32_t job, size_t index, BrowseItem& item);
#endif

}  // namespace dlna
}  // namespace voxone

#endif
