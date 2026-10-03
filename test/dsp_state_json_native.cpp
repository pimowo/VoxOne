#include "../src/core/dsp_state_json.h"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace voxone::dsp;

int main() {
  DemoBackend backend;
  DspService service;
  assert(service.init(backend, {}) == DspServiceError::Ok);
  assert(service.renameUserPreset(PresetId::User1, "A\"B\\C") ==
         DspServiceError::Ok);
  DspSharedAudioView audio{};
  audio.volume = 32;
  audio.maximumVolume = 80;
  audio.tone = {2, -1, 3};
  char json[4096];
  const size_t length = formatDspStateJson(json, sizeof(json), service, audio);
  assert(length > 1000 && length < sizeof(json));
  assert(json[length] == '\0');
  assert(std::strstr(json, "\"sharedAudio\":{\"volume\":32"));
  assert(std::strstr(json, "\"bass\":2,\"middle\":-1,\"treble\":3"));
  assert(std::strstr(json, "\"dirty\":true"));
  assert(std::strstr(json, "\"name\":\"A\\\"B\\\\C\""));
  assert(std::strstr(json, "\"computed\""));
  char tiny[32];
  assert(formatDspStateJson(tiny, sizeof(tiny), service, audio) == 0);
  std::puts(json);
}
