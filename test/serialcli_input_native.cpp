#include "../src/core/serialcli_input.h"
#include "../src/core/serialcli_command_match.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

using voxone::serialcli::InputEvent;
using EnabledFramer = voxone::serialcli::InputFramer<true>;
using DisabledFramer = voxone::serialcli::InputFramer<false>;

std::vector<std::string> feed(EnabledFramer& framer,
                              const std::vector<uint8_t>& bytes,
                              uint32_t& nowMs) {
  std::vector<std::string> frames;
  for (const uint8_t byte : bytes) {
    const auto event = framer.feed(byte, nowMs++);
    if (event == InputEvent::FrameReady) frames.emplace_back(framer.frame());
  }
  return frames;
}

std::vector<uint8_t> bytes(const char* text) {
  return std::vector<uint8_t>(text, text + std::strlen(text));
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

void testValidAsciiAndSeparators() {
  EnabledFramer framer;
  uint32_t now = 10;
  auto frames = feed(framer, bytes("  info  \r\nplay 2\nstatus\r"), now);
  assert((frames == std::vector<std::string>{"info", "play 2", "status"}));
  assert(!framer.discarding());
}

void testNulRejectsWholeFrame() {
  EnabledFramer framer;
  uint32_t now = 0;
  std::vector<uint8_t> input = {'r', 'e', 's', 0x00, 'e', 't', '\n',
                                'i', 'n', 'f', 'o', '\n'};
  const auto frames = feed(framer, input, now);
  assert((frames == std::vector<std::string>{"info"}));

  EnabledFramer prefix;
  now = 0;
  input = {0x00, 'r', 'e', 's', 'e', 't', '\n'};
  assert(feed(prefix, input, now).empty());
}

void testHistoricalBinaryEvidenceIsNeverACommand() {
  EnabledFramer framer;
  uint32_t now = 100;
  std::vector<uint8_t> input(290, 0x00);
  for (unsigned index = 0; index < 94; ++index) {
    input.push_back(0x79);
    input.push_back(0x01);
  }
  input.push_back(0xcf);
  input.insert(input.end(), {'r', 'e', 'b', 'o', 'o', 't', '\n'});
  assert(feed(framer, input, now).empty());
  assert(!framer.discarding());
}

void testTimeoutNeverExecutesRemainder() {
  EnabledFramer framer;
  uint32_t now = 5;
  assert(feed(framer, bytes("re"), now).empty());
  assert(framer.tick(now + voxone::serialcli::kFrameTimeoutMs) ==
         InputEvent::FrameRejected);
  now += voxone::serialcli::kFrameTimeoutMs + 1;
  assert(feed(framer, bytes("set\n"), now).empty());
  assert(!framer.discarding());
  assert(feed(framer, bytes("status\n"), now) ==
         std::vector<std::string>{"status"});
}

void testOverflowRejectsWholeFrame() {
  EnabledFramer framer;
  uint32_t now = 0;
  std::vector<uint8_t> input(voxone::serialcli::kMaxFrameLength, 'a');
  input.push_back('b');
  input.insert(input.end(), {'\n', 'b', 'o', 'o', 't', '\n'});
  const auto frames = feed(framer, input, now);
  assert((frames == std::vector<std::string>{"boot"}));
}

void testNoSeparatorAndRandomData() {
  EnabledFramer incomplete;
  uint32_t now = 0;
  assert(feed(incomplete, bytes("reboot"), now).empty());
  assert(incomplete.tick(now + voxone::serialcli::kFrameTimeoutMs) ==
         InputEvent::FrameRejected);

  std::mt19937 random(0x564f584fU);
  EnabledFramer fuzzed;
  now = 0;
  for (unsigned round = 0; round < 200; ++round) {
    std::vector<uint8_t> input;
    input.push_back(static_cast<uint8_t>(random() & 0x1fU));
    const char command[] = "reset";
    input.insert(input.end(), command, command + sizeof(command) - 1);
    for (unsigned index = 0; index < 16; ++index)
      input.push_back(static_cast<uint8_t>(random() & 0xffU));
    input.push_back((round & 1U) ? '\r' : '\n');
    assert(feed(fuzzed, input, now).empty());
  }
}

void testRepeatedRejectedFramesResynchronize() {
  EnabledFramer framer;
  uint32_t now = 0;
  const std::vector<uint8_t> input = {
      0x01, 'b', 'o', 'o', 't', '\n',
      0x7f, 'r', 'e', 's', 'e', 't', '\r', '\n',
      'v', 'e', 'r', 's', 'i', 'o', 'n', '\n'};
  assert(feed(framer, input, now) ==
         std::vector<std::string>{"version"});
}

void testTxOnlyNeverProducesAFrame() {
  DisabledFramer framer;
  uint32_t now = 0;
  for (const uint8_t byte : bytes("reset\ninfo\r\n"))
    assert(framer.feed(byte, now++) == InputEvent::Disabled);
  assert(framer.tick(now + 5000) == InputEvent::Disabled);
  assert(framer.frameLength() == 0 && std::strcmp(framer.frame(), "") == 0);
  assert(!DisabledFramer::rxEnabled());
  assert(EnabledFramer::rxEnabled());
}

void testCommandErrorRateLimitAndWraparound() {
  voxone::serialcli::CommandErrorLimiter limiter;
  assert(limiter.allow(100));
  assert(!limiter.allow(1099));
  assert(limiter.allow(1100));

  voxone::serialcli::CommandErrorLimiter wrapping;
  assert(wrapping.allow(UINT32_MAX - 100));
  assert(!wrapping.allow(200));
  assert(wrapping.allow(900));
}

void testCompleteCommandSyntax() {
  int value = 0;
  int second = 0;
  assert(voxone::serialcli::matchInt("vol 20", "vol %d%n", value));
  assert(value == 20);
  assert(!voxone::serialcli::matchInt(
      "vol 20reset", "vol %d%n", value));
  assert(voxone::serialcli::matchTwoInts(
      "sleep 1 2", "sleep %d %d%n", value, second));
  assert(value == 1 && second == 2);
  assert(!voxone::serialcli::matchTwoInts(
      "sleep 1 2 reboot", "sleep %d %d%n", value, second));

  char ssid[50]{};
  char password[50]{};
  assert(voxone::serialcli::matchTwoStrings(
      "wifi Home secret", "wifi %49[^ ] %49s%n", ssid, password));
  assert(std::strcmp(ssid, "Home") == 0 &&
         std::strcmp(password, "secret") == 0);
  assert(!voxone::serialcli::matchTwoStrings(
      "wifi Home secret extra", "wifi %49[^ ] %49s%n", ssid, password));
}

void testUartAndVolumeSessionContractsRemainSeparate() {
  const auto link = readFile("src/core/bt_link.cpp");
  const auto profile = readFile("profiles/l0.h");
  const auto protocol = readFile("src/core/bt_link_protocol.cpp");
  const auto build = readFile("platformio.ini");
  assert(link.find("BtLinkBaud = 921600") != std::string::npos);
  assert(link.find("serial_(1)") != std::string::npos);
  assert(profile.find("VOXONE_BT_UART_RX_PIN 15") != std::string::npos);
  assert(profile.find("VOXONE_BT_UART_TX_PIN 16") != std::string::npos);
  assert(protocol.find("VOL_SESSION_BEGIN") != std::string::npos);
  assert(protocol.find("VOL_LOCAL_ACK") != std::string::npos);
  assert(build.find("[env:l0-tx-only]") != std::string::npos);
  assert(build.find("-DVOXONE_SERIALCLI_RX_ENABLED=0") != std::string::npos);
}

}  // namespace

int main() {
  testValidAsciiAndSeparators();
  testNulRejectsWholeFrame();
  testHistoricalBinaryEvidenceIsNeverACommand();
  testTimeoutNeverExecutesRemainder();
  testOverflowRejectsWholeFrame();
  testNoSeparatorAndRandomData();
  testRepeatedRejectedFramesResynchronize();
  testTxOnlyNeverProducesAFrame();
  testCommandErrorRateLimitAndWraparound();
  testCompleteCommandSyntax();
  testUartAndVolumeSessionContractsRemainSeparate();
  static_assert(sizeof(EnabledFramer) <= 144,
                "Serial CLI input state exceeded its RAM budget");
  static_assert(sizeof(DisabledFramer) == 1,
                "TX-only build should not retain the RX frame buffer");
  return 0;
}
