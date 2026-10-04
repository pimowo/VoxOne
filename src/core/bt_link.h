#ifndef VOXONE_BT_LINK_H
#define VOXONE_BT_LINK_H

#include "options.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE

#include <HardwareSerial.h>

#include "bt_link_protocol.h"
#include "bt_runtime.h"

class BtLink {
 public:
  BtLink();

  void begin();
  void loop();
  void requestStatus();
  void requestDiag();
  void ping();
  bool play();
  bool pause();
  bool next();
  bool prev();
  bool setVolume(uint8_t absoluteVolume);

  const BtLinkState& state() const {
    return btRuntime.effectiveLinkState(protocol_.state());
  }
  const BtLinkDiagnostics& diagnostics() const { return protocol_.diagnostics(); }
  bool hasIncompleteOnlineSnapshot() const {
    return btRuntime.available() && protocol_.hasIncompleteOnlineSnapshot();
  }

 private:
  static void sendCommand(void* context, const char* command);
  static void onEvent(void* context, BtLinkEvent event);

  HardwareSerial serial_;
  BtLinkProtocol protocol_;
  bool started_ = false;
  bool runtimeActive_ = false;
};

extern BtLink btLink;

#endif
#endif
