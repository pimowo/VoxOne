#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}
}

int main() {
  const std::string server = readFile("src/core/netserver.cpp");
  const std::string command = readFile("src/core/commandhandler.cpp");
  const std::string html = readFile("web-src/voxone.html");
  const std::string javascript = readFile("web-src/voxone.js");
  const std::string manager = readFile("src/core/power_manager.cpp");

  assert(server.find("/api/power/wakeup") != std::string::npos);
  assert(server.find("powerManager.requestWakeup()") != std::string::npos);
  assert(command.find("strEquals(command, \"wakeup\")") !=
         std::string::npos);
  assert(html.find("Tryb zasilania") != std::string::npos);
  for (const char* mode : {"soft_standby", "hard_power", "mute_only"}) {
    assert(html.find(mode) != std::string::npos);
    assert(javascript.find(mode) != std::string::npos);
  }
  assert(javascript.find("fetch(\"/api/power\"") != std::string::npos);
  assert(manager.find("currentHardware().power") != std::string::npos);
  assert(manager.find("hasPin(pins.relay)") != std::string::npos);
  assert(manager.find("POWER_RELAY_PIN") == std::string::npos);
  assert(manager.find("config.persistV7()") != std::string::npos);
  assert(manager.find("display.beginShutdownSequence()") !=
         std::string::npos);
  const size_t shutdownAudio = manager.find("player.stopForShutdown()");
  const size_t shutdownScreen = manager.find("display.beginShutdownSequence()");
  assert(shutdownAudio != std::string::npos);
  assert(shutdownScreen != std::string::npos);
  assert(shutdownAudio < shutdownScreen);
  assert(manager.find("display.shutdownSequenceComplete()") !=
         std::string::npos);
  assert(manager.find("player.setMuted(true)") != std::string::npos);
  assert(manager.find("config.setDspOn(false, false)") == std::string::npos);
  assert(manager.find("if (!standby() && !shutdownNotice_) return;") !=
         std::string::npos);
  assert(manager.find("ButtonEvent::BUTTON_CLICK") != std::string::npos);
  assert(manager.find("else if (standby())") != std::string::npos);
  assert(manager.find("player.toggleMute();") != std::string::npos);
  assert(manager.find("ButtonEvent::BUTTON_DOUBLE_CLICK") !=
         std::string::npos);
  assert(manager.find("uiMenu.open()") != std::string::npos);
  assert(manager.find("transitionUiMode(MENU)") != std::string::npos);
  assert(manager.find("ButtonEvent::BUTTON_LONG_PRESS") != std::string::npos);
  assert(manager.find("beginShutdown();") != std::string::npos);

  const std::string player = readFile("src/core/player.cpp");
  assert(player.find("void Player::stopForShutdown()") != std::string::npos);
  assert(player.find("Audio::setVolume(0)") != std::string::npos);
  assert(player.find("stopAudioTask();") != std::string::npos);
  assert(player.find("void Player::resumeAfterShutdown()") !=
         std::string::npos);
  assert(player.find("startAudioTask()") != std::string::npos);
  return 0;
}
