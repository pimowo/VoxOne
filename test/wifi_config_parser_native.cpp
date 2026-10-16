#include "../src/core/wifi_config_parser.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

using voxone::wifi_config::ParseStatus;
using voxone::wifi_config::Profile;
using voxone::wifi_config::kMaxProfiles;

voxone::wifi_config::ParseResult parse(const std::string& input,
                                       Profile (&profiles)[kMaxProfiles]) {
  return voxone::wifi_config::parseFile(
      reinterpret_cast<const uint8_t*>(input.data()), input.size(), profiles,
      kMaxProfiles);
}

void assertCleared(const Profile (&profiles)[kMaxProfiles]) {
  for (std::size_t index = 0; index < kMaxProfiles; ++index) {
    assert(profiles[index].ssid[0] == '\0');
    assert(profiles[index].password[0] == '\0');
  }
}

}  // namespace

int main() {
  Profile profiles[kMaxProfiles]{};

  auto result = parse("", profiles);
  assert(result.status == ParseStatus::Empty && result.profileCount == 0);

  result = parse("Home\tpassphrase\n", profiles);
  assert(result.status == ParseStatus::Ok && result.profileCount == 1);
  assert(std::strcmp(profiles[0].ssid, "Home") == 0);
  assert(std::strcmp(profiles[0].password, "passphrase") == 0);

  const std::string five =
      "one\talpha\n"
      "two\tbeta\n"
      "three\tgamma\n"
      "four\tdelta\n"
      "five\tepsilon\n";
  result = parse(five, profiles);
  assert(result.status == ParseStatus::Ok && result.profileCount == 5);
  assert(std::strcmp(profiles[0].ssid, "one") == 0);
  assert(std::strcmp(profiles[4].ssid, "five") == 0);

  result = parse(five + "six\tzeta\n", profiles);
  assert(result.status == ParseStatus::TooManyProfiles);
  assert(result.recordNumber == 6);
  assertCleared(profiles);

  result = parse(std::string(30, 's') + "\tvalue\n", profiles);
  assert(result.status == ParseStatus::SsidTooLong);
  assertCleared(profiles);

  result = parse("Home\t" + std::string(40, 'p') + "\n", profiles);
  assert(result.status == ParseStatus::PasswordTooLong);
  assertCleared(profiles);

  const std::string maximumRecord =
      std::string(29, 's') + "\t" + std::string(39, 'p') + "\n";
  result = parse(maximumRecord, profiles);
  assert(result.status == ParseStatus::Ok && result.profileCount == 1);
  assert(profiles[0].ssid[28] == 's' && profiles[0].ssid[29] == '\0');
  assert(profiles[0].password[38] == 'p' &&
         profiles[0].password[39] == '\0');

  result = parse("\tvalue\n", profiles);
  assert(result.status == ParseStatus::EmptySsid);
  assertCleared(profiles);

  result = parse("OpenNetwork\t\n", profiles);
  assert(result.status == ParseStatus::Ok && result.profileCount == 1);
  assert(std::strcmp(profiles[0].ssid, "OpenNetwork") == 0);
  assert(profiles[0].password[0] == '\0');

  result = parse("missing-separator\n", profiles);
  assert(result.status == ParseStatus::MissingSeparator);
  assertCleared(profiles);

  result = parse("Home\tvalue\textra\n", profiles);
  assert(result.status == ParseStatus::ExtraSeparator);
  assertCleared(profiles);

  result = parse("Home\tvalue", profiles);
  assert(result.status == ParseStatus::Ok && result.profileCount == 1);
  assert(std::strcmp(profiles[0].password, "value") == 0);

  std::string invalidUtf8 = "Home\t";
  invalidUtf8.push_back(static_cast<char>(0xc3));
  invalidUtf8.push_back(static_cast<char>(0x28));
  invalidUtf8.push_back('\n');
  result = parse(invalidUtf8, profiles);
  assert(result.status == ParseStatus::InvalidUtf8);
  assertCleared(profiles);

  const std::string validUtf8 =
      "Sie\xc4\x87\tza\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87\n";
  result = parse(validUtf8, profiles);
  assert(result.status == ParseStatus::Ok && result.profileCount == 1);
  assert(std::memcmp(profiles[0].ssid, validUtf8.data(), 5) == 0);

  const std::string existing =
      "Legacy24\tlegacy-pass\n"
      "OpenGuest\t\n";
  result = parse(existing, profiles);
  assert(result.status == ParseStatus::Ok && result.profileCount == 2);
  assert(std::strcmp(profiles[0].ssid, "Legacy24") == 0);
  assert(std::strcmp(profiles[1].ssid, "OpenGuest") == 0);
  assert(profiles[1].password[0] == '\0');

  return 0;
}
