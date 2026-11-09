#include "../src/core/audio_runtime_config.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string readFile(const char* path) {
  std::ifstream stream(path);
  assert(stream.good());
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
  using namespace voxone::audio_runtime;

  static_assert(kPsramInputBufferBytes >= 512U * 1024U,
                "FLAC/HTTP input buffer must provide VBR headroom");
  static_assert(kDramInputBufferBytes == 48U * 1024U,
                "Non-PSRAM MP3/AAC input buffer must remain bounded");
  static_assert(kFlacFrameReserveBytes == 65535U,
                "FLAC reserve must hold the maximum frame");
  static_assert(inputBufferPlan(true).usePsram &&
                    inputBufferPlan(true).inputBytes == 512U * 1024U,
                "PSRAM capability must select the large ring");
  static_assert(!inputBufferPlan(false).usePsram &&
                    inputBufferPlan(false).inputBytes == 48U * 1024U,
                "Non-PSRAM capability must select the DRAM ring");
  static_assert(kTaskCore == 1, "Audio must run on Core 1");
  static_assert(kTaskPriority == 4, "Audio task priority must be 4");
  static_assert(kAudioSocketReceiveBufferBytes == 32768,
                "Audio socket receive buffer must be 32 KiB");

  const std::string audio = readFile("src/audioI2S/Audio.cpp");
  const std::string main = readFile("src/main.cpp");
  const std::string flac =
      readFile("src/audioI2S/flac_decoder/flac_decoder.cpp");
  const std::string platformio = readFile("platformio.ini");

  assert(audio.find("xTaskCreatePinnedToCore(") != std::string::npos);
  assert(audio.find("\"AudioTask\"") != std::string::npos);
  assert(audio.find("requestAudioDecode()") != std::string::npos);
  assert(audio.find("InBuff.lock()") != std::string::npos);
  assert(audio.find("SO_RCVBUF") != std::string::npos);
  assert(main.find("player.loop();") != std::string::npos);
  assert(main.find("audioTaskRunning") == std::string::npos);

  assert(flac.find("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT") !=
         std::string::npos);
  assert(flac.find("if (!memory) memory = calloc(1, sizeof(T));") !=
         std::string::npos);

  const std::string networkCore = "-DCONFIG_ASYNC_TCP_RUNNING_CORE=0";
  const std::size_t first = platformio.find(networkCore);
  assert(first != std::string::npos);
  assert(platformio.find(networkCore, first + networkCore.size()) !=
         std::string::npos);

  const std::string loopCore = "-DARDUINO_RUNNING_CORE=0";
  const std::size_t firstLoopCore = platformio.find(loopCore);
  assert(firstLoopCore != std::string::npos);
  assert(platformio.find(loopCore, firstLoopCore + loopCore.size()) !=
         std::string::npos);

  const std::string removeDefaultLoopCore =
      "build_unflags = -DARDUINO_RUNNING_CORE=1";
  const std::size_t firstUnflag = platformio.find(removeDefaultLoopCore);
  assert(firstUnflag != std::string::npos);
  assert(platformio.find(removeDefaultLoopCore,
                         firstUnflag + removeDefaultLoopCore.size()) !=
         std::string::npos);
}
