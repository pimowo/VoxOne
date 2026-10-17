#ifndef VOXONE_AP_UI_WWW_BINDING_H
#define VOXONE_AP_UI_WWW_BINDING_H

#include "first_boot_runtime_snapshot.h"

#include <stdint.h>

namespace voxone {
namespace first_boot {

enum class WebPage : uint8_t {
  Unavailable,
  FirstBootSetup,
  WifiRecovery,
  NormalPanel,
  WifiTransition,
  SecurityRecoveryExclusive
};

struct WebBinding {
  WebPage page;
  bool wifiProvisioning;
  bool normalPanel;
};

inline bool hasNoApData(const RuntimeSnapshot& snapshot) {
  return !snapshot.apActive &&
      snapshot.apSecurity == ApSecurity::NotActive &&
      !snapshot.presentApCredential && snapshot.apSsid[0] == '\0' &&
      snapshot.apAddress[0] == '\0';
}

inline bool hasCompleteApData(const RuntimeSnapshot& snapshot) {
  return snapshot.apActive && snapshot.apSsid[0] != '\0' &&
      snapshot.apAddress[0] != '\0';
}

inline WebBinding webBindingFor(const RuntimeSnapshot& snapshot) {
  if (!snapshot.initialized || snapshot.generation == 0)
    return {WebPage::Unavailable, false, false};

  switch (snapshot.mode) {
    case Mode::FirstBootSetup:
      if (snapshot.screen == Screen::WifiSetup &&
          snapshot.connection == Connection::SetupAp &&
          snapshot.apSecurity == ApSecurity::Open &&
          !snapshot.presentApCredential && hasCompleteApData(snapshot))
        return {WebPage::FirstBootSetup, true, false};
      return {WebPage::Unavailable, false, false};

    case Mode::WifiRecovery:
      if (snapshot.screen == Screen::WifiRecovery &&
          snapshot.connection == Connection::RecoveryAp &&
          snapshot.apSecurity == ApSecurity::Protected &&
          snapshot.presentApCredential && hasCompleteApData(snapshot))
        return {WebPage::WifiRecovery, true, false};
      return {WebPage::Unavailable, false, false};

    case Mode::Normal:
      if (snapshot.screen == Screen::Player &&
          snapshot.connection == Connection::Connected &&
          snapshot.staValidatedInThisBoot &&
          !snapshot.hasError && hasNoApData(snapshot))
        return {WebPage::NormalPanel, false, true};
      return {WebPage::Unavailable, false, false};

    case Mode::WifiConnecting:
      if (snapshot.screen == Screen::WifiProgress &&
          snapshot.connection == Connection::Connecting &&
          hasNoApData(snapshot)) {
        // Initial association remains fail-closed. Once this boot has reached
        // a validated GOT_IP, a transient reassociation keeps the normal WWW
        // path (and therefore its ordinary SECURITY policy) available.
        return snapshot.staValidatedInThisBoot
            ? WebBinding{WebPage::NormalPanel, false, true}
            : WebBinding{WebPage::WifiTransition, false, false};
      }
      return {WebPage::Unavailable, false, false};

    case Mode::WifiConfigured:
      // This mode is usable only when CORE has confirmed a connected state;
      // persistence or an HTTP success alone cannot create this snapshot.
      if (snapshot.screen == Screen::WifiProgress &&
          snapshot.connection == Connection::Connected &&
          snapshot.staValidatedInThisBoot &&
          !snapshot.hasError && hasNoApData(snapshot))
        return {WebPage::NormalPanel, false, true};
      return {WebPage::Unavailable, false, false};

    case Mode::SecurityRecovery:
      // SECURITY Recovery owns a different, exclusive HTTP runtime.
      return {WebPage::SecurityRecoveryExclusive, false, false};
  }

  return {WebPage::Unavailable, false, false};
}

}  // namespace first_boot
}  // namespace voxone

#endif
