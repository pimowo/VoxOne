#include "../src/audioI2S/audio_ring_math.h"

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

}  // namespace

int main() {
  using voxone::audio_ring::advance;
  using voxone::audio_ring::space;

  constexpr auto initial = space(16, 0, 0, true);
  static_assert(initial.freeBytes == 15 &&
                    initial.contiguousWriteBytes == 15,
                "an empty one-slot ring must expose its safe capacity");
  constexpr auto saturated = space(16, 7, 7, false);
  static_assert(saturated.freeBytes == 0 &&
                    saturated.contiguousWriteBytes == 0,
                "equal non-empty pointers must not underflow");
  constexpr auto wrapped = space(16, 5, 12, false);
  static_assert(wrapped.freeBytes == 8 &&
                    wrapped.contiguousWriteBytes == 4,
                "wrapped free and contiguous space must be bounded");
  static_assert(advance(16, 14, 2) == 0,
                "write pointer must wrap at the ring boundary");

  const std::string audio = readFile("src/audioI2S/Audio.cpp");
  const std::string audioHeader = readFile("src/audioI2S/AudioEx.h");
  const std::string flac =
      readFile("src/audioI2S/flac_decoder/flac_decoder.cpp");
  const std::string player = readFile("src/core/player.cpp");
  const std::string bt = readFile("src/core/bt_audio_input.cpp");

  assert(audioHeader.find("uint32_t        m_flacMaxFrameSize") !=
         std::string::npos);
  assert(flac.find("int32_t  m_bytesAvail") != std::string::npos);
  assert(flac.find("if(m_bytesAvail <= 0)") != std::string::npos);
  assert(flac.find("return UINT32_MAX") != std::string::npos);
  assert(audio.find("!InBuff.havePSRAM()") != std::string::npos);
  assert(audio.find("i2s_set_sample_rates(%lu) failed") !=
         std::string::npos);
  assert(audioHeader.find("std::atomic<uint8_t> m_vol{64}") !=
         std::string::npos);
  assert(audio.find("m_vol.store(vol, std::memory_order_relaxed)") !=
         std::string::npos);
  assert(audio.find("m_vol.load(std::memory_order_relaxed)") !=
         std::string::npos);

  const std::size_t play = player.find("void Player::_play(");
  const std::size_t arm = player.find("armPcmOutput()", play);
  const std::size_t connect = player.find("connecttohost(", play);
  const std::size_t immediateUnmute = player.find("setOutputPins(true)", play);
  const std::size_t service =
      player.find("void Player::servicePcmOutputActivation()");
  assert(play != std::string::npos && arm < connect);
  assert(immediateUnmute == std::string::npos || immediateUnmute > service);
  assert(service != std::string::npos &&
         player.find("takePcmOutputReady()", service) != std::string::npos);

  assert(audio.find("voxone::i2s_output::Guard outputGuard") !=
         std::string::npos);
  assert(bt.find("voxone::i2s_output::Guard outputGuard") !=
         std::string::npos);
}
