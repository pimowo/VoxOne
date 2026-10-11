#include "../src/core/network_diagnostics.h"

#include <cassert>
#include <cstring>

int main() {
  char ip[16], mask[16], gateway[16], mac[18];
  voxone::network_diagnostics::formatIpv4(
      ip, sizeof(ip), 192, 168, 1, 114);
  voxone::network_diagnostics::formatIpv4(
      mask, sizeof(mask), 255, 255, 255, 0);
  voxone::network_diagnostics::formatIpv4(
      gateway, sizeof(gateway), 192, 168, 1, 1);
  assert(std::strcmp(ip, "192.168.1.114") == 0);
  assert(std::strcmp(mask, "255.255.255.0") == 0);
  assert(std::strcmp(gateway, "192.168.1.1") == 0);

  const uint8_t bssid[] = {0xD0, 0xCF, 0x13, 0x2F, 0xEE, 0x00};
  voxone::network_diagnostics::formatMac(mac, sizeof(mac), bssid);
  assert(std::strcmp(mac, "D0:CF:13:2F:EE:00") == 0);
  voxone::network_diagnostics::formatMac(mac, sizeof(mac), nullptr);
  assert(std::strcmp(mac, "--") == 0);
}
