#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string functionBody(const std::string& source, const char* signature,
                         const char* nextSignature) {
  const size_t begin = source.find(signature);
  assert(begin != std::string::npos);
  const size_t end = source.find(nextSignature, begin + 1);
  assert(end != std::string::npos);
  return source.substr(begin, end - begin);
}
}  // namespace

int main() {
  const std::string command = readFile("src/core/commandhandler.cpp");
  const std::string sourceCommand = functionBody(
      command, "bool CommandHandler::exec(", "if (strEquals(command, \"webtransport\"))");
  assert(sourceCommand.find("sourceManagerRequestSource(target)") !=
         std::string::npos);
  assert(sourceCommand.find("sourceManagerSelectSource(target)") ==
         std::string::npos);

  const std::string manager = readFile("src/core/source_manager.cpp");
  const std::string request = functionBody(
      manager, "bool sourceManagerRequestSource(",
      "void sourceManagerServiceRequests()");
  assert(request.find("pendingSourceRequest.store") != std::string::npos);
  assert(request.find("btLink.state()") == std::string::npos);
  const std::string service = functionBody(
      manager, "void sourceManagerServiceRequests()",
      "SourceId sourceManagerActiveSource()");
  assert(service.find("pendingSourceRequest.exchange") != std::string::npos);
  assert(service.find("sourceManagerSelectSource(source)") !=
         std::string::npos);

  const std::string select = functionBody(
      manager, "bool sourceManagerSelectSource(ActiveSource source)",
      "bool sourceManagerSelectSource(SourceId source)");
  assert(select.find("sourceManagerApplyConfiguredBluetoothVolume()") !=
         std::string::npos);

  const std::string main = readFile("src/main.cpp");
  assert(main.find("sourceManagerServiceRequests();") != std::string::npos);
}
