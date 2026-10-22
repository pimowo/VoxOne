#ifndef VOXONE_FACTORY_RESET_H
#define VOXONE_FACTORY_RESET_H

#include <stddef.h>
#include <stdint.h>

namespace voxone { namespace factory_reset {

enum class Marker : uint8_t { Missing = 0, Erasing = 1, FirstBootPending = 2, Invalid = 3 };
enum class Result : uint8_t {
  Applied, MarkerWriteFailed, NvsEraseFailed, FileEraseFailed,
  VerificationFailed, FinalizeFailed,
};

class Backend {
 public:
  virtual ~Backend() {}
  virtual bool writeMarker(Marker marker) = 0;
  virtual Marker readMarker() = 0;
  virtual bool clearMarker() = 0;
  virtual size_t nvsNamespaceCount() const = 0;
  virtual bool eraseNvsNamespace(size_t index) = 0;
  virtual size_t userFileCount() const = 0;
  virtual bool eraseUserFile(size_t index) = 0;
  virtual bool userDataCleared() = 0;
};

Result perform(Backend& backend);
bool consumeFirstBootBypass(Backend& backend);
const char* resultName(Result result);

Result performDeviceFactoryReset();
bool consumeDeviceFactoryFirstBootBypass();

} }
#endif
