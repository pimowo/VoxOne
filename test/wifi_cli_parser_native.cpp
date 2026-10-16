#include "../src/core/wifi_cli_parser.h"
#include "../src/core/wifi_config_atomic.h"
#include "../src/core/wifi_config_parser.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

namespace {

using voxone::wifi_cli::ParseStatus;

ParseStatus parse(const char* command, char* ssid, char* password) {
  return voxone::wifi_cli::parseQuotedCredentials(command, ssid, 30,
                                                   password, 40);
}

struct FakeStorage {
  std::map<std::string, std::string> files;
  bool exists(const char* path) const { return files.count(path) != 0; }
  bool remove(const char* path) { return files.erase(path) == 1; }
  bool rename(const char* from, const char* to) {
    if (!exists(from) || exists(to)) return false;
    files[to] = files[from];
    files.erase(from);
    return true;
  }
  bool write(const char* path, const uint8_t* bytes, std::size_t size) {
    files[path] = std::string(reinterpret_cast<const char*>(bytes), size);
    return true;
  }
  bool matches(const char* path, const uint8_t* bytes, std::size_t size) const {
    const auto found = files.find(path);
    return found != files.end() && found->second.size() == size &&
           std::memcmp(found->second.data(), bytes, size) == 0;
  }
  bool validBytes(const uint8_t* bytes, std::size_t size) const {
    return voxone::wifi_config::validSerializedConfig(bytes, size);
  }
  bool valid(const char* path) const {
    const auto found = files.find(path);
    return found != files.end() && validBytes(
        reinterpret_cast<const uint8_t*>(found->second.data()),
        found->second.size());
  }
};

bool saveRecord(FakeStorage& storage, const char* ssid, const char* password) {
  if (!apWifiCredentialsValid(ssid, password)) return false;
  const std::string record = std::string(ssid) + '\t' + password + '\n';
  return voxone::wifi_config_atomic::replace(
      storage, reinterpret_cast<const uint8_t*>(record.data()), record.size());
}

}  // namespace

int main() {
  char ssid[30]{};
  char password[40]{};

  assert(parse("wifi.con(\"Open\",\"\")", ssid, password) ==
         ParseStatus::Valid);
  assert(std::strcmp(ssid, "Open") == 0);
  assert(std::strcmp(password, "") == 0);

  assert(parse("wifi.con(\"Home\",\"secret123\")", ssid, password) ==
         ParseStatus::Valid);
  assert(std::strcmp(ssid, "Home") == 0);
  assert(std::strcmp(password, "secret123") == 0);

  assert(parse("wifi.con(\"\",\"\")", ssid, password) ==
         ParseStatus::Invalid);
  assert(parse("wifi.con(\"Home\",\"secret123)", ssid, password) ==
         ParseStatus::Invalid);
  assert(parse("wifi.con(\"Home\")", ssid, password) ==
         ParseStatus::Invalid);

  const std::string longSsid(30, 's');
  const std::string longPassword(40, 'p');
  assert(parse(("wifi.con(\"" + longSsid + "\",\"\")").c_str(), ssid,
               password) == ParseStatus::Invalid);
  assert(parse(("wifi.con(\"Home\",\"" + longPassword + "\")").c_str(),
               ssid, password) == ParseStatus::Invalid);

  assert(parse("wifi.con(\"Home\\\"Lab\",\"secret123\")", ssid, password) ==
         ParseStatus::Invalid);
  assert(parse("wifi.con(\"Home\",\"secret123\") trailing", ssid,
               password) == ParseStatus::Invalid);
  const char invalidUtf8[] =
      "wifi.con(\"Home\",\"\xc3\x28\")";
  assert(parse(invalidUtf8, ssid, password) == ParseStatus::Invalid);
  assert(parse("wifi.con(Home,secret123)", ssid, password) ==
         ParseStatus::NotQuotedCommand);

  FakeStorage openStorage;
  assert(parse("wifi.con(\"Open\",\"\")", ssid, password) ==
         ParseStatus::Valid);
  assert(saveRecord(openStorage, ssid, password));
  assert(openStorage.files[voxone::wifi_config_atomic::kActivePath] ==
         "Open\t\n");

  FakeStorage wpaStorage;
  assert(parse("wifi.con(\"Home\",\"secret123\")", ssid, password) ==
         ParseStatus::Valid);
  assert(saveRecord(wpaStorage, ssid, password));
  assert(wpaStorage.files[voxone::wifi_config_atomic::kActivePath] ==
         "Home\tsecret123\n");
  return 0;
}
