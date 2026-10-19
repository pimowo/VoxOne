#include "options.h"
#include <stdarg.h>
#include "config.h"
#include "playlist_store.h"
#include "station_metadata.h"
#include "player.h"
#include "network.h"
#include "network_diagnostics.h"
#include "serialcli.h"
#include "serialcli_command_match.h"
#include "update_progress.h"
#include "wifi_cli_parser.h"
#include "../hardware/runtime_hardware_config.h"
#include <esp_netif.h>

namespace {

void formatIpAddress(char* output, size_t outputSize, const IPAddress& address) {
  voxone::network_diagnostics::formatIpv4(
      output, outputSize, address[0], address[1], address[2], address[3]);
}

void formatEspIpAddress(char* output, size_t outputSize,
                        const esp_ip4_addr_t& address) {
  const auto* octets = reinterpret_cast<const uint8_t*>(&address.addr);
  voxone::network_diagnostics::formatIpv4(
      output, outputSize, octets[0], octets[1], octets[2], octets[3]);
}

const char* dhcpStatusName(esp_netif_dhcp_status_t status) {
  switch (status) {
    case ESP_NETIF_DHCP_INIT: return "init";
    case ESP_NETIF_DHCP_STARTED: return "started";
    case ESP_NETIF_DHCP_STOPPED: return "stopped";
    default: return "unknown";
  }
}

const char* runtimeDisplayName(voxone::hardware::RuntimeDisplay display) {
  switch (display) {
    case voxone::hardware::RuntimeDisplay::None: return "none";
    case voxone::hardware::RuntimeDisplay::Ssd1306_128x64: return "ssd1306";
    case voxone::hardware::RuntimeDisplay::Ssd1309_128x64: return "ssd1309";
    case voxone::hardware::RuntimeDisplay::St7789_284x76: return "st7789";
    case voxone::hardware::RuntimeDisplay::St7796s_480x320: return "st7796s";
  }
  return "unknown";
}

const char* runtimeInputName(voxone::hardware::RuntimeInput input) {
  switch (input) {
    case voxone::hardware::RuntimeInput::None: return "none";
    case voxone::hardware::RuntimeInput::Ec11: return "ec11";
    case voxone::hardware::RuntimeInput::Buttons3: return "buttons3";
  }
  return "unknown";
}

const char* runtimeHardwareSaveStatusName(
    voxone::hardware::RuntimeHardwareSaveStatus status) {
  switch (status) {
    case voxone::hardware::RuntimeHardwareSaveStatus::Saved: return "saved";
    case voxone::hardware::RuntimeHardwareSaveStatus::Unchanged:
      return "unchanged";
    case voxone::hardware::RuntimeHardwareSaveStatus::Invalid: return "invalid";
    case voxone::hardware::RuntimeHardwareSaveStatus::Unsupported:
      return "unsupported";
    case voxone::hardware::RuntimeHardwareSaveStatus::StorageError:
      return "storage-error";
  }
  return "unknown";
}

}  // namespace

SerialCli serialCli;

void SerialCli::handleSerial() {
  uint16_t remaining = voxone::serialcli::kMaxBytesPerLoop;
  while (remaining-- != 0 && Serial.available() > 0) {
    const int value = Serial.read();
    if (value < 0) break;
    const auto event = input_.feed(static_cast<uint8_t>(value), millis());
    if (event == voxone::serialcli::InputEvent::FrameReady)
      on_input(input_.frame(), 0);
  }
  input_.tick(millis());
}

void SerialCli::loop() {
  handleSerial();
}

void SerialCli::printf(const char *format, ...) {
  char message[220];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  if (strcmp(message, "> ") == 0) return;
  char *prompt = strstr(message, "\n> ");
  if (prompt != NULL) prompt[1] = '\0';
  Serial.print(message);
}

void SerialCli::printf(uint8_t id, const char *format, ...) {
  (void)id;
  char message[220];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  Serial.print(message);
}

void SerialCli::commandError(uint8_t clientId) {
  if (commandErrorLimiter_.allow(millis()))
    printf(clientId, "##CMD_ERROR#\tunknown command\n> ");
}

void SerialCli::info() {
  printf("##CLI.INFO#\n");
  strftime(config.tmpBuf, sizeof(config.tmpBuf), "%Y-%m-%dT%H:%M:%S%z", &network.timeinfo);
  printf("##SYS.DATE#: %s\n", config.tmpBuf);
  printf("##CLI.NAMESET#: %d %s\n", config.lastStation(), config.station.name);
  if (player.status() == PLAYING) {
    char interpreted[BUFLEN + 1];
    stationMetaDisplay(config.station.title, config.station.metadataMode == STATION_META_SWAP,
                       interpreted, sizeof(interpreted));
    printf("##CLI.META#: %s\n", interpreted);
  }
  printf("##CLI.VOL#: %d\n", config.store.volume);
  if (player.status() == PLAYING) {
    printf("##CLI.PLAYING#\n");
  } else {
    printf("##CLI.STOPPED#\n");
  }
  printf("> ");
}

void SerialCli::printHeapFragmentationInfo(uint8_t id){
  size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
  size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
  float fragmentation = 100.0 * (1.0 - ((float)largestBlock / (float)freeHeap));
  printf(id, "\n*************************************\n");
  printf(id, "* Free heap: %u bytes\n", freeHeap);
  printf(id, "* Largest free block: %u bytes\n", largestBlock);
  printf(id, "* Fragmentation: %.2f%%\n", fragmentation);
  printf(id, "*************************************\n\n");
}
void SerialCli::on_input(const char* str, uint8_t clientId) {
  if (strlen(str) == 0) return;
  if (updateLockActive() &&
      strcmp(str, "cli.info") != 0 && strcmp(str, "info") != 0 &&
      strcmp(str, "sys.version") != 0 && strcmp(str, "version") != 0 &&
      strcmp(str, "wifi.status") != 0 && strcmp(str, "status") != 0) return;
  if (strcmp(str, "hw show") == 0) {
    const auto& selected = voxone::hardware::runtimeHardwareConfig();
    const auto diagnostics = voxone::hardware::runtimeHardwareDiagnostics();
    printf(clientId, "Runtime HW display: %s\n", runtimeDisplayName(selected.display));
    printf(clientId, "Runtime HW input: %s\n", runtimeInputName(selected.input));
    printf(clientId, "Runtime HW load: %s\n> ",
           voxone::hardware::runtimeHardwareLoadStatusName(
               diagnostics.loadStatus));
    return;
  }
  voxone::hardware::RuntimeDisplay requestedDisplay;
  bool setRuntimeDisplay = false;
  if (strcmp(str, "hw display none") == 0) {
    requestedDisplay = voxone::hardware::RuntimeDisplay::None;
    setRuntimeDisplay = true;
  } else if (strcmp(str, "hw display ssd1306") == 0) {
    requestedDisplay = voxone::hardware::RuntimeDisplay::Ssd1306_128x64;
    setRuntimeDisplay = true;
  } else if (strcmp(str, "hw display ssd1309") == 0) {
    requestedDisplay = voxone::hardware::RuntimeDisplay::Ssd1309_128x64;
    setRuntimeDisplay = true;
  }
  if (setRuntimeDisplay) {
    auto requested = voxone::hardware::runtimeHardwareConfig();
    requested.display = requestedDisplay;
    const auto status =
        voxone::hardware::saveRuntimeHardwareConfig(requested);
    printf(clientId, "Runtime HW display %s: %s\n",
           runtimeDisplayName(requestedDisplay),
           runtimeHardwareSaveStatusName(status));
    if (status == voxone::hardware::RuntimeHardwareSaveStatus::Saved)
      printf(clientId, "Restart required to activate display backend.\n");
    printf(clientId, "> ");
    return;
  }
  voxone::hardware::RuntimeInput requestedInput;
  bool setRuntimeInput = false;
  if (strcmp(str, "hw input ec11") == 0) {
    requestedInput = voxone::hardware::RuntimeInput::Ec11;
    setRuntimeInput = true;
  } else if (strcmp(str, "hw input buttons3") == 0) {
    requestedInput = voxone::hardware::RuntimeInput::Buttons3;
    setRuntimeInput = true;
  } else if (strcmp(str, "hw input none") == 0) {
    requestedInput = voxone::hardware::RuntimeInput::None;
    setRuntimeInput = true;
  }
  if (setRuntimeInput) {
    auto requested = voxone::hardware::runtimeHardwareConfig();
    requested.input = requestedInput;
    const auto status =
        voxone::hardware::saveRuntimeHardwareConfig(requested);
    printf(clientId, "Runtime HW input %s: %s\n",
           runtimeInputName(requestedInput),
           runtimeHardwareSaveStatusName(status));
    if (status == voxone::hardware::RuntimeHardwareSaveStatus::Saved)
      printf(clientId, "Restart required to activate input backend.\n");
    printf(clientId, "> ");
    return;
  }
  if(network.status == CONNECTED){
    if (strcmp(str, "cli.prev") == 0 || strcmp(str, "prev") == 0) {
      player.prev();
      return;
    }
    if (strcmp(str, "cli.next") == 0 || strcmp(str, "next") == 0) {
      player.next();
      return;
    }
    if (strcmp(str, "cli.toggle") == 0 || strcmp(str, "toggle") == 0) {
      player.toggle();
      return;
    }
    if (strcmp(str, "cli.stop") == 0 || strcmp(str, "stop") == 0) {
      player.sendCommand({PR_STOP, 0});
      //info();
      return;
    }
    if (strcmp(str, "cli.start") == 0 || strcmp(str, "start") == 0 || strcmp(str, "cli.play") == 0 || strcmp(str, "play") == 0) {
      player.sendCommand({PR_PLAY, config.lastStation()});
      return;
    }
    if (strcmp(str, "cli.vol") == 0 || strcmp(str, "vol") == 0) {
      printf(clientId, "##CLI.VOL#: %d\n> ", config.store.volume);
      return;
    }
    if (strcmp(str, "cli.vol-") == 0 || strcmp(str, "vol-") == 0) {
      player.stepVol(false);
      return;
    }
    if (strcmp(str, "cli.vol+") == 0 || strcmp(str, "vol+") == 0) {
      player.stepVol(true);
      return;
    }
    if (strcmp(str, "sys.date") == 0 || strcmp(str, "date") == 0 || strcmp(str, "time") == 0) {
      network.requestTimeSync(true);
      return;
    }
    int volume;
    if (voxone::serialcli::matchInt(str, "vol(%d)%n", volume) ||
        voxone::serialcli::matchInt(str, "cli.vol(\"%d\")%n", volume) ||
        voxone::serialcli::matchInt(str, "vol %d%n", volume)) {
      if (volume < 0) volume = 0;
      if (volume > 254) volume = 254;
      player.setVol(volume);
      return;
    }
    if (strcmp(str, "cli.audioinfo") == 0 || strcmp(str, "audioinfo") == 0) {
      printf(clientId, "##CLI.AUDIOINFO#: %d\n> ", config.store.audioinfo > 0);
      return;
    }
    int ainfo;
    if (voxone::serialcli::matchInt(str, "audioinfo(%d)%n", ainfo) ||
        voxone::serialcli::matchInt(str, "cli.audioinfo(\"%d\")%n", ainfo) ||
        voxone::serialcli::matchInt(str, "audioinfo %d%n", ainfo)) {
      config.saveValue(&config.store.audioinfo, ainfo > 0);
      printf(clientId, "new audioinfo value is: %d\n> ", config.store.audioinfo);
      return;
    }
    if (strcmp(str, "cli.smartstart") == 0 || strcmp(str, "smartstart") == 0) {
      printf(clientId, "##CLI.SMARTSTART#: %d\n> ", config.store.smartstart);
      return;
    }
    int sstart;
    if (voxone::serialcli::matchInt(str, "smartstart(%d)%n", sstart) ||
        voxone::serialcli::matchInt(str, "cli.smartstart(\"%d\")%n", sstart) ||
        voxone::serialcli::matchInt(str, "smartstart %d%n", sstart)) {
      config.saveValue(&config.store.smartstart, static_cast<uint8_t>(sstart));
      printf(clientId, "new smartstart value is: %d\n> ", config.store.smartstart);
      return;
    }
    if (strcmp(str, "cli.list") == 0 || strcmp(str, "list") == 0) {
      printf(clientId, "#CLI.LIST#\n");
      PlaylistGuard guard;
      std::vector<PlaylistRow> rows;
      String revision;
      if (guard && playlistStore.snapshot(rows, revision))
        for (size_t i = 0; i < rows.size(); ++i)
          printf(clientId, "#CLI.LISTNUM#: %*u: %s, %s\n", 3,
                 static_cast<unsigned>(i + 1), rows[i].name.c_str(), rows[i].url.c_str());
      printf(clientId, "##CLI.LIST#\n");
      printf(clientId, "> ");
      return;
    }
    if (strcmp(str, "cli.info") == 0 || strcmp(str, "info") == 0) {
      printf(clientId, "##CLI.INFO#\n");
      strftime(config.tmpBuf, sizeof(config.tmpBuf), "%Y-%m-%dT%H:%M:%S%z", &network.timeinfo);
      printf(clientId, "##SYS.DATE#: %s\n", config.tmpBuf);
      printf(clientId, "##CLI.NAMESET#: %d %s\n", config.lastStation(), config.station.name);
      if (player.status() == PLAYING) {
        char interpreted[BUFLEN + 1];
        stationMetaDisplay(config.station.title, config.station.metadataMode == STATION_META_SWAP,
                           interpreted, sizeof(interpreted));
        printf(clientId, "##CLI.META#: %s\n", interpreted);
      }
      printf(clientId, "##CLI.VOL#: %d\n", config.store.volume);
      if (player.status() == PLAYING) {
        printf(clientId, "##CLI.PLAYING#\n");
      } else {
        printf(clientId, "##CLI.STOPPED#\n");
      }
      printf(clientId, "> ");
      return;
    }
    int sb;
    if (voxone::serialcli::matchInt(str, "play(%d)%n", sb) ||
        voxone::serialcli::matchInt(str, "cli.play(\"%d\")%n", sb) ||
        voxone::serialcli::matchInt(str, "play %d%n", sb)) {
      if (sb < 1) sb = 1;
      uint16_t cs = config.playlistLength();
      if (sb >= cs) sb = cs;
      player.sendCommand({PR_PLAY, (uint16_t)sb});
      return;
    }
    if (strcmp(str, "sys.tzo") == 0 || strcmp(str, "tzo") == 0) {
      printf(clientId, "##SYS.TZO#: %d:%d\n> ", config.store.tzHour, config.store.tzMin);
      return;
    }
    //int16_t tzh, tzm;
    int tzh, tzm;
    if (voxone::serialcli::matchTwoInts(str, "tzo(%d:%d)%n", tzh, tzm) ||
        voxone::serialcli::matchTwoInts(
            str, "sys.tzo(\"%d:%d\")%n", tzh, tzm) ||
        voxone::serialcli::matchTwoInts(str, "tzo %d:%d%n", tzh, tzm)) {
      if (tzh < -12) tzh = -12;
      if (tzh > 14) tzh = 14;
      if (tzm < 0) tzm = 0;
      if (tzm > 59) tzm = 59;
      config.setTimezone((int8_t)tzh, (int8_t)tzm);
      if(tzh<0){
        printf(clientId, "new timezone offset: %03d:%02d\n", config.store.tzHour, config.store.tzMin);
      }else{
        printf(clientId, "new timezone offset: %02d:%02d\n", config.store.tzHour, config.store.tzMin);
      }
      network.requestTimeSync(true);
      return;
    }
    if (voxone::serialcli::matchInt(str, "tzo(%d)%n", tzh) ||
        voxone::serialcli::matchInt(str, "sys.tzo(\"%d\")%n", tzh) ||
        voxone::serialcli::matchInt(str, "tzo %d%n", tzh)) {
      if (tzh < -12) tzh = -12;
      if (tzh > 14) tzh = 14;
      config.setTimezone((int8_t)tzh, 0);
      if(tzh<0){
        printf(clientId, "new timezone offset: %03d:%02d\n", config.store.tzHour, config.store.tzMin);
      }else{
        printf(clientId, "new timezone offset: %02d:%02d\n", config.store.tzHour, config.store.tzMin);
      }
      network.requestTimeSync(true);
      return;
    }
    if (voxone::serialcli::matchInt(str, "dspon(%d)%n", tzh) ||
        voxone::serialcli::matchInt(str, "cli.dspon(\"%d\")%n", tzh) ||
        voxone::serialcli::matchInt(str, "dspon %d%n", tzh)) {
      config.setDspOn(tzh!=0);
      return;
    }
    if (voxone::serialcli::matchInt(str, "dim(%d)%n", tzh) ||
        voxone::serialcli::matchInt(str, "cli.dim(\"%d\")%n", tzh) ||
        voxone::serialcli::matchInt(str, "dim %d%n", tzh)) {
      if (tzh < 0) tzh = 0;
      if (tzh > 100) tzh = 100;
      config.store.brightness = (uint8_t)tzh;
      config.setBrightness(true);
      return;
    }
    if (voxone::serialcli::matchTwoInts(
            str, "sleep(%d,%d)%n", tzh, tzm) ||
        voxone::serialcli::matchTwoInts(
            str, "cli.sleep(\"%d\",\"%d\")%n", tzh, tzm) ||
        voxone::serialcli::matchTwoInts(
            str, "sleep %d %d%n", tzh, tzm)) {
      if(tzh>0 && tzm>0) {
        printf(clientId, "sleep for %d minutes after %d minutes ...\n> ", tzh, tzm);
        config.sleepForAfter(tzh, tzm);
      }else{
        commandError(clientId);
      }
      return;
    }
    if (voxone::serialcli::matchInt(str, "sleep(%d)%n", tzh) ||
        voxone::serialcli::matchInt(str, "cli.sleep(\"%d\")%n", tzh) ||
        voxone::serialcli::matchInt(str, "sleep %d%n", tzh)) {
      if(tzh>0) {
        printf(clientId, "sleep for %d minutes ...\n> ", tzh);
        config.sleepForAfter(tzh);
      }else{
        commandError(clientId);
      }
      return;
    }
  }
  if (strcmp(str, "sys.version") == 0 || strcmp(str, "version") == 0) {
    printf(clientId, "##SYS.VERSION#: %s\n> ", YOVERSION);
    return;
  }
  if (strcmp(str, "sys.boot") == 0 || strcmp(str, "boot") == 0 || strcmp(str, "reboot") == 0) {
    ESP.restart();
    return;
  }
  if (strcmp(str, "sys.reset") == 0 || strcmp(str, "reset") == 0) {
    config.reset();
    return;
  }
  if (strcmp(str, "wifi.list") == 0 || strcmp(str, "wifi") == 0) {
    printf(clientId, "#WIFI.SCAN#\n");
    int n = WiFi.scanNetworks();
    if (n == 0) {
        printf(clientId, "no networks found\n");
    } else {
      for (int i = 0; i < n; ++i) {
        printf(clientId, "%d", i + 1);
        printf(clientId, ": ");
        printf(clientId, "%s", WiFi.SSID(i));
        printf(clientId, " (");
        printf(clientId, "%d", WiFi.RSSI(i));
        printf(clientId, ")");
        printf(clientId, (WiFi.encryptionType(i) == WIFI_AUTH_OPEN)?" ":"*");
        printf(clientId, "\n");
        delay(10);
      }
    }
    printf(clientId, "#WIFI.SCAN#\n> ");
    return;
  }
  if (strcmp(str, "wifi.con") == 0 || strcmp(str, "conn") == 0) {
    printf(clientId, "#WIFI.CON#\n");
    File file = SPIFFS.open(SSIDS_PATH, "r");
    if (file && !file.isDirectory()) {
      uint8_t c = 1;
      while (file.available()) {
        if (config.parseSsid(file.readStringUntil('\n').c_str(), config.tmpBuf, config.tmpBuf2)) {
          printf(clientId, "%d: %s, password %s\n", c, config.tmpBuf,
                 config.tmpBuf2[0] ? "set" : "empty");
          c++;
        }
      }
    }
    printf(clientId, "##WIFI.CON#\n> ");
    return;
  }
  if (strcmp(str, "wifi.station") == 0 || strcmp(str, "station") == 0 || strcmp(str, "ssid") == 0) {
    printf(clientId, "#WIFI.STATION#\n");
    File file = SPIFFS.open(SSIDS_PATH, "r");
    if (file && !file.isDirectory()) {
      uint8_t c = 1;
      while (file.available()) {
        if (config.parseSsid(file.readStringUntil('\n').c_str(), config.tmpBuf, config.tmpBuf2)) {
          if(c==config.store.lastSSID)
            printf(clientId, "%d: %s, password %s\n", c, config.tmpBuf,
                   config.tmpBuf2[0] ? "set" : "empty");
          c++;
        }
      }
    }
    printf(clientId, "##WIFI.STATION#\n> ");
    return;
  }
  char ssidbuf[50]{}, passbuff[50]{};
  const voxone::wifi_cli::ParseStatus quotedCredentials =
      voxone::wifi_cli::parseQuotedCredentials(
          str, ssidbuf, sizeof(ssidbuf), passbuff, sizeof(passbuff));
  if (quotedCredentials == voxone::wifi_cli::ParseStatus::Invalid) {
    printf(clientId, "Wi-Fi credentials invalid or save failed\n> ");
    return;
  }
  if (quotedCredentials == voxone::wifi_cli::ParseStatus::Valid ||
      voxone::serialcli::matchTwoStrings(
          str, "wifi.con(%49[^,],%49[^)])%n", ssidbuf, passbuff) ||
      voxone::serialcli::matchTwoStrings(
          str, "wifi.con(%49[^ ] %49[^)])%n", ssidbuf, passbuff) ||
      voxone::serialcli::matchTwoStrings(
          str, "wifi %49[^ ] %49s%n", ssidbuf, passbuff)) {
    if (!config.saveWifiCredentials(ssidbuf, passbuff)) {
      printf(clientId, "Wi-Fi credentials invalid or save failed\n> ");
      return;
    }
    printf(clientId, "Wi-Fi credentials saved; rebooting\n> ");
    Serial.flush();
    ESP.restart();
    return;
  }
  if (strcmp(str, "wifi.status") == 0 || strcmp(str, "status") == 0) {
    const IPAddress ip = WiFi.getMode() == WIFI_STA
        ? WiFi.localIP() : WiFi.softAPIP();
    const IPAddress mask = WiFi.getMode() == WIFI_STA
        ? WiFi.subnetMask() : IPAddress(255, 255, 255, 0);
    const IPAddress gateway = WiFi.getMode() == WIFI_STA
        ? WiFi.gatewayIP() : WiFi.softAPIP();
    const IPAddress dns1 = WiFi.dnsIP(0);
    const IPAddress dns2 = WiFi.dnsIP(1);
    char ipText[16], maskText[16], gatewayText[16], dns1Text[16], dns2Text[16],
        bssidText[18];
    formatIpAddress(ipText, sizeof(ipText), ip);
    formatIpAddress(maskText, sizeof(maskText), mask);
    formatIpAddress(gatewayText, sizeof(gatewayText), gateway);
    formatIpAddress(dns1Text, sizeof(dns1Text), dns1);
    formatIpAddress(dns2Text, sizeof(dns2Text), dns2);
    voxone::network_diagnostics::formatMac(
        bssidText, sizeof(bssidText), WiFi.BSSID());

    esp_netif_t* staNetif =
        WiFi.getMode() == WIFI_STA
            ? esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") : nullptr;
    esp_netif_dhcp_status_t dhcpStatus = ESP_NETIF_DHCP_INIT;
    esp_netif_ip_info_t netifIp{};
    const esp_err_t dhcpResult = staNetif
        ? esp_netif_dhcpc_get_status(staNetif, &dhcpStatus)
        : ESP_ERR_INVALID_STATE;
    const esp_err_t ipResult = staNetif
        ? esp_netif_get_ip_info(staNetif, &netifIp)
        : ESP_ERR_INVALID_STATE;
    char netifIpText[16], netifMaskText[16], netifGatewayText[16];
    formatEspIpAddress(netifIpText, sizeof(netifIpText), netifIp.ip);
    formatEspIpAddress(netifMaskText, sizeof(netifMaskText), netifIp.netmask);
    formatEspIpAddress(netifGatewayText, sizeof(netifGatewayText), netifIp.gw);

    printf(clientId, "#WIFI.STATUS#\nStatus:\t\t%d\nMode:\t\t%s\nIP:\t\t%s\nMask:\t\t%s\nGateway:\t%s\nDNS1:\t\t%s\nDNS2:\t\t%s\nRSSI:\t\t%d dBm\nChannel:\t%d\nBSSID:\t\t%s\n",
      WiFi.status(), WiFi.getMode()==WIFI_STA?"WIFI_STA":"WIFI_AP",
      ipText, maskText, gatewayText, dns1Text, dns2Text, WiFi.RSSI(),
      WiFi.channel(), bssidText);
    printf(clientId, "Network ready:\t%u\nNetif:\t\t%s\nDHCP:\t\t%s (%d)\nNetif IP:\t%s\nNetif mask:\t%s\nNetif gateway:\t%s (%d)\n##WIFI.STATUS#\n> ",
      network.ready() ? 1U : 0U,
      staNetif && esp_netif_is_netif_up(staNetif) ? "up" : "down",
      dhcpStatusName(dhcpStatus), static_cast<int>(dhcpResult), netifIpText,
      netifMaskText, netifGatewayText, static_cast<int>(ipResult)
    );
    return;
  }
  if (strcmp(str, "wifi.rssi") == 0 || strcmp(str, "rssi") == 0) {
    printf(clientId, "#WIFI.RSSI#\t%d dBm\n> ", WiFi.RSSI());
    return;
  }
  if (strcmp(str, "sys.heap") == 0 || strcmp(str, "heap") == 0) {
    //printf(clientId, "Free heap:\t%d bytes\n> ", xPortGetFreeHeapSize());
    printHeapFragmentationInfo(clientId);
    return;
  }
  if (strcmp(str, "sys.config") == 0 || strcmp(str, "config") == 0) {
    config.bootInfo();
    //printf(clientId, "Free heap:\t%d bytes\n> ", xPortGetFreeHeapSize());
    printHeapFragmentationInfo(clientId);
    return;
  }
  if (strcmp(str, "wifi.discon") == 0 || strcmp(str, "discon") == 0 || strcmp(str, "disconnect") == 0) {
    printf(clientId, "#WIFI.DISCON#\tdisconnected...\n> ");
    WiFi.disconnect();
    return;
  }
  commandError(clientId);
}
