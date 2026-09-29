#ifndef VOXONE_BT_LINK_H
#define VOXONE_BT_LINK_H

#include "options.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE

#include <HardwareSerial.h>

#include "bt_link_protocol.h"

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

  const BtLinkState& state() const { return protocol_.state(); }
  const BtLinkDiagnostics& diagnostics() const { return protocol_.diagnostics(); }
  bool hasIncompleteOnlineSnapshot() const {
    return protocol_.hasIncompleteOnlineSnapshot();
  }

 private:
  static void sendCommand(void* context, const char* command);
  static void onEvent(void* context, BtLinkEvent event);

  HardwareSerial serial_;
  BtLinkProtocol protocol_;
  bool started_ = false;
};

extern BtLink btLink;

#endif
#endif
