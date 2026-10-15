#include "../src/core/system_operation_state.h"
#include "../src/core/update_progress.h"
#include "../src/core/update_restart_coordinator.h"
#include "../src/core/web_update_state.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readSource(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void failureLeavesRetryable(UpdateTarget failed, UpdateTarget retry,
                            UpdatePhase terminal) {
  UpdateProgressState progress;
  UpdateRestartCoordinator restart;
  assert(progress.begin(failed, UpdatePhase::Writing, 1024));
  progress.progress(512, 1024);
  progress.terminal(terminal, 7, "failed");
  const auto state = progress.snapshot();
  assert(!state.active && !state.locked && state.percent == 0);
  assert(restart.tick(state, 10000) == UpdateRestartCoordinator::Action::None);
  assert(progress.begin(retry, UpdatePhase::Preparing, 1024));
  const auto clean = progress.snapshot();
  assert(clean.target == retry && clean.errorCode == 0 &&
         clean.status[0] == '\0' && clean.doneBytes == 0);
}
}  // namespace

int main() {
  const uint8_t firmware[8] = {0xE9, 4, 0, 0, 0, 0, 0, 0};
  const uint8_t filesystem[8] = {0, 0, 1, 0, 1, 0, 0, 0};
  uint8_t bad[8] = {};
  constexpr size_t appSize = 1966080;
  constexpr size_t fsSize = 196608;

  assert(validateWebUpdateStart(WebUpdateImageKind::Firmware, firmware, 8,
             1024, 1200, appSize) == WebUpdateValidation::Ok);
  assert(validateWebUpdateStart(WebUpdateImageKind::Firmware, firmware, 8,
             0, 1200, appSize) == WebUpdateValidation::Empty);
  assert(validateWebUpdateStart(WebUpdateImageKind::Firmware, firmware, 4,
             1024, 1200, appSize) == WebUpdateValidation::HeaderTooShort);
  assert(validateWebUpdateStart(WebUpdateImageKind::Firmware, bad, 8,
             1024, 1200, appSize) == WebUpdateValidation::BadImage);
  assert(validateWebUpdateStart(WebUpdateImageKind::Firmware, firmware, 8,
             appSize + 1, appSize + 100, appSize) ==
         WebUpdateValidation::Oversized);
  assert(validateWebUpdateStart(WebUpdateImageKind::Firmware, firmware, 8,
             1024, 1000, appSize) ==
         WebUpdateValidation::DeclaredSizeExceedsRequest);
  assert(validateWebUpdateStart(WebUpdateImageKind::Filesystem, filesystem, 8,
             fsSize, fsSize + 300, fsSize) == WebUpdateValidation::Ok);
  assert(validateWebUpdateStart(WebUpdateImageKind::Filesystem, bad, 8,
             fsSize, fsSize + 300, fsSize) == WebUpdateValidation::BadImage);
  assert(validateWebUpdateStart(WebUpdateImageKind::Filesystem, filesystem, 8,
             fsSize - 1, fsSize + 300, fsSize) ==
         WebUpdateValidation::FilesystemSizeMismatch);

  assert(webUpdateChunkFits(0, 0, 256, 1024, appSize));
  assert(webUpdateChunkFits(256, 256, 256, 1024, appSize));
  assert(!webUpdateChunkFits(256, 255, 256, 1024, appSize));
  assert(!webUpdateChunkFits(512, 512, 513, 1024, appSize));
  assert(!webUpdateUploadComplete(512, 1024));
  assert(webUpdateUploadComplete(1024, 1024));

  for (bool started : {false, true}) {
    for (bool filesystemUnavailable : {false, true}) {
      const auto policy = webUpdateFailurePolicy(started, filesystemUnavailable);
      assert(policy.abortSession == started);
      assert(policy.recoverFilesystem == filesystemUnavailable);
      assert(!policy.allowRestart && !policy.resumeAudio);
    }
  }
  struct FilesystemFailure {
    const char* name;
    bool started;
    bool needsRecovery;
  };
  const FilesystemFailure filesystemFailures[] = {
      {"backup", false, false},
      {"begin-after-unmount", false, true},
      {"disconnect-after-begin", true, true},
      {"write", true, true},
      {"finalize", true, true},
  };
  for (const auto& failure : filesystemFailures) {
    assert(failure.name[0] != '\0');
    const auto policy = webUpdateFailurePolicy(
        failure.started, failure.needsRecovery);
    assert(policy.abortSession == failure.started);
    assert(policy.recoverFilesystem == failure.needsRecovery);
    assert(!policy.allowRestart && !policy.resumeAudio);
  }
  assert(webUpdateStorageReadyAfterFailure(false, false, false));
  assert(webUpdateStorageReadyAfterFailure(true, true, true));
  assert(!webUpdateStorageReadyAfterFailure(true, false, false));
  assert(!webUpdateStorageReadyAfterFailure(true, true, false));

  failureLeavesRetryable(UpdateTarget::VoxOneFirmware,
                         UpdateTarget::VoxOneFirmware, UpdatePhase::Error);
  failureLeavesRetryable(UpdateTarget::VoxOneFirmware,
                         UpdateTarget::Filesystem, UpdatePhase::Aborted);
  failureLeavesRetryable(UpdateTarget::Filesystem,
                         UpdateTarget::Filesystem, UpdatePhase::Error);
  failureLeavesRetryable(UpdateTarget::Filesystem,
                         UpdateTarget::VoxOneFirmware, UpdatePhase::Aborted);

  SystemOperationState recovery;
  assert(recovery.updateStarted());
  recovery.radioStopped();
  recovery.filesystemUnmounting();
  assert(recovery.filesystemUnavailable() && recovery.blocksRequests());
  // A failed remount keeps recovery-only routing and silence, but permits a
  // subsequent authenticated MAIN/SPIFFS attempt.
  assert(recovery.audioBlocked());
  assert(recovery.updateStarted());
  assert(recovery.audioBlocked() && !recovery.restartPending());

  const std::string server = readSource("src/core/netserver.cpp");
  const std::string header = readSource("src/core/netserver.h");
  assert(server.find("recoverFilesystemAfterFailedUpdate") != std::string::npos);
  assert(server.find("SPIFFS.begin(false)") != std::string::npos);
  assert(server.find("restoreWebUpdateData()") != std::string::npos);
  assert(server.find("cleanupFailedWebUpdate(request, true)") != std::string::npos);
  assert(server.find("webUpdateUploadComplete") != std::string::npos);
  assert(server.find("Update.abort()") != std::string::npos);
  assert(server.find("finishUpdate(updateTarget, UpdatePhase::Success") !=
         std::string::npos);
  assert(server.find("request->authenticate(HTTP_USER, HTTP_PASS)") !=
         std::string::npos);
  assert(server.find("activeConfigRestoreUpload || activeConfigBackupRequest") !=
         std::string::npos);
  assert(server.find("webserver.on(\"/api/ready\", HTTP_GET") !=
         std::string::npos);
  assert(server.find("voxone::ota::bootHealthReady() ? \"true\" : \"false\"") !=
         std::string::npos);
  assert(server.find("voxone::ota::formatBootHealthJson") !=
         std::string::npos);
  assert(server.find("if (!voxone::ota::bootHealthReady())") !=
         std::string::npos);
  assert(server.find("Firmware boot health validation is still in progress") !=
         std::string::npos);
  assert(server.find("Cache-Control\", \"no-store, max-age=0") !=
         std::string::npos);
  assert(server.find("request->url() == \"/emergency\" || request->url() == \"/update.html\"") !=
         std::string::npos);
  assert(server.find("if (config.currentWwwReady)\n      request->redirect(\"/#update\");") !=
         std::string::npos);
  assert(header.find("emergency_form") != std::string::npos);
  assert(header.find("Ta strona działa bez plików WWW") != std::string::npos);
}
