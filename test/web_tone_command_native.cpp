#include "../src/core/web_tone_command.h"

#include <cassert>
#include <deque>
#include <fstream>
#include <iterator>
#include <string>

namespace {

using voxone::web_tone::Command;
using voxone::web_tone::Kind;
using voxone::web_tone::Values;

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string section(const std::string& source, const char* begin,
                    const char* end) {
  const std::size_t first = source.find(begin);
  assert(first != std::string::npos);
  const std::size_t last = source.find(end, first + 1);
  assert(last != std::string::npos && last > first);
  return source.substr(first, last - first);
}

Command single(Kind kind, int8_t value) {
  Command command{kind, {0, 0, 0}};
  if (kind == Kind::Bass) command.values.bass = value;
  if (kind == Kind::Middle) command.values.middle = value;
  if (kind == Kind::Treble) command.values.treble = value;
  return command;
}

void testValidation() {
  int8_t parsed = 0;
  assert(voxone::web_tone::parseSingleValue("-6", parsed) && parsed == -6);
  assert(voxone::web_tone::parseSingleValue("0", parsed) && parsed == 0);
  assert(voxone::web_tone::parseSingleValue("+6", parsed) && parsed == 6);
  const char* invalidValues[] = {
      nullptr, "", "7", "-7", "1x", " 1", "999999999999999999999"};
  for (const char* invalid : invalidValues)
    assert(!voxone::web_tone::parseSingleValue(invalid, parsed));

  assert(voxone::web_tone::valid(
      Command{Kind::Preset, {-6, 0, 6}}));
  assert(!voxone::web_tone::valid(
      Command{Kind::Preset, {-6, 0, 7}}));
  assert(voxone::web_tone::valid(single(Kind::Treble, 4)));
  assert(!voxone::web_tone::valid(single(Kind::Bass, -7)));
}

void testFifoResolution() {
  std::deque<Command> queue;
  queue.push_back(single(Kind::Bass, 5));
  queue.push_back(single(Kind::Middle, -2));
  queue.push_back(single(Kind::Treble, 4));
  queue.push_back(Command{Kind::Preset, {3, 2, 3}});
  queue.push_back(single(Kind::Treble, 1));

  Values current{0, 0, 0};
  Values resolved{};
  assert(voxone::web_tone::resolve(queue.front(), current, resolved));
  queue.pop_front();
  current = resolved;
  assert(current.bass == 5 && current.middle == 0 && current.treble == 0);

  assert(voxone::web_tone::resolve(queue.front(), current, resolved));
  queue.pop_front();
  current = resolved;
  assert(current.bass == 5 && current.middle == -2 && current.treble == 0);

  assert(voxone::web_tone::resolve(queue.front(), current, resolved));
  queue.pop_front();
  current = resolved;
  assert(current.bass == 5 && current.middle == -2 && current.treble == 4);

  assert(voxone::web_tone::resolve(queue.front(), current, resolved));
  queue.pop_front();
  current = resolved;
  assert(current.bass == 3 && current.middle == 2 && current.treble == 3);

  assert(voxone::web_tone::resolve(queue.front(), current, resolved));
  queue.pop_front();
  current = resolved;
  assert(current.bass == 3 && current.middle == 2 && current.treble == 1);
  assert(queue.empty());
}

void testFirmwareIntegrationContract() {
  const std::string server = readFile("src/core/netserver.cpp");
  const std::string config = readFile("src/core/config.cpp");
  const std::string main = readFile("src/main.cpp");
  const std::string frontend = readFile("web-src/voxone.js");

  const std::string callback = section(
      server, "void NetServer::onWsMessage", "void NetServer::requestOnChange");
  const std::string service = section(
      server, "void NetServer::serviceScrollSettings()",
      "void NetServer::onWsMessage");
  const std::string setTone = section(
      config, "bool Config::setTone", "void Config::setSmartStart");

  for (const char* command : {"\"tone\"", "\"bass\"", "\"middle\"",
                              "\"treble\""})
    assert(callback.find(command) != std::string::npos);
  assert(callback.find("enqueueToneCommand") != std::string::npos);
  assert(callback.find("config.setTone") == std::string::npos);
  assert(callback.find("persistV7") == std::string::npos);

  // REST tickets share this queue but authorize inside executeRestConfig().
  // This assertion concerns the later WebSocket Tone branch, not the first
  // (REST) dequeue in the whole service function.
  const std::size_t authorization = service.find("queuedToneCommandAuthorization");
  assert(authorization != std::string::npos);
  const std::size_t receive = service.find(
      "xQueueReceive(toneCommandQueue", authorization);
  const std::size_t persist = service.find("config.setTone");
  assert(receive != std::string::npos && persist != std::string::npos);
  assert(receive < persist);
  assert(authorization < receive);
  assert(service.find("configurationMutationMutex") != std::string::npos);

  const std::size_t durableWrite = setTone.find("persistV7()");
  const std::size_t confirmation = setTone.find(
      "requestOnChange(EQUALIZER");
  assert(durableWrite != std::string::npos && confirmation != std::string::npos);
  assert(durableWrite < confirmation);
  assert(main.find("netserver.serviceScrollSettings();") !=
         std::string::npos);

  assert(frontend.find("send(\"tone\"") != std::string::npos);
  for (const char* command : {"command: \"bass\"", "command: \"middle\"",
                              "command: \"treble\""})
    assert(frontend.find(command) != std::string::npos);
}

}  // namespace

int main() {
  testValidation();
  testFifoResolution();
  testFirmwareIntegrationContract();
  return 0;
}
