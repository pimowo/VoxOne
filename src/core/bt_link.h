#ifndef VOXONE_BT_LINK_H
#define VOXONE_BT_LINK_H

#include "options.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE

#include <atomic>
#include <HardwareSerial.h>

#include "bt_link_protocol.h"
#include "bt_link_command_queue.h"
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
  bool setSessionVolume(const BtVolumeSessionCommand& command);
  bool takeVolumeSessionEvent(BtVolumeSessionEvent& event);
  bool volumeSessionCapable() const;
  bool startFirmwareUpdate(BtFirmwareImage& image);
  void abortFirmwareUpdate();
  bool firmwareUpdateInProgress() const { return sender_.exclusive(); }
  BtFirmwareSender::Progress firmwareUpdateProgress() const {
    return sender_.progress();
  }

  BtLinkState state() const;
  BtSourceSnapshot sourceSnapshot() const;
  BtLinkDiagnostics diagnostics() const;
  bool hasIncompleteOnlineSnapshot() const;

 private:
  static void sendCommand(void* context, const char* command);
  static void onFirmwareDiagnostic(void* context, const char* message);
  static void onEvent(void* context, BtLinkEvent event);
  static bool onLine(void* context, const char* line, uint32_t nowMs);
  int availableForWrite() override;
  size_t write(const uint8_t* bytes, size_t length) override;
  bool enqueue(const BtLinkCommand& command);
  bool dequeue(BtLinkCommand& command);
  void clearCommands();
  void processCommands();
  void publishState();

  HardwareSerial serial_;
  BtLinkProtocol protocol_;
  BtFirmwareSender sender_;
  bool started_ = false;
  bool runtimeActive_ = false;
  static constexpr size_t CommandQueueCapacity = 16;
  BoundedBtLinkCommandQueue<CommandQueueCapacity> commands_;
  portMUX_TYPE commandMux_ = portMUX_INITIALIZER_UNLOCKED;
  mutable portMUX_TYPE stateMux_ = portMUX_INITIALIZER_UNLOCKED;
  BtLinkState publishedState_{};
  BtLinkDiagnostics publishedDiagnostics_{};
  bool publishedIncompleteSnapshot_ = false;
  std::atomic<bool> commandsAccepted_{false};
};

extern BtLink btLink;

#endif
#endif
