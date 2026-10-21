#ifndef VOXONE_NATIVE_WIFI_MOCK_H
#define VOXONE_NATIVE_WIFI_MOCK_H

#include <cstdint>

class IPAddress {
 public:
  explicit IPAddress(uint32_t value = 0) : value_(value) {}
  bool operator!=(const IPAddress& other) const { return value_ != other.value_; }

 private:
  uint32_t value_;
};

class NativeWiFiMock {
 public:
  bool isConnected() const { return connected; }
  IPAddress localIP() const { return IPAddress(0xC0A8012A); }

  bool connected = true;
};

extern NativeWiFiMock WiFi;

#endif
