#include "factory_reset.h"

namespace voxone { namespace factory_reset {

Result perform(Backend& backend) {
  if (!backend.writeMarker(Marker::Erasing) ||
      backend.readMarker() != Marker::Erasing)
    return Result::MarkerWriteFailed;
  for (size_t index = 0; index < backend.nvsNamespaceCount(); ++index)
    if (!backend.eraseNvsNamespace(index)) return Result::NvsEraseFailed;
  for (size_t index = 0; index < backend.userFileCount(); ++index)
    if (!backend.eraseUserFile(index)) return Result::FileEraseFailed;
  if (!backend.userDataCleared()) return Result::VerificationFailed;
  if (!backend.writeMarker(Marker::FirstBootPending) ||
      backend.readMarker() != Marker::FirstBootPending)
    return Result::FinalizeFailed;
  return Result::Applied;
}

bool consumeFirstBootBypass(Backend& backend) {
  if (backend.readMarker() != Marker::FirstBootPending) return false;
  return backend.clearMarker() && backend.readMarker() == Marker::Missing;
}

const char* resultName(Result result) {
  switch (result) {
    case Result::Applied: return "applied";
    case Result::MarkerWriteFailed: return "marker_write_failed";
    case Result::NvsEraseFailed: return "nvs_erase_failed";
    case Result::FileEraseFailed: return "file_erase_failed";
    case Result::VerificationFailed: return "verification_failed";
    case Result::FinalizeFailed: return "finalize_failed";
  }
  return "unknown";
}

} }
