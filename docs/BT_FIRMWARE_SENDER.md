# MAIN → VoxOneBT firmware sender (BT-FW-4)

The MAIN-side sender lives in `src/core/bt_firmware_sender.*`; `BtLink` owns it
and exposes `startFirmwareUpdate(BtFirmwareImage&)`,
`abortFirmwareUpdate()`, and `firmwareUpdateProgress()`. This stage has **no
upload or staging provider**: a later caller must retain its image source until
the transfer reaches a terminal state. The source supplies exact size, whole
image CRC-32/ISO-HDLC, expected `FW_VERSION`, and random-access reads. The
sender retains only one 1024-byte DATA frame, not a firmware-sized buffer.

Start requires VoxOneBT online, `PROTO 2`, the exact `FW_UPDATE` capability,
and active source other than BT. It never changes SourceManager or the RADIO
PLAY/STOP intent. MAIN sends `FW_BEGIN`, waits for `FW_READY 1024`, then sends
one binary DATA frame at a time. Bytes are enqueued in at most 64-byte slices
per main-loop iteration and never exceed UART `availableForWrite()`; no delay
is used. `FW_ACK` advances confirmed bytes only after validating sequence and
offset. A retryable `FW_NACK` or 5-second ACK timeout retransmits the exact
same frame, with at most three retransmissions. Fatal errors and caller abort
send a best-effort binary ABORT if the remote may still be in binary mode.

After all DATA ACKs, END is sent. `FW_VERIFY` and then `FW_OK` are required;
100% acknowledged bytes alone are not success. MAIN does not restart. It waits
up to 30 seconds for a post-`FW_OK` `READY`, requests a fresh `GET_STATUS`, and
accepts success only when the new online snapshot has `PROTO 2`, the expected
`FW_VERSION`, and `FW_UPDATE` capability. The version may equal the pre-update
version. Ordinary BT commands, polling, and source observation are suppressed
while the sender owns UART; normal operation resumes on success/error/abort.
The progress snapshot exposes target VoxOneBT, phase, total bytes, ACK-confirmed
bytes, percent and error. Hardware UART/OTA testing remains outstanding.

The wire frame layout and VoxOneBT responses are specified by the separate
VoxOneBT repository's `docs/UART_PROTOCOL.md`; this document describes only
MAIN ownership and success policy.
