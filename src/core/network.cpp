#include "options.h"
#include <ESPmDNS.h>
#include "time.h"
#include "rtcsupport.h"
#include "network.h"
#include "display.h"
#include "config.h"
#include "serialcli.h"
#include "netserver.h"
#include "system_operation_state.h"
#include "player.h"
#include "mqtt.h"
#include "mqtt_config.h"
#include "source_manager.h"
#include "radio_source_policy.h"
#include "network_diagnostics.h"
#include "timekeeper.h"
#include "ui_state.h"
#include <esp_netif.h>
#include <sys/time.h>
#include <atomic>
#include <freertos/task.h>

#ifndef WIFI_ATTEMPTS
  #define WIFI_ATTEMPTS  16
#endif

MyNetwork network;
char apSsid[14] = "VoxOne-Setup";

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

voxone::network::Ipv4ReadinessEvidence gotIpReadinessEvidence(
    const WiFiEventInfo_t& info) {
  const IPAddress localIp = WiFi.localIP();
  esp_netif_t* staNetif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t netifIp{};
  const esp_err_t ipResult = staNetif
      ? esp_netif_get_ip_info(staNetif, &netifIp)
      : ESP_ERR_INVALID_STATE;
  return {
      true,
      info.got_ip.ip_info.ip.addr,
      static_cast<uint32_t>(localIp),
      staNetif != nullptr,
      staNetif && esp_netif_is_netif_up(staNetif),
      ipResult == ESP_OK,
      netifIp.ip.addr,
  };
}

void logWifiEvent(WiFiEvent_t event, const WiFiEventInfo_t& info,
                  bool readyCandidate = false) {
  const IPAddress localIp = WiFi.localIP();
  const IPAddress subnet = WiFi.subnetMask();
  const IPAddress gateway = WiFi.gatewayIP();
  const IPAddress dns1 = WiFi.dnsIP(0);
  const IPAddress dns2 = WiFi.dnsIP(1);
  char localIpText[16], subnetText[16], gatewayText[16], dns1Text[16],
      dns2Text[16], bssidText[18];
  formatIpAddress(localIpText, sizeof(localIpText), localIp);
  formatIpAddress(subnetText, sizeof(subnetText), subnet);
  formatIpAddress(gatewayText, sizeof(gatewayText), gateway);
  formatIpAddress(dns1Text, sizeof(dns1Text), dns1);
  formatIpAddress(dns2Text, sizeof(dns2Text), dns2);
  voxone::network_diagnostics::formatMac(
      bssidText, sizeof(bssidText), WiFi.BSSID());

  const unsigned disconnectReason =
      event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED
          ? static_cast<unsigned>(info.wifi_sta_disconnected.reason)
          : 0U;
  Serial.printf(
      "##[WIFI-EVENT]# t=%lu id=%d name=%s reason=%u status=%d "
      "ip=%s mask=%s gateway=%s dns1=%s dns2=%s rssi=%d channel=%d "
      "bssid=%s\n",
      static_cast<unsigned long>(millis()), static_cast<int>(event),
      WiFi.eventName(event), disconnectReason, static_cast<int>(WiFi.status()),
      localIpText, subnetText, gatewayText, dns1Text, dns2Text, WiFi.RSSI(),
      WiFi.channel(), bssidText);

  esp_netif_t* staNetif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
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
  Serial.printf(
      "##[WIFI-NETIF]# t=%lu present=%u up=%u dhcp=%s dhcpErr=%d "
      "ipErr=%d ip=%s mask=%s gateway=%s\n",
      static_cast<unsigned long>(millis()), staNetif ? 1U : 0U,
      staNetif && esp_netif_is_netif_up(staNetif) ? 1U : 0U,
      dhcpStatusName(dhcpStatus), static_cast<int>(dhcpResult),
      static_cast<int>(ipResult), netifIpText, netifMaskText,
      netifGatewayText);

  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    char eventIpText[16], eventMaskText[16], eventGatewayText[16];
    formatEspIpAddress(eventIpText, sizeof(eventIpText), info.got_ip.ip_info.ip);
    formatEspIpAddress(eventMaskText, sizeof(eventMaskText),
                       info.got_ip.ip_info.netmask);
    formatEspIpAddress(eventGatewayText, sizeof(eventGatewayText),
                       info.got_ip.ip_info.gw);
    const bool localMatches =
        info.got_ip.ip_info.ip.addr == static_cast<uint32_t>(localIp);
    const bool netifMatches = ipResult == ESP_OK &&
        info.got_ip.ip_info.ip.addr == netifIp.ip.addr;
    Serial.printf(
        "##[WIFI-GOT-IP]# t=%lu changed=%u eventIp=%s eventMask=%s "
        "eventGateway=%s localMatch=%u netifMatch=%u readyCandidate=%u\n",
        static_cast<unsigned long>(millis()), info.got_ip.ip_changed ? 1U : 0U,
        eventIpText, eventMaskText, eventGatewayText,
        localMatches ? 1U : 0U, netifMatches ? 1U : 0U,
        readyCandidate ? 1U : 0U);
  }
}

}  // namespace

void MyNetwork::WiFiReconnected(WiFiEvent_t event, WiFiEventInfo_t info){
  const auto evidence = gotIpReadinessEvidence(info);
  const bool readyCandidate = voxone::network::validIpv4Readiness(evidence);
  logWifiEvent(event, info, readyCandidate);
  if (!readyCandidate) {
    network.readyState.disconnected();
    network.status = DISCONNECTED;
    if (network.runtimeCallbacksEnabled)
      network.armWifiReassociation(millis());
    Serial.println("##[WIFI-READY]# GOT_IP rejected: IPv4/netif mismatch");
    return;
  }
  network.readyState.gotIp(evidence);
  network.status = CONNECTED;
  network.resetWifiReassociation();
  if (!network.runtimeCallbacksEnabled) {
    Serial.println("##[WIFI-READY]# ready=1");
    return;
  }
  if (systemRestartPending()) {
    Serial.println("##[WIFI-READY]# ready=1 restartPending=1");
    return;
  }
  if (systemUpdateAudioBlocked()) {
    network.beginReconnect = false;
    network.lostPlaying = false;
    Serial.println("##[WIFI-READY]# ready=1 updateBlocked=1");
    mqttWifiConnected();
    return;
  }
  network.beginReconnect = false;
  player.lockOutput = false;
  transitionUiMode(PLAYER);
  transitionUiMode(PLAYER);
  if (player.temporaryBusy()) network.lostPlaying = false;
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  if (radioWifiReconnectShouldPlay(network.lostPlaying,
                                   !bluetoothSourceSelected(),
                                   sourceManagerRadioPlayIntent()))
    player.sendCommand({PR_RADIO_NETWORK_ONLINE, 0});
#else
  if (radioWifiReconnectShouldPlay(network.lostPlaying, true,
                                   sourceManagerRadioPlayIntent()))
    player.sendCommand({PR_RADIO_NETWORK_ONLINE, 0});
#endif
  network.lostPlaying = false;
  Serial.println("##[WIFI-READY]# ready=1");
  mqttWifiConnected();
}

void MyNetwork::WiFiLostConnection(WiFiEvent_t event, WiFiEventInfo_t info){
  network.readyState.disconnected();
  network.status = DISCONNECTED;
  logWifiEvent(event, info);
  Serial.println("##[WIFI-READY]# ready=0");
  if (!network.runtimeCallbacksEnabled) return;
  if (!wifiRecoveryAllowed(systemRestartPending())) {
    Serial.println("##[SYSTEM]# WiFi disconnect ignored during restart");
    return;
  }
  network.armWifiReassociation(millis());
  if (systemUpdateAudioBlocked()) {
    network.lostPlaying = false;
    network.beginReconnect = true;
    mqttWifiDisconnected();
    return;
  }
  if(!network.beginReconnect){
    const bool temporaryRecovery = player.interruptTemporaryForNetwork();
    Serial.printf("Lost connection, reconnecting to %s...\n", config.ssids[config.store.lastSSID-1].ssid);
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
    const bool radioSelected = !bluetoothSourceSelected();
#else
    const bool radioSelected = true;
#endif
    network.lostPlaying = radioWifiDisconnectShouldSuspend(
        temporaryRecovery, radioSelected, sourceManagerRadioPlayIntent());
    if (network.lostPlaying) {
      player.lockOutput = true;
      player.sendCommand({PR_RADIO_NETWORK_OFFLINE, 0});
    }
    transitionUiMode(LOST);
  }
  network.beginReconnect = true;
  mqttWifiDisconnected();
}

void MyNetwork::armWifiReassociation(uint32_t nowMs) {
  bool armed = false;
  portENTER_CRITICAL(&reassocMux);
  if (staReconnectEnabled) armed = reassocPolicy.arm(nowMs);
  portEXIT_CRITICAL(&reassocMux);
  if (armed) {
    Serial.printf("##[WIFI-REASSOC]# armed t=%lu delay=%lu ms\n",
                  static_cast<unsigned long>(nowMs),
                  static_cast<unsigned long>(
                      voxone::wifi::ReassocPolicy::delayForAttempt(0)));
  }
}

void MyNetwork::resetWifiReassociation() {
  portENTER_CRITICAL(&reassocMux);
  reassocPolicy.connected();
  portEXIT_CRITICAL(&reassocMux);
}

void MyNetwork::loop() {
  if (systemRestartPending()) return;

  const uint32_t nowMs = millis();
  bool reconnect = false;
  uint8_t attempt = 0;
  uint32_t nextDelayMs = 0;
  portENTER_CRITICAL(&reassocMux);
  reconnect = reassocPolicy.takeDue(
      nowMs, staReconnectEnabled, readyState.ready());
  if (reconnect) {
    attempt = reassocPolicy.attempts();
    nextDelayMs = voxone::wifi::ReassocPolicy::delayForAttempt(attempt);
  }
  portEXIT_CRITICAL(&reassocMux);

  if (!reconnect) return;
  const bool requested = WiFi.reconnect();
  Serial.printf(
      "##[WIFI-REASSOC]# t=%lu attempt=%u request=%s nextDelay=%lu ms "
      "status=%d ready=%u\n",
      static_cast<unsigned long>(nowMs), static_cast<unsigned>(attempt),
      requested ? "accepted" : "rejected",
      static_cast<unsigned long>(nextDelayMs), static_cast<int>(WiFi.status()),
      ready() ? 1U : 0U);
}

bool MyNetwork::wifiBegin(bool silent){
  uint8_t ls = (config.store.lastSSID == 0 || config.store.lastSSID > config.ssidsCount) ? 0 : config.store.lastSSID - 1;
  uint8_t startedls = ls;
  uint8_t errcnt = 0;
  //WiFi.mode(WIFI_STA);
  while (true) {
    if(!silent){
      Serial.printf("##[BOOT]#\tAttempt to connect to %s\n", config.ssids[ls].ssid);
      Serial.print("##[BOOT]#\t");
      display.putRequest(BOOTSTRING, ls);
    }
    //WiFi.disconnect(true, true); //disconnect & erase internal credentials https://github.com/e2002/yoradio/pull/164/commits/89d8b4450dde99cd7930b84bb14d81dab920b879
    //delay(100);
    WiFi.mode(WIFI_STA);
    if(strlen(config.store.mdnsname)>0) WiFi.setHostname(config.store.mdnsname);
    WiFi.begin(config.ssids[ls].ssid, config.ssids[ls].password);
    while (WiFi.status() != WL_CONNECTED) {
      if(!silent) Serial.print(".");
      delay(500);
      if(REAL_LEDBUILTIN!=255 && !silent) digitalWrite(REAL_LEDBUILTIN, !digitalRead(REAL_LEDBUILTIN));
      errcnt++;
      if (errcnt > WIFI_ATTEMPTS) {
        errcnt = 0;
        ls++;
        if (ls > config.ssidsCount - 1) ls = 0;
        if(!silent) Serial.println();
        WiFi.mode(WIFI_OFF);
        break;
      }
    }
    if (WiFi.status() != WL_CONNECTED && ls == startedls) {
      return false; break;
    }
    if (WiFi.status() == WL_CONNECTED) {
      config.setLastSSID(ls + 1);
      return true; break;
    }
  }
  return false;
}

#define DBGAP false

void MyNetwork::begin() {
  BOOTLOG("network.begin");
#if RTCSUPPORTED
  if (config.isRTCFound() && rtc.isRunning()) {
    rtc.getTime(&timeinfo);
    timeinfo.tm_isdst = -1;
    if (timeinfo.tm_year >= 120) {
      const time_t rtcEpoch = mktime(&timeinfo);
      if (rtcEpoch > 0) {
        const timeval systemTime = {rtcEpoch, 0};
        settimeofday(&systemTime, nullptr);
        display.putRequest(CLOCK);
      }
    }
  }
#endif
  config.initNetwork();
  setWifiParams();
  if (config.ssidsCount == 0 || DBGAP) {
    raiseSoftAP();
    return;
  }
  if(!wifiBegin()){
    raiseSoftAP();
    Serial.println("##[BOOT]#\tdone");
    return;
  }
  Serial.println(".");
  if (!ready()) {
    Serial.println("##[BOOT]#\tWiFi connected without validated GOT_IP");
    status = FAILED;
    raiseSoftAP();
    return;
  }
  status = CONNECTED;
  staReconnectEnabled = true;
  runtimeCallbacksEnabled = true;
  if(strlen(config.store.mdnsname)>0)
    MDNS.begin(config.store.mdnsname);
  mqttInit();
  
  Serial.println("##[BOOT]#\tdone");
  if(REAL_LEDBUILTIN!=255) digitalWrite(REAL_LEDBUILTIN, LOW);
  
  if (network_on_connect) network_on_connect();
}

void MyNetwork::setWifiParams(){
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);
  WiFi.onEvent(WiFiReconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiLostConnection, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

void MyNetwork::requestTimeSync(bool withSerialOutput) {
  if (!withSerialOutput) {
    timekeeper.forceRtcSync = true;
    timekeeper.restartNtp = true;
    timekeeper.forceTimeSync = true;
    return;
  }
  if (withSerialOutput) {
    static std::atomic<uint32_t> outputSequence{0};
    const uint32_t sequence = outputSequence.fetch_add(1) + 1;
    if (sequence <= 10 || sequence % 100 == 0) {
      Serial.printf("##[TIME]# requestTimeSync output seq=%lu task=%s\n",
                    static_cast<unsigned long>(sequence), pcTaskGetName(nullptr));
    }
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%S%z", &timeinfo);
    serialCli.printf("##SYS.DATE#: %s\n> ", timeStringBuff);
  }
}

void rebootTime() {
  ESP.restart();
}

void MyNetwork::raiseSoftAP() {
  staReconnectEnabled = false;
  resetWifiReassociation();
  char autoRoot[14];
  if (mqttAutoRoot(autoRoot)) {
    std::memcpy(apSsid + 7, autoRoot + 7, 7); // MAC6 plus terminator.
  } else {
    BOOTLOG("AP MAC read failed; using fallback SSID");
  }

  const bool modeReady = WiFi.mode(WIFI_AP);
  if (modeReady && strlen(config.store.mdnsname) > 0)
    WiFi.softAPsetHostname(config.store.mdnsname);
  const bool apReady = modeReady && WiFi.softAP(apSsid, apPassword);
  status = apReady ? SOFT_AP : FAILED;

  Serial.println("##[BOOT]#");
  BOOTLOG("************************************************");
  BOOTLOG("AP Wi-Fi mode: %d (%s)", static_cast<int>(WiFi.getMode()), modeReady ? "ready" : "failed");
  BOOTLOG("AP softAP: %s", apReady ? "success" : "failed");
  if (apReady) {
    const String apIp = WiFi.softAPIP().toString();
    BOOTLOG("AP SSID: %s", apSsid);
    BOOTLOG("AP IP: %s", apIp.c_str());
    BOOTLOG("AP channel: %d", WiFi.channel());
    BOOTLOG("Connect to AP %s with password %s", apSsid, apPassword);
    BOOTLOG("and go to http://%s/ to configure", apIp.c_str());
  }
  BOOTLOG("************************************************");
  if (apReady && config.store.softapdelay > 0)
    timekeeper.waitAndDo(static_cast<uint32_t>(config.store.softapdelay) * 60UL, rebootTime, DelayedActionSlot::REBOOT);
}
