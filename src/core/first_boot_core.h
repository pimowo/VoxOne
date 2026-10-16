#ifndef VOXONE_FIRST_BOOT_CORE_H
#define VOXONE_FIRST_BOOT_CORE_H

#include <stdint.h>

namespace voxone {
namespace first_boot {

enum class Mode : uint8_t {
  Normal,
  FirstBootSetup,
  WifiConnecting,
  WifiConfigured,
  WifiRecovery,
  SecurityRecovery
};

enum class Connection : uint8_t {
  Idle,
  Connecting,
  Connected,
  SetupAp,
  RecoveryAp,
  Failed
};

struct Inputs {
  uint8_t wifiProfileCount;
  Connection connection;
  bool credentialsJustSaved;
  bool securityRecovery;
  const char* apSsid;
  const char* address;
  int errorCode;
};

struct View {
  Mode mode;
  Connection connection;
  const char* product;
  const char* title;
  const char* apSsid;
  const char* address;
  const char* statusText;
  int errorCode;
  bool setupApOpen;
  bool wifiScanAvailable;
};

inline bool hasWifiConfiguration(uint8_t wifiProfileCount) {
  return wifiProfileCount != 0;
}

inline bool shouldStartOpenSetupAp(uint8_t wifiProfileCount) {
  return !hasWifiConfiguration(wifiProfileCount);
}

inline Mode resolveMode(const Inputs& inputs) {
  if (inputs.securityRecovery) return Mode::SecurityRecovery;
  if (inputs.connection == Connection::Connecting)
    return Mode::WifiConnecting;
  if (!hasWifiConfiguration(inputs.wifiProfileCount))
    return Mode::FirstBootSetup;
  if (inputs.credentialsJustSaved && inputs.connection == Connection::Connected)
    return Mode::WifiConfigured;
  if (inputs.connection == Connection::RecoveryAp ||
      inputs.connection == Connection::Failed)
    return Mode::WifiRecovery;
  return Mode::Normal;
}

inline const char* statusText(Mode mode) {
  switch (mode) {
    case Mode::Normal: return "GOTOWY";
    case Mode::FirstBootSetup: return "SIEC OTWARTA";
    case Mode::WifiConnecting: return "LACZENIE WI-FI";
    case Mode::WifiConfigured: return "WI-FI SKONFIGUROWANE";
    case Mode::WifiRecovery: return "WI-FI RECOVERY";
    case Mode::SecurityRecovery: return "SECURITY RECOVERY";
  }
  return "";
}

inline View makeView(const Inputs& inputs) {
  const Mode mode = resolveMode(inputs);
  const bool setup = mode == Mode::FirstBootSetup;
  return View{mode,
              inputs.connection,
              "VoxOne",
              setup ? "KONFIGURACJA" : statusText(mode),
              inputs.apSsid ? inputs.apSsid : "",
              inputs.address ? inputs.address : "",
              statusText(mode),
              inputs.errorCode,
              setup,
              setup && inputs.connection == Connection::SetupAp};
}

}  // namespace first_boot
}  // namespace voxone

#endif
