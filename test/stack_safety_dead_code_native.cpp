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

bool fileExists(const char* path) {
  std::ifstream input(path, std::ios::binary);
  return input.good();
}

size_t occurrences(const std::string& value, const std::string& needle) {
  size_t count = 0;
  for (size_t offset = 0;
       (offset = value.find(needle, offset)) != std::string::npos;
       offset += needle.size()) ++count;
  return count;
}

}  // namespace

int main() {
  const auto recovery = readFile("src/core/security_recovery.cpp");
  const auto recoveryHeader = readFile("src/core/security_recovery.h");
  assert(recovery.find("char body[1536]") != std::string::npos);
  assert(recovery.find("char header[512]") != std::string::npos);
  assert(recovery.find("securityRecoveryApRunning") == std::string::npos);
  assert(recoveryHeader.find("securityRecoveryApRunning") == std::string::npos);

  const auto restore = readFile("src/core/config_restore.cpp");
  assert(restore.find("uint8_t buffer[256]") != std::string::npos);
  assert(restore.find("std::min(sizeof(buffer), reader->size() - offset)") !=
         std::string::npos);

  const auto btInput = readFile("src/core/bt_audio_input.cpp");
  assert(btInput.find("int16_t pcm[256]") != std::string::npos);
  assert(btInput.find("i2s_read(kBtI2sPort, pcm, sizeof(pcm)") !=
         std::string::npos);

  const auto audio = readFile("src/audioI2S/Audio.cpp");
  const auto audioHeader = readFile("src/audioI2S/AudioEx.h");
  assert(audio.find("char rhl[256]") != std::string::npos);
  assert(audio.find("char redirectUrl[256]") != std::string::npos);
  assert(audio.find("Audio::I2Sstop") == std::string::npos);
  assert(audio.find("Audio::i2s_mclk_pin_select") == std::string::npos);
  assert(audioHeader.find("m_tsPacketSize") == std::string::npos);
  assert(audioHeader.find("m_tsHeaderSize") == std::string::npos);
  assert(audioHeader.find("m_plsBuffEntryLen") == std::string::npos);
  assert(occurrences(audioHeader, "#include <vector>") == 1);

  const auto html = readFile("web-src/voxone.html");
  const auto css = readFile("web-src/voxone.css");
  const auto js = readFile("web-src/voxone.js");
  const auto commands = readFile("src/core/commandhandler.cpp");
  const auto server = readFile("src/core/netserver.cpp");
  for (const auto* symbol : {"tts-section", "tts-container", "legacy-card"}) {
    assert(html.find(symbol) == std::string::npos);
    assert(css.find(symbol) == std::string::npos);
  }
  for (const auto* symbol : {"sendTtsRequest", "handleTtsWsMessage",
                             "legacyTabSwitch"})
    assert(js.find(symbol) == std::string::npos);
  assert(commands.find("tts_speak") == std::string::npos);
  assert(commands.find("legacy_settings") == std::string::npos);
  assert(server.find("/api/tts/status") == std::string::npos);

  assert(fileExists("src/core/core_utils.h"));
  assert(!fileExists("src/core/common.h"));
  assert(!fileExists("src/core/vu_frame_limiter.h"));
  assert(!fileExists("src/core/eq_preset_label.h"));
  assert(fileExists("src/core/dac_mute.h"));
  assert(!fileExists("src/core/dac_mute_state.h"));
  return 0;
}
