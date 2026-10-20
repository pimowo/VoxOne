#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

#include "../src/core/first_boot_captive.h"

using namespace voxone::first_boot;

namespace {

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

WebBinding binding(WebPage page) {
  return {page, page == WebPage::FirstBootSetup,
          page == WebPage::NormalPanel};
}

}  // namespace

int main() {
  const WebBinding firstBoot = binding(WebPage::FirstBootSetup);
  assert(captivePortalActive(firstBoot));

  const char* const phoneAndComputerProbes[] = {
      "/generate_204", "/gen_204", "/hotspot-detect.html",
      "/library/test/success.html", "/connecttest.txt", "/ncsi.txt",
      "/canonical.html", "/success.txt", "/check_network_status.txt",
  };
  for (const char* path : phoneAndComputerProbes) {
    assert(captivePortalProbePath(path));
    assert(captivePortalRedirectAllowed(firstBoot, true, path));
  }

  assert(!captivePortalRedirectAllowed(firstBoot, false, "/generate_204"));
  assert(!captivePortalRedirectAllowed(firstBoot, true, "/"));
  assert(!captivePortalRedirectAllowed(firstBoot, true, "/api/wifi/scan"));
  assert(!captivePortalRedirectAllowed(firstBoot, true, "/unknown"));

  for (WebPage page : {WebPage::Unavailable, WebPage::WifiRecovery,
                       WebPage::NormalPanel, WebPage::WifiTransition,
                       WebPage::SecurityRecoveryExclusive}) {
    const WebBinding other = binding(page);
    assert(!captivePortalActive(other));
    for (const char* path : phoneAndComputerProbes)
      assert(!captivePortalRedirectAllowed(other, true, path));
  }

  const std::string network = readFile("src/core/network.cpp");
  const std::string server = readFile("src/core/netserver.cpp");
  assert(network.find("captiveDnsServer.start(") != std::string::npos);
  assert(network.find("captiveDnsServer.processNextRequest()") !=
         std::string::npos);
  assert(network.find("captivePortalActive(binding)") != std::string::npos);
  assert(server.find("captivePortalRedirectAllowed(") != std::string::npos);
  assert(server.find("response->addHeader(\"Location\", location)") !=
         std::string::npos);
  assert(server.find("response->addHeader(\"Cache-Control\", "
                     "\"no-store, max-age=0\")") != std::string::npos);
  return 0;
}
