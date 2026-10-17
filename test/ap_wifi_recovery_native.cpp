#include "../src/core/ap_wifi_recovery.h"

#include <cassert>

int main() {
  assert(apWifiCredentialsValid("Home", "secret"));
  assert(!apWifiCredentialsValid("", "secret"));
  assert(apWifiCredentialsValid("OpenNetwork", ""));
  assert(!apWifiCredentialsValid("bad\tssid", "secret"));
  assert(!apWifiCredentialsValid("Home", "bad\npassword"));
  assert(!apWifiCredentialsValid("123456789012345678901234567890", "secret"));
  assert(!apWifiCredentialsValid("Home", "1234567890123456789012345678901234567890"));
  assert(netServerShouldInitialize(false));
  assert(!netServerShouldInitialize(true));
}
