#include "bt_link.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE

#include "serialcli.h"
#include "netserver.h"
#include "../hardware/hardware_descriptor.h"
#include "source_manager.h"

namespace {
static constexpr uint32_t BtLinkBaud = 921600;
}

BtLink btLink;

BtLink::BtLink()
    : serial_(1), protocol_(&BtLink::sendCommand, &BtLink::onEvent, this),
      sender_(*this) {
  protocol_.setLineObserver(&BtLink::onLine);
  sender_.setDiagnosticLogger(&BtLink::onFirmwareDiagnostic, this);
}

int BtLink::availableForWrite() { return serial_.availableForWrite(); }
size_t BtLink::write(const uint8_t* bytes, size_t length) {
  return serial_.write(bytes, length);
}

bool BtLink::startFirmwareUpdate(BtFirmwareImage& image) {
  const BtLinkState& current = state();
  BtFirmwareSender::Preconditions p;
  p.online = started_ && btRuntime.available() && current.runtimeAvailable;
  p.protocol = current.protocolVersion;
  p.capabilities = current.capabilities;
  p.activeBtSource = bluetoothSourceSelected();
  const bool started = sender_.start(image, p, millis());
  protocol_.setUpdateExclusive(sender_.exclusive());
  if (started) {
    commandsAccepted_.store(false, std::memory_order_release);
    clearCommands();
  }
  return started;
}

void BtLink::abortFirmwareUpdate() {
  sender_.abort();
  protocol_.setUpdateExclusive(sender_.exclusive());
  commandsAccepted_.store(btRuntime.available() && !sender_.exclusive(),
                          std::memory_order_release);
}

void BtLink::begin() {
  const auto& uart = voxone::hardware::currentHardware().btUart;
  serial_.setRxBufferSize(1024);
  serial_.setTxBufferSize(1024);
  serial_.begin(BtLinkBaud, SERIAL_8N1, uart.rx, uart.tx);
  if (!serial_) {
    serialCli.printf("##[BT]# UART init failed RX=%d TX=%d\n",
                     uart.rx, uart.tx);
    return;
  }
  started_ = true;
  runtimeActive_ = true;
  serialCli.printf("##[BT]# UART RX=%d TX=%d baud=%lu\n",
                   uart.rx, uart.tx, static_cast<unsigned long>(BtLinkBaud));
  protocol_.begin(millis());
  commandsAccepted_.store(true, std::memory_order_release);
  publishState();
}

void BtLink::loop() {
  if (!started_) return;
  const bool enabled = btRuntime.available();
  if (!enabled) {
    commandsAccepted_.store(false, std::memory_order_release);
    if (sender_.exclusive()) sender_.onLine("FW_ERR LINK_UNAVAILABLE", millis());
    protocol_.setUpdateExclusive(false);
    if (runtimeActive_) protocol_.suspend();
    runtimeActive_ = false;
    publishState();
    return;
  }
  if (!runtimeActive_) {
    // Drop bytes accumulated while disabled, then request a fresh snapshot.
    uint16_t remaining = 1024;
    while (remaining-- != 0 && serial_.available() > 0) serial_.read();
    protocol_.begin(millis());
    runtimeActive_ = true;
  }
  protocol_.setUpdateExclusive(sender_.exclusive());
  commandsAccepted_.store(!sender_.exclusive(), std::memory_order_release);
  processCommands();
  // Bound work per main loop so a busy UART cannot starve the radio player.
  uint16_t remaining = 512;
  while (remaining-- != 0 && serial_.available() > 0) {
    const int byte = serial_.read();
    if (byte < 0) break;
    protocol_.feed(static_cast<char>(byte), millis());
  }
  if (sender_.takeStatusProbe()) protocol_.requestStatus(millis());
  sender_.tick(millis());
  protocol_.setUpdateExclusive(sender_.exclusive());
  protocol_.tick(millis());
  commandsAccepted_.store(!sender_.exclusive(), std::memory_order_release);
  publishState();
}

void BtLink::requestStatus() {
  enqueue(BtLinkCommand(BtLinkCommandKind::RequestStatus));
}

void BtLink::requestDiag() {
  enqueue(BtLinkCommand(BtLinkCommandKind::RequestDiagnostics));
}

void BtLink::ping() {
  enqueue(BtLinkCommand(BtLinkCommandKind::Ping));
}

bool BtLink::play() { return enqueue(BtLinkCommand(BtLinkCommandKind::Play)); }
bool BtLink::pause() { return enqueue(BtLinkCommand(BtLinkCommandKind::Pause)); }
bool BtLink::next() { return enqueue(BtLinkCommand(BtLinkCommandKind::Next)); }
bool BtLink::prev() { return enqueue(BtLinkCommand(BtLinkCommandKind::Previous)); }
bool BtLink::setVolume(uint8_t absoluteVolume) {
  BtLinkCommand command{BtLinkCommandKind::SetVolume};
  command.volume = absoluteVolume;
  return enqueue(command);
}
bool BtLink::setSessionVolume(const BtVolumeSessionCommand& command) {
  BtLinkCommand queued{BtLinkCommandKind::SetSessionVolume};
  queued.session = command;
  return enqueue(queued);
}
bool BtLink::takeVolumeSessionEvent(BtVolumeSessionEvent& event) {
  return protocol_.takeVolumeSessionEvent(event);
}
bool BtLink::volumeSessionCapable() const {
  return btRuntime.available() && protocol_.volumeSessionCapable();
}

bool BtLink::enqueue(const BtLinkCommand& command) {
  if (!commandsAccepted_.load(std::memory_order_acquire)) return false;
  portENTER_CRITICAL(&commandMux_);
  const bool queued = commandsAccepted_.load(std::memory_order_relaxed) &&
                      commands_.push(command);
  portEXIT_CRITICAL(&commandMux_);
  return queued;
}

bool BtLink::dequeue(BtLinkCommand& command) {
  portENTER_CRITICAL(&commandMux_);
  const bool found = commands_.pop(command);
  portEXIT_CRITICAL(&commandMux_);
  return found;
}

void BtLink::clearCommands() {
  BtLinkCommand ignored;
  portENTER_CRITICAL(&commandMux_);
  while (commands_.pop(ignored)) {}
  portEXIT_CRITICAL(&commandMux_);
}

void BtLink::processCommands() {
  if (sender_.exclusive()) return;
  BtLinkCommand command;
  while (dequeue(command)) {
    switch (command.kind) {
      case BtLinkCommandKind::RequestStatus:
        protocol_.requestStatus(millis());
        break;
      case BtLinkCommandKind::RequestDiagnostics:
        protocol_.requestDiag();
        break;
      case BtLinkCommandKind::Ping:
        protocol_.ping(millis());
        break;
      case BtLinkCommandKind::Play:
        protocol_.play();
        break;
      case BtLinkCommandKind::Pause:
        protocol_.pause();
        break;
      case BtLinkCommandKind::Next:
        protocol_.next();
        break;
      case BtLinkCommandKind::Previous:
        protocol_.prev();
        break;
      case BtLinkCommandKind::SetVolume:
        protocol_.setVolume(command.volume);
        break;
      case BtLinkCommandKind::SetSessionVolume:
        protocol_.setSessionVolume(command.session);
        break;
    }
  }
}

void BtLink::publishState() {
  const BtLinkState state = protocol_.state();
  const BtLinkDiagnostics diagnostics = protocol_.diagnostics();
  const bool incomplete = protocol_.hasIncompleteOnlineSnapshot();
  portENTER_CRITICAL(&stateMux_);
  publishedState_ = state;
  publishedDiagnostics_ = diagnostics;
  publishedIncompleteSnapshot_ = incomplete;
  portEXIT_CRITICAL(&stateMux_);
}

BtLinkState BtLink::state() const {
  if (!btRuntime.available()) return {};
  portENTER_CRITICAL(&stateMux_);
  const BtLinkState state = publishedState_;
  portEXIT_CRITICAL(&stateMux_);
  return state;
}

BtLinkDiagnostics BtLink::diagnostics() const {
  portENTER_CRITICAL(&stateMux_);
  const BtLinkDiagnostics diagnostics = publishedDiagnostics_;
  portEXIT_CRITICAL(&stateMux_);
  return diagnostics;
}

bool BtLink::hasIncompleteOnlineSnapshot() const {
  if (!btRuntime.available()) return false;
  portENTER_CRITICAL(&stateMux_);
  const bool incomplete = publishedIncompleteSnapshot_;
  portEXIT_CRITICAL(&stateMux_);
  return incomplete;
}

bool BtLink::onLine(void* context, const char* line, uint32_t nowMs) {
  return static_cast<BtLink*>(context)->sender_.onLine(line, nowMs);
}

void BtLink::sendCommand(void* context, const char* command) {
  BtLink* link = static_cast<BtLink*>(context);
  link->serial_.println(command);
}

void BtLink::onFirmwareDiagnostic(void*, const char* message) {
  serialCli.printf("##[BT FW]# %s\n", message);
}

void BtLink::onEvent(void* context, BtLinkEvent event) {
  BtLink* link = static_cast<BtLink*>(context);
  const BtLinkState& state = link->protocol_.state();
  switch (event) {
    case BtLinkEvent::Online:
      serialCli.printf("##[BT]# online proto=%u fw=%s name=%s\n",
                       state.protocolVersion, state.firmwareVersion,
                       state.btName);
      netserver.requestOnChange(WEBSTATUS, 0);
      break;
    case BtLinkEvent::UpdateIdentity:
      link->sender_.onIdentity(state.runtimeAvailable, state.protocolVersion,
                               state.firmwareVersion, state.capabilities,
                               state.otaState);
      break;
    case BtLinkEvent::Offline:
      serialCli.printf("##[BT]# module offline\n");
      netserver.requestOnChange(WEBSTATUS, 0);
      break;
    case BtLinkEvent::Diagnostics: {
      const BtLinkDiagnostics& diag = link->protocol_.diagnostics();
      serialCli.printf("##[BT]# diag reset=%s uptime=%lu heap=%lu minHeap=%lu\n",
                       diag.resetReason, static_cast<unsigned long>(diag.uptime),
                       static_cast<unsigned long>(diag.heap),
                       static_cast<unsigned long>(diag.minHeap));
      break;
    }
    case BtLinkEvent::UnsupportedProtocol:
      serialCli.printf("##[BT]# unsupported proto=%u (need 2)\n",
                       state.protocolVersion);
      break;
  }
}

#endif
