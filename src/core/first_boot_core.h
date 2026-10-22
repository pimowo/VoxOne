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

// Semantic presentation targets. Renderers own geometry, fonts and colours;
// this model only tells them which kind of screen the runtime state requires.
enum class Screen : uint8_t {
  Player,
  WifiSetup,
  WifiProgress,
  WifiRecovery,
  SecurityRecovery
};

enum class ApSecurity : uint8_t {
  NotActive,
  Open,
  Protected
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
  // This is deliberately stronger than "credentials were persisted" or an
  // HTTP 200 response. The owner may set it only after the new configuration
  // has produced a validated connected state.
  bool wifiConfigurationConfirmed;
  bool securityRecovery;
  const char* apSsid;
  const char* address;
  int errorCode;
};

struct View {
  Mode mode;
  Screen screen;
  Connection connection;
  const char* product;
  const char* title;
  const char* apSsid;
  const char* address;
  const char* statusText;
  int errorCode;
  ApSecurity apSecurity;
  bool apActive;
  bool presentApCredential;
  bool hasError;
  // Kept for existing renderer compatibility; equivalent to an active,
  // open setup AP and not a second source of state.
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
  if (!hasWifiConfiguration(inputs.wifiProfileCount))
    return Mode::FirstBootSetup;
  if (inputs.connection == Connection::Connecting)
    return Mode::WifiConnecting;
  if (inputs.wifiConfigurationConfirmed &&
      inputs.connection == Connection::Connected)
    return Mode::WifiConfigured;
  if (inputs.connection == Connection::RecoveryAp ||
      inputs.connection == Connection::Failed)
    return Mode::WifiRecovery;
  return Mode::Normal;
}

inline Screen screenForMode(Mode mode) {
  switch (mode) {
    case Mode::Normal: return Screen::Player;
    case Mode::FirstBootSetup: return Screen::WifiSetup;
    case Mode::WifiConnecting:
    case Mode::WifiConfigured: return Screen::WifiProgress;
    case Mode::WifiRecovery: return Screen::WifiRecovery;
    case Mode::SecurityRecovery: return Screen::SecurityRecovery;
  }
  return Screen::Player;
}

inline const char* titleText(Mode mode) {
  switch (mode) {
    case Mode::Normal: return "VoxOne";
    case Mode::FirstBootSetup: return "KONFIGURACJA";
    case Mode::WifiConnecting: return "LACZENIE WI-FI";
    case Mode::WifiConfigured: return "WI-FI SKONFIGUROWANE";
    case Mode::WifiRecovery: return "KONFIGURACJA WI-FI";
    case Mode::SecurityRecovery: return "SECURITY RECOVERY";
  }
  return "";
}

inline const char* statusText(Mode mode) {
  switch (mode) {
    case Mode::Normal: return "GOTOWY";
    case Mode::FirstBootSetup: return "SIEĆ OTWARTA";
    case Mode::WifiConnecting: return "LACZENIE WI-FI";
    case Mode::WifiConfigured: return "WI-FI SKONFIGUROWANE";
    case Mode::WifiRecovery: return "WI-FI RECOVERY";
    case Mode::SecurityRecovery: return "SECURITY RECOVERY";
  }
  return "";
}

inline View makeView(const Inputs& inputs) {
  const Mode mode = resolveMode(inputs);
  const bool setupApActive = mode == Mode::FirstBootSetup &&
      inputs.connection == Connection::SetupAp;
  const bool wifiRecoveryApActive = mode == Mode::WifiRecovery &&
      inputs.connection == Connection::RecoveryAp;
  const bool securityRecoveryApActive = mode == Mode::SecurityRecovery &&
      inputs.connection == Connection::RecoveryAp;
  const bool apActive = setupApActive || wifiRecoveryApActive ||
      securityRecoveryApActive;
  const ApSecurity apSecurity = !apActive ? ApSecurity::NotActive :
      ((setupApActive || securityRecoveryApActive) ? ApSecurity::Open
                                                   : ApSecurity::Protected);
  // Ordinary Wi-Fi recovery keeps its existing protected-AP behaviour.
  // SECURITY Recovery is deliberately open and carries no credential.
  const bool presentApCredential = wifiRecoveryApActive;
  const bool hasError = inputs.errorCode != 0 ||
      inputs.connection == Connection::Failed;
  return View{mode,
              screenForMode(mode),
              inputs.connection,
              "VoxOne",
              titleText(mode),
              apActive && inputs.apSsid ? inputs.apSsid : "",
              apActive && inputs.address ? inputs.address : "",
              statusText(mode),
              inputs.errorCode,
              apSecurity,
              apActive,
              presentApCredential,
              hasError,
              setupApActive,
              setupApActive};
}

}  // namespace first_boot
}  // namespace voxone

#endif
