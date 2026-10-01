#include "bt_link.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE

#include "serialcli.h"
#include "netserver.h"

static_assert(VOXONE_BT_UART_RX_PIN != 255 && VOXONE_BT_UART_TX_PIN != 255,
              "BT capable profile requires UART RX and TX pins");

BtLink btLink;

BtLink::BtLink()
    : serial_(1), protocol_(&BtLink::sendCommand, &BtLink::onEvent, this) {}

void BtLink::begin() {
  serial_.setRxBufferSize(1024);
  serial_.begin(115200, SERIAL_8N1, VOXONE_BT_UART_RX_PIN,
                VOXONE_BT_UART_TX_PIN);
  if (!serial_) {
    serialCli.printf("##[BT]# UART init failed RX=%d TX=%d\n",
                     VOXONE_BT_UART_RX_PIN, VOXONE_BT_UART_TX_PIN);
    return;
  }
  started_ = true;
  serialCli.printf("##[BT]# UART RX=%d TX=%d baud=115200\n",
                   VOXONE_BT_UART_RX_PIN, VOXONE_BT_UART_TX_PIN);
  protocol_.begin(millis());
}

void BtLink::loop() {
  if (!started_) return;
  // Bound work per main loop so a busy UART cannot starve the radio player.
  uint16_t remaining = 512;
  while (remaining-- != 0 && serial_.available() > 0) {
    const int byte = serial_.read();
    if (byte < 0) break;
    protocol_.feed(static_cast<char>(byte), millis());
  }
  protocol_.tick(millis());
}

void BtLink::requestStatus() {
  if (started_) protocol_.requestStatus(millis());
}

void BtLink::requestDiag() {
  if (started_) protocol_.requestDiag();
}

void BtLink::ping() {
  if (started_) protocol_.ping(millis());
}

bool BtLink::play() { return started_ && protocol_.play(); }
bool BtLink::pause() { return started_ && protocol_.pause(); }
bool BtLink::next() { return started_ && protocol_.next(); }
bool BtLink::prev() { return started_ && protocol_.prev(); }
bool BtLink::setVolume(uint8_t absoluteVolume) {
  return started_ && protocol_.setVolume(absoluteVolume);
}

void BtLink::sendCommand(void* context, const char* command) {
  BtLink* link = static_cast<BtLink*>(context);
  link->serial_.println(command);
}

void BtLink::onEvent(void* context, BtLinkEvent event) {
  const BtLink* link = static_cast<const BtLink*>(context);
  const BtLinkState& state = link->protocol_.state();
  switch (event) {
    case BtLinkEvent::Online:
      serialCli.printf("##[BT]# online proto=%u fw=%s name=%s\n",
                       state.protocolVersion, state.firmwareVersion,
                       state.btName);
      netserver.requestOnChange(WEBSTATUS, 0);
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
