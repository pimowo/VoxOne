#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string functionBody(const std::string& source, const char* signature,
                         const char* nextSignature) {
  const std::size_t begin = source.rfind(signature);
  assert(begin != std::string::npos);
  const std::size_t end = source.find(nextSignature, begin + 1);
  assert(end != std::string::npos);
  return source.substr(begin, end - begin);
}
}  // namespace

int main() {
  const std::string server = readFile("src/core/netserver.cpp");
  const std::string adapter = readFile("src/core/config_restore_adapter.cpp");
  const std::string html = readFile("web-src/voxone.html");
  const std::string script = readFile("web-src/voxone.js");

  for (const char* token : {"kBackupRoute", "kRestoreRoute",
                            "handleConfigBackup", "handleConfigRestoreBody",
                            "handleConfigRestore"})
    assert(server.find(token) != std::string::npos);
  assert(server.find("Content-Disposition") != std::string::npos);
  assert(server.find("VoxOne-\" VOXONE_VERSION \"-backup.vxcf") !=
         std::string::npos);
  assert(server.find("config_backup_web::kCacheControl") != std::string::npos);
  assert(server.find("deviceApiAuthorized(request)") != std::string::npos);
  assert(server.find("ProductionBackupStream") != std::string::npos);
  assert(server.find("ProductionRestoreUpload") != std::string::npos);
  assert(server.find("heap_caps_malloc(total, MALLOC_CAP_8BIT)") ==
         std::string::npos);
  assert(server.find("kMaxUploadBytes") != std::string::npos);

  const std::string download = functionBody(
      server, "void handleConfigBackup(AsyncWebServerRequest* request)",
      "void handleConfigRestoreBody");
  const std::string upload = functionBody(
      server, "void handleConfigRestoreBody", "void handleConfigRestore(");
  const std::string restore = functionBody(
      server, "void handleConfigRestore(AsyncWebServerRequest* request)",
      "void handleHardwareConfigRead");
  assert(download.find("deviceApiAuthorized(request)") != std::string::npos);
  assert(upload.find("authenticate(HTTP_USER, HTTP_PASS)") != std::string::npos);
  assert(upload.find("upload->append(index, data, len)") != std::string::npos);
  assert(upload.find("memcpy(session->bytes") == std::string::npos);
  assert(restore.find("deviceApiAuthorized(request)") != std::string::npos);
  assert(download.find("Serial") == std::string::npos);
  assert(upload.find("Serial") == std::string::npos);
  assert(restore.find("Serial") == std::string::npos);

  assert(adapter.find("ProductionBackupStream::read") != std::string::npos);
  assert(adapter.find("ProductionRestoreUpload::append") != std::string::npos);
  assert(adapter.find("kBackupVariableOffset") != std::string::npos);
  assert(adapter.find("/data/.vxrestore.wifi.new") != std::string::npos);
  assert(adapter.find("/data/.vxrestore.stations.new") != std::string::npos);
  assert(adapter.find("SPIFFS.totalBytes() - SPIFFS.usedBytes()") !=
         std::string::npos);
  assert(adapter.find("kRestoreStagingReserveBytes") !=
         std::string::npos);
  assert(adapter.find("sizeof(Impl) <= 2048") != std::string::npos);
  assert(adapter.find("config_backup::restoreValidated(impl_->candidate") !=
         std::string::npos);
  assert(adapter.find("heap_caps_malloc") == std::string::npos);
  assert(server.find("onDisconnect([request]() { finishConfigRestoreUpload(request); })") !=
         std::string::npos);
  assert(html.find("id=\"config-backup-button\"") != std::string::npos);
  assert(html.find("id=\"config-restore-file\"") != std::string::npos);
  assert(html.find("hasła Wi-Fi/MQTT") != std::string::npos);
  assert(script.find("/api/config/backup") != std::string::npos);
  assert(script.find("/api/config/restore") != std::string::npos);
  assert(script.find("recovery_pending") != std::string::npos);
  assert(script.find("rebootDialog.showModal()") != std::string::npos);
}
