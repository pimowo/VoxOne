#include <assert.h>
#include <fstream>
#include <iterator>
#include <string>

static std::string readSource(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

static bool contains(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

int main() {
  const std::string runtime = readSource("src/core/ota_boot_runtime.cpp");
  const std::string health = readSource("src/core/ota_boot_health.h");
  const std::string main = readSource("src/main.cpp");
  const std::string config = readSource("src/core/config.cpp");
  const std::string server = readSource("src/core/netserver.cpp");
  const std::string player = readSource("src/core/player.cpp");
  const std::string platform = readSource("platformio.ini");

  // This strong project symbol overrides the weak Arduino implementation;
  // the installed PlatformIO framework is never patched.
  assert(contains(runtime,
      "extern \"C\" bool verifyRollbackLater() { return true; }"));
  assert(!contains(runtime, "bool verifyOta("));
  assert(contains(runtime, "esp_ota_get_state_partition"));
  assert(contains(runtime, "ESP_OTA_IMG_PENDING_VERIFY"));
  assert(contains(runtime, "esp_ota_check_rollback_is_possible"));
  assert(contains(runtime, "esp_ota_mark_app_valid_cancel_rollback"));
  assert(contains(runtime, "esp_ota_mark_app_invalid_rollback_and_reboot"));
  assert(contains(runtime, "markValidRecovery("));
  assert(contains(runtime, "esp_ota_get_state_partition(running, &confirmedState)"));
  assert(contains(runtime, "firmwareUpdatePreflight()"));
  assert(contains(runtime, "rollback-candidate-lost"));
  assert(contains(server, "FirmwareUpdatePreflight preflight"));
  assert(contains(server, "firmware preflight blocked"));

  // The timeout runs in its own task, so a setup/loop stall cannot silently
  // approve an image. A live main-loop heartbeat is part of runtime health.
  assert(contains(runtime, "xTaskCreate(monitorTask"));
  assert(contains(runtime, "lastServiceMs"));
  assert(contains(health, "kHealthTimeoutMs = 180000"));
  assert(contains(health, "kHealthStableMs = 5000"));

  assert(contains(main, "beginBootHealthMonitor();"));
  assert(contains(main, "Failure::ConfigRestore"));
  assert(contains(main, "completeBootHealth"));
  assert(contains(main, "serviceBootHealth("));
  assert(contains(main, "currentBootControlPlaneReady(netserver.started())"));
  assert(contains(runtime, "evidenceBits.fetch_or(ControlPlaneReady)"));
  assert(contains(runtime, "evidenceBits.fetch_and("));
  assert(contains(main, "bootControlPlaneReady"));
  assert(contains(main, "network.status == CONNECTED && network.ready()"));
  assert(contains(main, "network.status == SOFT_AP"));
  assert(contains(main, "player.ready()"));
  assert(!contains(main, "reportBootFatal(voxone::ota::Failure::Network)"));
  assert(contains(player,
      "return playerQueue != nullptr && _temporaryUrls != nullptr;"));
  assert(!contains(health, "httpApiReady"));
  assert(!contains(health, "networkRequired"));

  // A pending MAIN image may observe corruption, but must never format the
  // shared data partition before rollback. Current WWW assets are not a gate,
  // preserving compatibility with old SPIFFS bundles.
  assert(contains(config, "SPIFFS.begin(false)"));
  assert(!contains(config, "SPIFFS.begin(true)"));
  assert(contains(main, "currentWwwReady is deliberately not required"));

  assert(contains(server, "formatBootHealthJson"));
  assert(contains(server, "reportHttpApiProbe"));
  assert(contains(server, "if (!voxone::ota::bootHealthReady())"));
  assert(contains(server,
      "Firmware boot health validation is still in progress"));
  assert(contains(runtime, "VOXONE_OTA_HIL_ROLLBACK_AFTER_PROBE"));
  assert(contains(platform, "[env:m0-ota-rollback-test]"));
  assert(contains(platform, "-DVOXONE_OTA_HIL_ROLLBACK_AFTER_PROBE=1"));
  assert(contains(server, "bootHealthReady() ? \"true\" : \"false\""));
  assert(contains(runtime, "\\\"runningSlot\\\""));
  assert(contains(runtime, "\\\"bootSlot\\\""));
  assert(contains(runtime, "\\\"imageState\\\""));
  assert(contains(runtime, "\\\"lastFailure\\\""));
  assert(contains(runtime, "\\\"firmwareUpdateReady\\\""));
  assert(contains(runtime, "\\\"firmwareUpdateReason\\\""));
}
