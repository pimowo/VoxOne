#ifndef VOXONE_FIRST_BOOT_CAPTIVE_H
#define VOXONE_FIRST_BOOT_CAPTIVE_H

#include "ap_ui_www_binding.h"

#include <cstring>

namespace voxone {
namespace first_boot {

inline bool captivePortalActive(const WebBinding& binding) {
  return binding.page == WebPage::FirstBootSetup &&
         binding.wifiProvisioning && !binding.normalPanel;
}

inline bool captivePortalProbePath(const char* path) {
  if (!path) return false;
  static const char* const probes[] = {
      "/generate_204",              // Android and ChromeOS.
      "/gen_204",                   // Android fallback.
      "/hotspot-detect.html",       // Apple CNA.
      "/library/test/success.html", // Apple legacy CNA.
      "/connecttest.txt",           // Windows NCSI.
      "/ncsi.txt",                  // Windows legacy NCSI.
      "/canonical.html",            // Firefox connectivity service.
      "/success.txt",               // Firefox fallback.
      "/check_network_status.txt",  // NetworkManager/Ubuntu.
  };
  for (const char* probe : probes)
    if (std::strcmp(path, probe) == 0) return true;
  return false;
}

inline bool captivePortalRedirectAllowed(const WebBinding& binding,
                                         bool isGet, const char* path) {
  return isGet && captivePortalActive(binding) &&
         captivePortalProbePath(path);
}

}  // namespace first_boot
}  // namespace voxone

#endif
