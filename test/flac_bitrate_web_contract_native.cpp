#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

int main() {
  std::ifstream input("web-src/voxone.js", std::ios::binary);
  assert(input.good());
  const std::string script{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
  const std::size_t begin = script.find("function renderStream()");
  const std::size_t end = script.find("function renderSource()", begin);
  assert(begin != std::string::npos && end != std::string::npos);
  const std::string body = script.substr(begin, end - begin);
  assert(body.find("parts.push(String(bitrate))") != std::string::npos);
  assert(body.find("parts.join(\" \")") != std::string::npos);
  assert(body.find("kb/s") == std::string::npos);
  assert(body.find("kbps") == std::string::npos);
  return 0;
}
