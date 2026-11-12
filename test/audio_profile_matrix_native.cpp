#include "../src/hardware/runtime_audio_output.h"
#include "../src/core/audio_runtime_config.h"

#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

using namespace voxone::hardware;

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

void contains(const std::string& text, const char* value) {
  assert(text.find(value) != std::string::npos);
}
}  // namespace

int main() {
  const auto none = runtimeAudioActivationPlan(RuntimeAudioOutput::None);
  const auto pcm = runtimeAudioActivationPlan(RuntimeAudioOutput::Pcm5102a);
  const auto max = runtimeAudioActivationPlan(RuntimeAudioOutput::Max98357);
  const auto maxStereo =
      runtimeAudioActivationPlan(RuntimeAudioOutput::Max98357Stereo);
  const auto dsp = runtimeAudioActivationPlan(RuntimeAudioOutput::DspMini);
  assert(!none.initializeSerialI2s && !none.localPlaybackAvailable);
  assert(pcm.initializeSerialI2s && pcm.localPlaybackAvailable);
  assert(max.initializeSerialI2s && max.localPlaybackAvailable);
  assert(maxStereo.initializeSerialI2s && maxStereo.localPlaybackAvailable);
  assert(maxStereo.initializeSerialI2s == max.initializeSerialI2s);
  assert(maxStereo.localPlaybackAvailable == max.localPlaybackAvailable);
  assert(!dsp.initializeSerialI2s && !dsp.localPlaybackAvailable);

  const auto pcmPath = voxone::audio_runtime::outputActivationPlan(
      AudioOutputKind::Pcm5102a, AUDIO_PCM5102, true);
  const auto dspPath = voxone::audio_runtime::outputActivationPlan(
      AudioOutputKind::DspMini, AUDIO_DSPMINI, true);
  const auto unsupportedDspPath = voxone::audio_runtime::outputActivationPlan(
      AudioOutputKind::DspMini, AUDIO_DSPMINI, false);
  const auto mismatchedDspPath = voxone::audio_runtime::outputActivationPlan(
      AudioOutputKind::Pcm5102a, AUDIO_DSPMINI, true);
  assert(pcmPath.initializeSerialI2s && pcmPath.localPlaybackAvailable &&
         !pcmPath.dspMiniSelected);
  assert(dspPath.initializeSerialI2s && dspPath.localPlaybackAvailable &&
         dspPath.dspMiniSelected);
  assert(!unsupportedDspPath.initializeSerialI2s &&
         !unsupportedDspPath.localPlaybackAvailable);
  assert(!mismatchedDspPath.initializeSerialI2s &&
         !mismatchedDspPath.localPlaybackAvailable);

  const std::string player = readFile("src/core/player.cpp");
  contains(player, "I2S_NUM_0, false");
  contains(player, "outputActivationPlan");
  contains(player, "audioPlan.initializeSerialI2s");
  contains(player, "!_localAudioAvailable &&");
  contains(player, "request.type == PR_PLAY");
  contains(player, "request.type == PR_BURL");

  const std::string source = readFile("src/core/source_manager.cpp");
  contains(source, "allowed && player.localAudioAvailable()");
  contains(source, "!player.localAudioAvailable()");

  const std::string web = readFile("web-src/voxone.js");
  contains(web, "snapshot.supportedAudioOutputs");
  contains(web, "body.set(\"audio\", hardwareAudio.value)");
  const std::string html = readFile("web-src/voxone.html");
  contains(html, "LEFT/RIGHT");
  contains(html, "SD/MODE");

  const std::string server = readFile("src/core/netserver.cpp");
  contains(server, "!request->hasParam(\"audio\", true)");
  contains(server, "input.c_str(), audio.c_str(), requested");
}
