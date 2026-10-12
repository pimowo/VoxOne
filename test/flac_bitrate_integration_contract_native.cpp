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

std::size_t count(const std::string& text, const std::string& token) {
  std::size_t total = 0;
  for (std::size_t position = 0;
       (position = text.find(token, position)) != std::string::npos;
       position += token.size())
    ++total;
  return total;
}
}  // namespace

int main() {
  const std::string audio = readFile("src/audioI2S/Audio.cpp");
  const std::string handlers = readFile("src/core/audiohandlers.h");
  const std::string server = readFile("src/core/netserver.cpp");

  assert(audio.find("m_codec == CODEC_FLAC && bytesDecoded > 0") !=
         std::string::npos);
  assert(audio.find("m_flacBitrateMeter.addBytes(millis()") !=
         std::string::npos);
  assert(audio.find("audio_bitrate(chbuf)") != std::string::npos);
  assert(count(audio, "m_flacBitrateMeter.reset()") >= 3);
  assert(audio.find("m_codec != CODEC_FLAC") != std::string::npos);

  assert(handlers.find("config.station.bitrate = 0") != std::string::npos);
  assert(handlers.find("netserver.requestOnChange(BITRATE, 0)") !=
         std::string::npos);
  assert(server.find("case BF_FLAC: return \"FLAC\";") !=
         std::string::npos);
  return 0;
}
