#include "../src/core/mqtt_config.h"
#include "../src/core/volume_map.h"
#include <cassert>
#include <cstring>
#include <initializer_list>

int main() {
  assert(mqttUserToWire(0) == 0);
  assert(mqttUserToWire(50) == 127);
  assert(mqttUserToWire(100) == 254);
  assert(mqttWireToUser(0) == 0);
  assert(mqttWireToUser(127) == 50);
  assert(mqttWireToUser(254) == 100);
  assert(mqttWireToUser(-1) == 0);
  assert(mqttWireToUser(999) == 100);
  int wire = -1;
  assert(mqttParseWireVolume("vol 127.0", wire) && wire == 127);
  assert(mqttParseWireVolume("vol 999", wire) && wire == 254);
  assert(mqttParseWireVolume("vol -7", wire) && wire == 0);
  assert(!mqttParseWireVolume("vol x", wire));
  for (uint8_t maximum : {20, 60, 100}) {
    assert(mqttUserToWire(100) == 254);
    assert(volumeStateFromUser(100, maximum).raw == volumeRawMaximum(maximum));
  }

  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0x0C, 0x75, 0xF8};
  char autoRoot[14];
  mqttAutoRootFromMac(mac, autoRoot);
  assert(std::strcmp(autoRoot, "voxone-0C75F8") == 0);
  char root[64];
  assert(mqttNormalizeRoot("radio-salon", root, sizeof(root)) && std::strcmp(root, "radio-salon") == 0);
  assert(mqttNormalizeRoot("/radio-salon/", root, sizeof(root)) && std::strcmp(root, "radio-salon") == 0);
  assert(mqttNormalizeRoot("radio//salon", root, sizeof(root)) && std::strcmp(root, "radio/salon") == 0);
  assert(mqttNormalizeRoot(" \t///  ", root, sizeof(root)) && root[0] == '\0');
  assert(mqttNormalizeRoot("", root, sizeof(root)) && root[0] == '\0');
  assert(!mqttNormalizeRoot("radio+salon", root, sizeof(root)));
  assert(!mqttNormalizeRoot("radio#salon", root, sizeof(root)));
  assert(!mqttNormalizeRoot("radio salon", root, sizeof(root)));
  assert(!mqttNormalizeRoot("radio\tsalon", root, sizeof(root)));
  char longRoot[65];
  std::memset(longRoot, 'a', 64);
  longRoot[64] = '\0';
  assert(!mqttNormalizeRoot(longRoot, root, sizeof(root)));

  MqttSettings settings = mqttDefaultSettings();
  assert(settings.enabled == 0 && settings.port == 1883 && mqttValidSettings(settings));
  settings.enabled = 1;
  assert(!mqttValidSettings(settings));
  std::strcpy(settings.host, "broker.local");
  assert(mqttValidSettings(settings));
  settings.port = 0;
  assert(!mqttValidSettings(settings));
  settings.port = 65535;
  assert(mqttValidSettings(settings));
  std::strcpy(settings.password, "secret");
  assert(!mqttValidSettings(settings));
  std::strcpy(settings.username, "user");
  assert(mqttValidSettings(settings));
  assert(mqttApplyPassword(settings, "", false) && std::strcmp(settings.password, "secret") == 0);
  assert(mqttApplyPassword(settings, "new-secret", false) && std::strcmp(settings.password, "new-secret") == 0);
  assert(!mqttApplyPassword(settings, "other", true) && std::strcmp(settings.password, "new-secret") == 0);
  assert(mqttApplyPassword(settings, "", true) && settings.password[0] == '\0');
  for (char byte : settings.password) assert(byte == '\0');
}
