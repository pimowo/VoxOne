#include "../src/network/dlna_client.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace {

template <typename Parser>
bool writeChunks(Parser& parser, const std::string& input, size_t chunk) {
  for (size_t offset = 0; offset < input.size(); offset += chunk) {
    const size_t length = input.size() - offset < chunk
                              ? input.size() - offset
                              : chunk;
    if (!parser.write(input.data() + offset, length)) return false;
  }
  return true;
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
  using namespace voxone::dlna;

  const std::string ssdp =
      "HTTP/1.1 200 OK\r\n"
      "CACHE-CONTROL: max-age=1800\r\n"
      "LOCATION: http://192.168.1.20:8200/rootDesc.xml\r\n"
      "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n"
      "USN: uuid:test::urn:schemas-upnp-org:device:MediaServer:1\r\n\r\n";
  SsdpResponseParser ssdpParser;
  assert(writeChunks(ssdpParser, ssdp, 3));
  SsdpServer server;
  assert(ssdpParser.finish(server));
  assert(std::string(server.location) ==
         "http://192.168.1.20:8200/rootDesc.xml");

  const std::string descriptionXml =
      "<?xml version=\"1.0\"?>"
      "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
      "<URLBase>http://192.168.1.20:8200/</URLBase>"
      "<device><friendlyName>Domowy MiniDLNA</friendlyName><serviceList>"
      "<service><serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>"
      "<controlURL>/ctl/ConnectionMgr</controlURL></service>"
      "<service><serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>"
      "<serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>"
      "<controlURL>/ctl/ContentDir</controlURL></service>"
      "</serviceList></device></root>";
  DeviceDescriptionParser descriptionParser;
  assert(writeChunks(descriptionParser, descriptionXml, 5));
  DeviceDescription description;
  assert(descriptionParser.finish(description));
  assert(std::string(description.friendlyName) == "Domowy MiniDLNA");
  assert(std::string(description.controlUrl) == "/ctl/ContentDir");
  char controlUrl[384];
  assert(resolveUrl(server.location, description.urlBase,
                    description.controlUrl, controlUrl, sizeof(controlUrl)));
  assert(std::string(controlUrl) == "http://192.168.1.20:8200/ctl/ContentDir");
  assert(resolveUrl("http://host:8200/descriptions/root.xml", "", "ctl/content",
                    controlUrl, sizeof(controlUrl)));
  assert(std::string(controlUrl) ==
         "http://host:8200/descriptions/ctl/content");

  const std::string soap =
      "<?xml version=\"1.0\"?>"
      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
      "<s:Body><u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
      "<Result>&lt;DIDL-Lite xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot;&gt;"
      "&lt;container id=&quot;1&quot; parentID=&quot;0&quot;&gt;"
      "&lt;dc:title&gt;Muzyka &amp;amp; podcasty&lt;/dc:title&gt;"
      "&lt;res protocolInfo=&quot;http-get:*:image/jpeg:*&quot;&gt;"
      "http://192.168.1.20:8200/folder.jpg&lt;/res&gt;&lt;/container&gt;"
      "&lt;item id=&quot;2$15&quot; parentID=&quot;0&quot;&gt;"
      "&lt;dc:title&gt;Koncert.flac&lt;/dc:title&gt;"
      "&lt;res protocolInfo=&quot;http-get:*:audio/flac:*&quot;&gt;"
      "http://192.168.1.20:8200/MediaItems/15.flac&lt;/res&gt;"
      "&lt;/item&gt;&lt;/DIDL-Lite&gt;</Result>"
      "<NumberReturned>2</NumberReturned><TotalMatches>2</TotalMatches>"
      "<UpdateID>7</UpdateID></u:BrowseResponse></s:Body></s:Envelope>";
  BrowseResponseParser browseParser;
  assert(writeChunks(browseParser, soap, 7));
  BrowseListing listing;
  assert(browseParser.finish(listing));
  assert(listing.count == 2);
  assert(listing.numberReturned == 2 && listing.totalMatches == 2);
  assert(listing.items[0].container);
  assert(std::string(listing.items[0].id) == "1");
  assert(std::string(listing.items[0].title) == "Muzyka & podcasty");
  assert(listing.items[0].resourceUrl[0] == '\0');
  assert(!listing.items[1].container);
  assert(std::string(listing.items[1].title) == "Koncert.flac");
  assert(std::string(listing.items[1].mime) == "audio/flac");
  assert(std::string(listing.items[1].resourceUrl).find("15.flac") !=
         std::string::npos);

  // Some servers wrap raw DIDL-Lite in CDATA instead of XML-escaping it.
  // A one-byte feed exercises delimiter splits across every chunk boundary.
  const std::string cdataSoap =
      "<Envelope><Body><BrowseResponse><Result><![CDATA["
      "<DIDL-Lite><container id=\"9\" parentID=\"0\">"
      "<title>Podcasty</title></container></DIDL-Lite>"
      "]]></Result><NumberReturned>1</NumberReturned>"
      "<TotalMatches>1</TotalMatches></BrowseResponse></Body></Envelope>";
  BrowseResponseParser cdataParser;
  assert(writeChunks(cdataParser, cdataSoap, 1));
  BrowseListing cdataListing;
  assert(cdataParser.finish(cdataListing));
  assert(cdataListing.count == 1 && cdataListing.items[0].container);
  assert(std::string(cdataListing.items[0].id) == "9");
  assert(std::string(cdataListing.items[0].title) == "Podcasty");

  // Raw UTF-8 and numeric XML entities must reach the display model as UTF-8.
  const std::string polishSoap =
      "<Envelope><Body><BrowseResponse><Result>"
      "&lt;DIDL-Lite&gt;"
      "&lt;item id=&quot;10&quot; parentID=&quot;0&quot;&gt;"
      "&lt;title&gt;Zażółć gęślą jaźń&lt;/title&gt;"
      "&lt;res protocolInfo=&quot;http-get:*:audio/flac:*&quot;&gt;x&lt;/res&gt;"
      "&lt;/item&gt;"
      "&lt;item id=&quot;11&quot; parentID=&quot;0&quot;&gt;"
      "&lt;title&gt;G&amp;#281;śl&amp;#261; &amp;#x141;&amp;#xF3;dź&lt;/title&gt;"
      "&lt;res protocolInfo=&quot;http-get:*:audio/flac:*&quot;&gt;y&lt;/res&gt;"
      "&lt;/item&gt;&lt;/DIDL-Lite&gt;"
      "</Result><NumberReturned>2</NumberReturned><TotalMatches>2</TotalMatches>"
      "</BrowseResponse></Body></Envelope>";
  BrowseResponseParser polishParser;
  assert(writeChunks(polishParser, polishSoap, 1));
  BrowseListing polishListing;
  assert(polishParser.finish(polishListing));
  assert(polishListing.count == 2);
  assert(std::string(polishListing.items[0].title) == "Zażółć gęślą jaźń");
  assert(std::string(polishListing.items[1].title) == "Gęślą Łódź");

  assert(validObjectId("0"));
  assert(validObjectId("64$2"));
  assert(!validObjectId(""));
  assert(!validObjectId("bad\nvalue"));

  DeviceDescriptionParser incomplete;
  assert(incomplete.write("<root><device>", 14));
  assert(!incomplete.finish(description));
  BrowseResponseParser missingResult;
  const char* emptySoap = "<Envelope><Body/></Envelope>";
  assert(missingResult.write(emptySoap, std::strlen(emptySoap)));
  assert(!missingResult.finish(listing));

  const std::string serverSource = readFile("src/core/netserver.cpp");
  assert(serverSource.find("dlna::kPlayRoute") != std::string::npos);
  assert(serverSource.find("Source::Dlna") != std::string::npos);
  const std::string menuSource = readFile("src/core/dlna_menu_controller.cpp");
  assert(menuSource.find("copyBrowseItem") != std::string::npos);
  assert(menuSource.find("request.source = temporary_audio::Source::Dlna") !=
         std::string::npos);
  assert(menuSource.find("parentHistory") != std::string::npos);
  const std::string web = readFile("web-src/voxone.js");
  assert(web.find("/api/dlna/browse?parent=") != std::string::npos);
  assert(web.find("/api/dlna/play") != std::string::npos);
  assert(web.find("data-dlna-index") != std::string::npos);
  return 0;
}
