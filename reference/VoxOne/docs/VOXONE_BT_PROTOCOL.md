# VoxOneBT protocol v1 (prototype)

MAIN implementation checkpoint: `features.bluetooth_enabled=false` leaves
UART uninitialized and module state Disabled. When enabled in NORMAL, MAIN
opens UART2, sends `PROTO 1`/`GET_STATUS`, enters Waiting, and becomes
Unavailable if v1 `READY` does not arrive within 1500 ms. This does not
block RADIO or boot; a late `READY` can recover. `STARTING` restarts the
wait. Unknown/empty frames and lines over 255 bytes are ignored. UART work
is bounded to 256 bytes per application loop. Commands are sent only when
Ready. A Ready control link does not imply a wired PCM path: BT source
selection is rejected until MAIN I2S RX exists.

This is the control/status link, **not** the audio link. UART is 115200,
8N1, 3.3 V logic, common GND, TX crossed to RX. Do not connect 5 V UART.
One ASCII command/event per `\n` line; optional `\r` is ignored. Main sends
`PROTO 1` and `GET_STATUS` after opening UART. VoxOneBT sends `PROTO 1`,
then `STARTING` until A2DP profile initialization completes, then `READY`.
`GET_STATUS` repeats protocol, readiness, transport, playback, sample rate,
logical volume and all metadata. A non-v1 peer must not be controlled.

| Main -> VoxOneBT | Meaning |
|---|---|
| `PROTO 1` | Version check/status request |
| `GET_STATUS` | Full state snapshot (also for recovery after lost events) |
| `PLAY`, `PAUSE` | AVRCP transport; phone remains paired and connected |
| `NEXT`, `PREV` | AVRCP track navigation |
| `SET_VOLUME n` | Logical 0..100, mapped to AVRCP 0..127 |

| VoxOneBT -> Main | Meaning |
|---|---|
| `PROTO 1`, `STARTING`, `READY` | Version and A2DP profile readiness |
| `CONNECTED`, `DISCONNECTED` | A2DP transport, not source selection |
| `PLAYING`, `PAUSED`, `STOPPED` | A2DP audio state |
| `BT_STATE_PLAYING` | Event edge for future auto-switch policy; never on mere CONNECTED or GET_STATUS |
| `DEVICE text` | Connected peer name; empty clears field |
| `ARTIST text`, `TITLE text`, `ALBUM text` | UTF-8 metadata; empty clears field |
| `SAMPLE_RATE hz` | Native negotiated clock of VoxOneBT I2S TX |
| `VOLUME n` | Logical 0..100 AVRCP sync/status |
| `ERR code` | Bad command, absent AVRCP transport, or I2S error |

In text fields `%` and ASCII control bytes are `%HH` escaped. UTF-8 bytes
otherwise pass through unchanged. Main decodes `%HH`; unknown escapes pass
through literally. BT metadata is truncated at 128 bytes per field; Main
accepts at most 255 bytes per line and discards an overlong frame. Commands
are at most 63 bytes. This prototype has no ACK sequence IDs, checksum,
heartbeat or flow control; `GET_STATUS` is the resynchronization primitive.
The queue of asynchronous events is bounded and may drop events under load.
Before treating remote as production-ready, add periodic status polling,
UART-loss detection and a tested reconnection policy.

VoxOneBT outputs unattenuated signed 16-bit stereo PCM in Philips I2S format
as I2S TX master at the native negotiated sample rate. Main will
eventually receive it as I2S RX slave, choose BT/Radio/PlayMedia, apply the
**single** shared logical gain 0..100, and send one I2S TX stream to PCM5102A.
Do not feed VoxOneBT I2S directly to the DAC. Match BCLK/LRCLK/DATA and GND;
do not connect two I2S masters together. Sample-rate changes require Main
RX reconfiguration and buffer/drain handling; neither RX nor router is wired
into the current DESK firmware.

Future PlayMedia: if BT was playing, Main sends `PAUSE`, selects PlayMedia,
then sends `PLAY` after restore. Connection must persist throughout. If BT
was already paused, Main must not send `PLAY`. `BT_STATE_PLAYING` may propose
`baseSource=BT`; CONNECTED alone must not. A playing edge during PlayMedia
is deferred until the override ends.

Security/limits: this is a trusted, point-to-point wire protocol without
authentication. It must not be exposed to an untrusted serial bus. Hardware
validation, lost-frame handling, volume echo suppression and PCM clock-domain
testing are open tasks; successful compilation does not establish them.
