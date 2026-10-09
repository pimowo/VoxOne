#ifndef VOXONE_BT_LINK_H
#define VOXONE_BT_LINK_H

#include "options.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE

#include <HardwareSerial.h>

#include "bt_link_protocol.h"
#include "bt_firmware_sender.h"
#include "bt_runtime.h"

class BtLink : private BtFirmwareTransport {
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
  bool startFirmwareUpdate(BtFirmwareImage& image);
  void abortFirmwareUpdate();
  bool firmwareUpdateInProgress() const { return sender_.exclusive(); }
  BtFirmwareSender::Progress firmwareUpdateProgress() const {
    return sender_.progress();
  }

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
  static bool onLine(void* context, const char* line, uint32_t nowMs);
  int availableForWrite() override;
  size_t write(const uint8_t* bytes, size_t length) override;

  HardwareSerial serial_;
  BtLinkProtocol protocol_;
  BtFirmwareSender sender_;
  bool started_ = false;
  bool runtimeActive_ = false;
};

extern BtLink btLink;

#endif
#endif
