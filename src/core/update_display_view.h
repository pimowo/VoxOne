#ifndef VOXONE_UPDATE_DISPLAY_VIEW_H
#define VOXONE_UPDATE_DISPLAY_VIEW_H

#include "update_progress.h"

inline const char* updateTargetDisplayName(UpdateTarget target) {
  switch (target) {
    case UpdateTarget::VoxOneFirmware: return "VoxOne Firmware";
    case UpdateTarget::Filesystem: return "VoxOne system plików";
    case UpdateTarget::VoxOneBtFirmware: return "VoxOneBT Firmware";
    case UpdateTarget::None: return "";
  }
  return "";
}

inline const char* updateActivityDisplayText(UpdateActivity activity) {
  switch (activity) {
    case UpdateActivity::PreparingUpdate: return "Przygotowanie aktualizacji...";
    case UpdateActivity::StoppingAudio: return "Zatrzymywanie audio...";
    case UpdateActivity::BackingUpSettings: return "Tworzenie kopii ustawień...";
    case UpdateActivity::WritingFirmware: return "Zapisywanie firmware...";
    case UpdateActivity::WritingFilesystem: return "Zapisywanie systemu plików...";
    case UpdateActivity::SendingToBt: return "Wysyłanie do VoxOneBT...";
    case UpdateActivity::Verifying: return "Weryfikacja...";
    case UpdateActivity::RestartingBt: return "Restart VoxOneBT...";
    case UpdateActivity::WaitingForBt: return "Oczekiwanie na VoxOneBT...";
    case UpdateActivity::HealthCheck: return "Test nowego firmware...";
    case UpdateActivity::Completed: return "Aktualizacja zakończona";
    case UpdateActivity::PreparingRestart: return "Przygotowanie restartu...";
    case UpdateActivity::Failed: return "Błąd aktualizacji";
    case UpdateActivity::None: return "";
  }
  return "";
}

inline bool updateScreenOwnsDisplay(const UpdateProgressSnapshot& snapshot) {
  return snapshot.locked;
}

inline bool updateScreenAllowsMode(const UpdateProgressSnapshot& snapshot,
                                   bool updateMode) {
  return !updateScreenOwnsDisplay(snapshot) || updateMode;
}

inline bool updateScreenReturnsToPlayer(const UpdateProgressSnapshot& snapshot) {
  return !snapshot.locked &&
      (snapshot.phase == UpdatePhase::Error || snapshot.phase == UpdatePhase::Aborted);
}

struct UpdateDisplayProgress {
  UpdateDisplayProgress(bool known = false, uint8_t value = 0)
      : determinate(known), percent(value) {}
  bool determinate;
  uint8_t percent;
};

class UpdateDisplayProgressState {
 public:
  UpdateDisplayProgress observe(const UpdateProgressSnapshot& snapshot) {
    if (snapshot.acquisition != acquisition_) {
      acquisition_ = snapshot.acquisition;
      hadKnownProgress_ = false;
    }
    if (snapshot.progressKnown) hadKnownProgress_ = true;
    if (snapshot.progressKnown) return {true, snapshot.percent};
    const bool completed = snapshot.phase == UpdatePhase::Success ||
        snapshot.phase == UpdatePhase::Restarting;
    if (completed && (hadKnownProgress_ ||
        (snapshot.totalBytes != 0 && snapshot.doneBytes == snapshot.totalBytes)))
      return {true, 100};
    return {};
  }

 private:
  uint32_t acquisition_ = 0;
  bool hadKnownProgress_ = false;
};

#endif
