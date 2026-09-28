#include "../src/core/ap_wifi_recovery.h"

#include <cassert>

int main() {
  // WWW content on SPIFFS is independent of AP credential recovery.
  assert(apWifiRecoveryAllowed(false));
  assert(apWifiCredentialsValid("Home", "secret"));
  // Empty SPIFFS uses the same recovery path.
  assert(apWifiRecoveryAllowed(false));
  // Connected requests must continue through the normal authenticated handler.
  assert(!apWifiRecoveryAllowed(true));
  assert(!apWifiCredentialsValid("", "secret"));
  assert(apWifiCredentialsValid("OpenNetwork", ""));
  assert(!apWifiCredentialsValid("bad\tssid", "secret"));
  assert(!apWifiCredentialsValid("Home", "bad\npassword"));
  assert(!apWifiCredentialsValid("123456789012345678901234567890", "secret"));
  assert(!apWifiCredentialsValid("Home", "1234567890123456789012345678901234567890"));
  assert(netServerShouldInitialize(false, false));
  assert(!netServerShouldInitialize(true, false));
  assert(!netServerShouldInitialize(false, true));
}
