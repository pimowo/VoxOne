# MAIN → VoxOneBT firmware sender (BT-FW-4)

The MAIN-side sender lives in `src/core/bt_firmware_sender.*`; `BtLink` owns it
and exposes `startFirmwareUpdate(BtFirmwareImage&)`,
`abortFirmwareUpdate()`, and `firmwareUpdateProgress()`. The BT-FW-5 WWW caller
retains its staged image source until the transfer reaches a terminal state.
The source supplies exact size, whole
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

## BT-FW-5 upload and staging

`POST /update/bt` accepts one `.bin` with a declared byte length; it does not
use the MAIN/SPIFFS `/update` endpoint. The A0/B0 hardware capability gates
the feature, and runtime PSRAM, module status, PROTO 2, `FW_UPDATE`, and source
other than BT are also required. X0 has no BT upload runtime and uses direct
USB for VoxOneBT. Future Ax/Bx/Cx/Dx profiles may opt in only with the same
capability and sufficient resources. Vx is the module family; V0 is the
current compatible revision. B0 PSRAM size is not assumed by its profile.

The image occupies up to 80 separately allocated 16 KiB PSRAM blocks (last
block may be shorter). A 384 KiB free-PSRAM reserve is enforced before and
after allocation. The V0 OTA-slot maximum is 1,310,720 bytes; the receiver
checks its real slot again. Bookkeeping is at most 80 pointers (320 B on
ESP32-S3) plus small counters. Upload writes update whole-image
CRC-32/ISO-HDLC incrementally. A complete image must start with ESP magic
`0xE9` and contain exactly one valid 60-byte VoxOneImageManifest v1 at any
offset. MAIN independently checks the documented magic, little-endian fields,
V0 identity, PROTO 2, bounded version, zero reserved bytes and manifest CRC.
The sender's expected `FW_VERSION` comes solely from that validated manifest.

An interrupted HTTP upload frees staging without sending `FW_BEGIN`. After
final validation, ownership moves to the MAIN loop; browser disconnect cannot
cancel the UART update. MAIN starts the existing sender and retains staging
through retries until SUCCESS, ERROR or ABORT. A rejected start frees it at
once. `GET /api/bt/update` reports a snapshot of the existing sender progress;
`systemInfo.btFirmwareUpdateSupported` reports profile capability. The WWW
uses this as a minimal status; common MAIN/SPIFFS/BT progress UI is BT-FW-6.
Physical OTA and fault-injection validation remain BT-FW-7.
