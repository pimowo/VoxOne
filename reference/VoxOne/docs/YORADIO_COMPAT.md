# VoxOne / yoRadio compatibility standard

Current checkpoint: the only MQTT interface is the ha_yoradio-compatible
command, status, volume and retained playlist set. PLAY_MEDIA/TTS URLs use the temporary
highest-priority override. The base source model is RADIO / BT / STOP;
double-click and logical BT CONNECTED/DISCONNECTED switching leave the new
source stopped. MAIN uses BtLink/RemoteBluetoothBackend and VoxOneBT is a
separate module. Automatic BT switching is hardware-test-pending.

The internal radio transport is deliberately outside the MQTT wire contract.
Its retained runtime baseline is an 8192-byte MP3 input buffer, network reads
up to 4096 bytes, decoded PCM writes up to 4608 bytes, I2S DMA 8 x 512 and
maximum shared gain Q15=16384. These values do not add topics or fields.
Display rendering runs in DisplayTask on core 0.

## yoRadio MQTT compatibility (implemented subset)

VoxOne MAIN subscribes only to the exact `<root>/command` topic and accepts
`prev`, `next`, `toggle`, `stop`,
`start`, `turnon`, `turnoff`, `volm`, `volp`, `vol N`, `play`, and `play N`.
Radio station selection uses
the existing StationStore/App routing; BT controls use the existing BtLink
routing only when BT is the selected source. `stop` pauses the active radio
stream without changing RADIO source intent, so a later `start` can resume it.
This also matches the component's `async_media_pause`, which sends `stop`.
`turnoff` saves the base source and selected station, stops playback and
reports `on=0`; `turnon` reports `on=1` and restores the saved RADIO station
when available. These are logical player states, not display power control.

Legacy `vol N` is clamped to 0..254 and rounded to VoxOne logical 0..100 as
`(N*100+127)/254`; published `<root>/volume` uses
`(logical*254+50)/100`. `volm` and `volp` change logical volume by one step.
`play N` uses yoRadio's one-based playlist position, not the stable internal
StationStore ID. Position N is resolved through the current persisted order
and then routed through App using that record's internal ID. A bare `play`
targets position 1. Out-of-range positions are rejected. `start` resumes the
selected radio station; from STOP it has no effect. VoxOne does not implement
yoRadio smartstart.

`<root>/status` is retained JSON with exactly `status` (integer 0/1),
`station` (selected one-based playlist position), `name` (station or BT peer), `title` (artist and
title joined with ` - `), and `on` (integer logical player ON/OFF 0/1).
`<root>/volume` is a retained decimal integer in 0..254. Status and volume are
refreshed only when their respective payload changes and on MQTT reconnect.
These fields are a best-effort
mapping from VoxOne StateStore, not a claim that the entire yoRadio model is
implemented. Status does not contain a source identifier.

yoRadio's `<root>/playlist` is a retained URL pointing to a retrievable TSV
file at `/data/playlist.csv`, not an inline list. VoxOne publishes the stable
URL `http://<current LAN IP>/data/playlist.csv`; the endpoint is
read-only and generated dynamically from StationStore. Playlist entries keep
yoRadio's `name<TAB>URL<TAB>ovol` record shape. Records with empty fields or
embedded tabs/newlines are skipped because the upstream parser does not support
quoted TSV fields. Commas and quotes remain literal field data.

The URL is republished on MQTT reconnect and if the LAN IP changes while
connected. The audited ha_yoradio 0.11.0 component numbers nonempty playlist
rows starting at 1, publishes `play N` for station selection, and reads
`status`, `volume`, and `playlist` as retained MQTT messages. Its metadata
parser splits `title` at the first ` - `; `name` becomes media_channel.
`browse_media` works within Home Assistant and needs no firmware command
until a media item is selected.

Direct HTTP MP3 URLs sent by the HA component now enter PLAY_MEDIA as a
temporary override. MAIN snapshots the base source and playback intent, uses
`AudioOutputOwner::PlayMedia`, and restores RADIO/BT/STOP on EOF, error or
cancel. HTTPS, redirects, AAC/OGG, arbitrary filesystem playlists and embedded
metadata are not implemented. Home Assistant integration is provided by
`ha_yoradio`; VoxOne does not publish native MQTT Discovery entities.

## Scope and evidence

This document defines a wider future protocol standard. The MQTT subset above
is implemented; yoRadio WebSocket and full client compatibility are not.
VoxOne remains firmware 0.4.0.
The goal is to replace yoRadio for yoPILOT, NexaPanel, NexaPanelMini and Home
Assistant with only an IP/hostname change. All client acceptance tests are pending.

Upstream inspected on 2026-09-18: yoRadio main commit
`2fd3e388d528756f7db666b2261326a6ff234dc7`. Protocol-bearing netserver, MQTT,
command handler, player and HA files in the local reference match that revision.
Local config.cpp differs; its relevant parsers were checked against upstream.

Primary evidence:

- [WS serialization and events](https://github.com/e2002/yoradio/blob/2fd3e388d528756f7db666b2261326a6ff234dc7/yoRadio/src/core/netserver.cpp): processQueue, onWsMessage, getPlaylist, onWsEvent.
- [MQTT topics, publishers and commands](https://github.com/e2002/yoradio/blob/2fd3e388d528756f7db666b2261326a6ff234dc7/yoRadio/src/core/mqtt.cpp).
- [Command dispatch](https://github.com/e2002/yoradio/blob/2fd3e388d528756f7db666b2261326a6ff234dc7/yoRadio/src/core/commandhandler.cpp).
- [Text parsers and display power](https://github.com/e2002/yoradio/blob/2fd3e388d528756f7db666b2261326a6ff234dc7/yoRadio/src/core/config.cpp): parseWsCommand, parseCSV, setDspOn.
- [Player and volume steps](https://github.com/e2002/yoradio/blob/2fd3e388d528756f7db666b2261326a6ff234dc7/yoRadio/src/core/player.cpp).
- [HA consumer](https://github.com/e2002/yoradio/blob/2fd3e388d528756f7db666b2261326a6ff234dc7/HA/custom_components/yoradio/media_player.py).

**Confirmed upstream** means existing yoRadio behavior. **VoxOne proposal**
means a future adapter contract, not functionality already implemented here.

## WebSocket contract

### Confirmed upstream

Endpoint: `ws://<host>/ws`, on the HTTP server (port 80 by default).
Server messages are JSON text; retain the original JSON value types.

| Information | Wire message |
| --- | --- |
| Station name | `{"payload":[{"id":"nameset","value":"Radio 357"}]}` |
| Metadata | `{"payload":[{"id":"meta","value":"Artist - Title"}]}` |
| Bitrate / format | `{"payload":[{"id":"bitrate","value":128},{"id":"fmt","value":"MP3"}]}` |
| Player state | `{"payload":[{"id":"playerwrap","value":"playing"}]}` or value `"stopped"` |
| Volume | `{"payload":[{"id":"volume","value":127}]}`; integer 0..254 |
| Current station | `{"current":12}`; top-level integer, normally 1-based |
| Playlist location | `{"file":"http://<ip>/data/playlist.csv"}`; top-level URL |
| Ping response | `{"pong":1}` |

There is no legacy playerwrap value `paused`. Do not turn current/file into
payload IDs or replace the playlist URL with an inline station array.
Format strings include MP3, AAC, FLC, OGG, WAV; fallback is `bitrate`.
Upstream's FLAC token is specifically FLC, not FLAC.

Inbound commands are text `command=value`, NOT JSON or MQTT-style spaces:
`start=`, `stop=`, `prev=`, `next=`, `toggle=`, `volp=`, `volm=`, `vol=127`,
`volume=127`, `play=12`, `playstation=12`, `getindex=`, `ping=`.
The equals sign is required even without an argument.

getindex queues station name/current, metadata, volume, EQ, balance,
bitrate/format, player state, SD availability and player mode. Replies are a
sequence of messages, not one atomic JSON object. A WS connection does not
automatically initiate this snapshot in the inspected revision. Playlist is a
separate PLAYLIST queue event: do not assume getindex returns it or invent a
getplaylist command without checking actual client requests.

Additional confirmed payload IDs: bass, middle, trebble (historical spelling),
balance, rssi, heap. Additional top-level fields: sdinit and playermode
(modeweb/modesd). Here heap is an audio-buffer fill percentage when enabled,
NOT free heap bytes. Further settings messages are outside this playback profile.

### VoxOne proposal

Preserve legacy IDs, messages and types. Escape JSON strings correctly, including
quotes, backslashes, controls and UTF-8. Broadcast committed state changes;
directed queries reply to the requester. Serialize immutable StateStore snapshots
to avoid mixing fields from different sources.

## Source extension

An optional additional, separate top-level WS message:

```json
{"source":"RADIO"}
```

Allowed values: RADIO, BT, STOP. This supplements legacy messages and never
replaces them. Initially keep it separate so legacy payload shapes stay unchanged.
Unknown-message tolerance must be tested for every client; provide legacy-only
mode if necessary. Do not encode source in fmt, current, playermode or station IDs.

## MQTT contract

### Confirmed upstream

yoRadio concatenates MQTT_ROOT_TOPIC with the suffix and inserts NO slash.
Configure the macro as `"yoradio/"` for the topics below. HA instead takes the
logical root `yoradio` and appends `/...`; avoid missing/doubled separators.

| Example topic | Direction | Payload |
| --- | --- | --- |
| yoradio/command | Client -> device | Plain-text command |
| yoradio/status | Device -> clients | JSON object below |
| yoradio/playlist | Device -> clients | Plain URL http://<ip>/data/playlist.csv |
| yoradio/volume | Device -> clients | Plain decimal integer 0..254, e.g. 127 |

Command subscription is QoS 2. Status, playlist and volume publish at QoS 0,
retain=true, including on broker connection. Preserve these semantics; commands
must not be retained. There is no legacy status bitrate or availability field.

```json
{"status":1,"station":12,"name":"Radio 357","title":"Artist - Title","on":1}
```

| Key | Type / meaning |
| --- | --- |
| status | Integer 1 playing / 0 stopped; not strings or booleans |
| station | Integer last/current radio station, normally 1..playlist length |
| name | String station name |
| title | String station/track metadata |
| on | Integer 0/1 from config.store.dspon: display power |

**on means display power, not availability, connectivity or playback.**
HA maps status=0,on=1 to idle; status=0,on=0 to off; status=1 to playing.
Do not set on=0 just because playback stops or BT disconnects. An awake display
normally reports on=1; future display-power controls must reflect real state.
Never publish yoRadio's internal STOPPED enum 2: MQTT explicitly translates it to 0.

Clients fetch the playlist URL over HTTP. Despite `.csv`, entries are tab-separated
`name<TAB>URL<TAB>ovol`, one per line. HA numbers nonempty names starting at 1.
The future endpoint can generate this in memory without a filesystem. The URL
must be retrievable and numbering must refer to a real, consistent station list.

### VoxOne implementation

VoxOne keeps exactly the five legacy status keys/types, including for BT, and
does not add source, codec or bitrate fields or topics. MQTT uses only
`command`, `status`, `volume` and `playlist` under the normalized root.
Existing root, credentials and broker settings must match for replacement to
work.

## Command mapping

Confirmed inbound forms and proposed future CommandQueue translation:

| Meaning | WS text | MQTT text | VoxOne dispatch |
| --- | --- | --- | --- |
| Previous | prev= | prev | Previous |
| Next | next= | next | Next |
| Toggle | toggle= | toggle | TogglePlayStop |
| Stop | stop= | stop | SetStop |
| Start last station / resume | start= | start | SetPlay, subject to active-source policy |
| Volume down | volm= | volm | Logical volume -1 -> future queue action |
| Volume up | volp= | volp | Logical volume +1 -> future queue action |
| Absolute volume | vol=127 / volume=127 | vol 127 | Converted SetVolumeAbsolute |
| Select/play station | play=12 / playstation=12 | play 12 | Future SelectStation / PlayStation command |

Bare play is NOT an upstream synonym for start. Its empty value parses as zero
and clamps to station 1 when a playlist exists. HA media_play uses start, while
source selection uses play N. Preserve that distinction for RADIO. Do not assume
MQTT aliases volume 127 or playstation 12 work.

HA can send `vol 127.0` or another positive decimal. Upstream `%d` consumes its
integer prefix. The future adapter should accept this and truncate the positive
fraction before clamping 0..254, rather than rejecting HA commands.

MQTT turnoff turns off the display and stops playback, preserving smartstart;
turnon turns on the display and may start the last station according to smartstart.
HA uses both. WS dspon=0/1 also controls display power. Future VoxOne power/queue
semantics are not designed yet; a playback-only profile cannot claim full HA
compatibility without addressing these. getindex/ping are queries, not audio actions.

Current CommandQueue has no station-selection action or MQTT source enum. Do not
misuse SetPlay.value: App does not implement station selection that way. Future
adapters validate bounded input, enqueue commands and leave ownership decisions
to App. They must never operate I2S directly. Invalid input/queue-full must have no
partial audio effects. Safely reassemble MQTT fragments instead of copying
upstream's length-based direct-URL parser.

## Mapping RADIO

- nameset / MQTT name: selected station name.
- meta / MQTT title: structured artist + ` - ` + title, or the stream's raw title;
  missing metadata remains empty.
- WS bitrate: compressed stream bitrate in decimal kb/s, not decoded PCM.
- WS fmt: actual decoder codec. MP3-only today; this contract does not add AAC.
- current / MQTT station: consistent real playlist ID, starting at 1.
- playerwrap / status: playing / 1 only when playback is running; stopped / 0
  while connecting, failed or stopped.
- Optional source: RADIO when App retains RADIO source ownership.

For VBR, define/test current-frame versus smoothed compressed-audio bitrate.
Exclude HTTP/TCP/ICY overhead. Never publish stale codec or another station's metadata.

## Mapping BLUETOOTH

- nameset / MQTT name: connected peer name, with an honest known-peer fallback.
- meta / MQTT title: AVRCP artist + ` - ` + title; if one field is empty, use the
  other alone. Do not invent track metadata or a station name.
- WS fmt: SBC for the actual negotiated SBC transport, not PCM.
- WS bitrate: actual calculated/measured SBC bitrate; 0 if unavailable.
- current / MQTT station: retain the last valid RADIO ID. Do not use BT station
  0/-1 or inject a virtual BT station. With no valid playlist, compatibility of
  station IDs is unresolved; do not fabricate an entry to claim compatibility.
- playerwrap / status: A2DP audio-started -> playing / 1; suspended, stopped or
  disconnected -> stopped / 0. A connection alone does not mean playing.
- MQTT on: real display power, as for RADIO.
- Optional source: BT while App retains BT ownership, including reconnect grace.

BT command policy: prev/next -> AVRCP previous/next; toggle -> play/pause;
start -> AVRCP play; stop -> AVRCP stop or a defined local stop policy, NOT
A2DP teardown. Volume is global VoxOne volume. Station-select play N while BT
owns audio is an accepted playback no-op: do not disconnect BT, start RADIO
underneath it or interpret N as an AVRCP opcode. Re-publish actual state to undo
optimistic client changes. Explicit future source selection is a separate policy.

Existing reconnect grace stays 10 seconds. Retain known peer/metadata until grace
expires as App specifies, report stopped while disconnected and set unknown
bitrate to 0. On final ownership loss publish actual fallback source/state;
optional STOP denotes no active source. No Bluetooth code changes are requested.

## Volume 0..254 outside / 0..100 inside

Legacy boundary conversion, if an absolute yoRadio value must be adapted:

```text
internal = floor((external * 100 + 127) / 254)
external_reported = floor((internal * 254 + 50) / 100)
```

0 maps exactly to 0 and 254 to 100 at the compatibility boundary. Wire
compatibility preserves 0..254; VoxOne internal logical volume is only 0..100.

The conversion is performed only by the future YoRadioCompatService. VoxOne
does not use this mapping as its internal model. volp and volm are logical +/-1
commands in the 0..100 range, with ordinary clamping. Test monotonic steps,
clamping and feedback-loop clients at the
compatibility boundary.

## Bitrate semantics and SBC calculation

Primary evidence: [ESP-IDF 5.3 A2DP API](https://github.com/espressif/esp-idf/blob/v5.3/components/bt/host/bluedroid/api/include/api/esp_a2dp_api.h),
[IDF SBC information parser](https://github.com/espressif/esp-idf/blob/v5.3/components/bt/host/bluedroid/stack/a2dp/a2d_sbc.c),
[BlueZ SBC implementation](https://kernel.googlesource.com/pub/scm/bluetooth/sbc/+/refs/heads/master/sbc/sbc.c)
(sbc_get_frame_length and sbc_get_frame_duration). The BlueZ link is a moving
reference; the checked formula is recorded explicitly below.

Fs: sample rate in Hz; S: subbands 4/8; B: blocks 4/8/12/16;
C: channels (mono=1, otherwise 2); P: **actual current frame bitpool**.

```text
base = 4 + (4 * S * C) / 8
mono / dual-channel: L = base + ceil(B * C * P / 8)
stereo:              L = base + ceil(B * P / 8)
joint-stereo:        L = base + ceil((S + B * P) / 8)
frame_duration_seconds = S * B / Fs
bitrate_bps = 8 * L * Fs / (S * B)
bitrate_kbps = bitrate_bps / 1000
```

L includes SBC header, CRC, scale factors and padding, not RTP/AVDTP/L2CAP overhead
or retransmissions. Allocation method (SNR/loudness) does not alter length at fixed
parameters. Example: Fs=44100, joint stereo, B=16, S=8, P=53 -> L=119 bytes ->
327993.75 bit/s, approximately 328 kb/s; Fs=48000 gives 357 kb/s.
For variable bitpool, average encoded frame bytes over their audio durations.
Never use PCM byte count or the 44.1-kHz/16-bit/stereo PCM rate 1411.2 kb/s.

### Available data and integration point

Installed ESP-IDF 5.3 exposes ESP_A2D_AUDIO_CFG_EVT with audio_cfg.mcc.cie.sbc[4]:

| Byte | Information |
| --- | --- |
| 0 | Sampling-frequency mask (high nibble), channel mode (low nibble) |
| 1 | Block-length mask (high nibble), subbands and allocation method |
| 2 | Minimum negotiated bitpool |
| 3 | Maximum negotiated bitpool |

Validate SBC type and a selected configuration; multi-bit capability masks do
not identify one runtime configuration. Sample-rate callbacks alone are insufficient;
a two-channel count cannot distinguish dual/stereo/joint modes. Parse raw IE using
the IDF layout rather than library-helper assumptions or newer-IDF-only fields.

The negotiated min/max bitpool range is NOT the actual frame bitpool. Unless it
collapses to one fixed value, calculating with max_bitpool gives only a bound.
This stack's public sink data callback delivers decoded PCM, which cannot reveal
actual SBC bitrate. SBC frame headers (sync 0x9c) carry actual parameters/bitpool;
an encoded-frame observer before decode is required for variable-bitpool reporting.

The old MAIN `BluetoothLifecycleSink`/`BluetoothService` integration is retired.
Any future SBC observer belongs in the separate VoxOneBT firmware and may
send bounded status through UART v1 or a later version. Until an encoded-frame
hook is proven, report bitrate as unknown (0), not a calculated maximum.
MAIN currently does not implement these measurements.

## Backward compatibility rules / unsupported cases

1. Preserve IDs, historical spelling, JSON types, topic payloads and volume range.
2. Preserve exactly five MQTT status fields. Do not renumber stations, replace
   URL playlists with JSON or confuse display power with player/availability.
3. Do not add MQTT extensions outside command/status/volume/playlist.
4. App retains source decisions; exclusive AudioOutputManager access and BT
   priority/grace remain unchanged. Adapters only enqueue/serialize, never write I2S.
5. Preserve valid wire behavior, not unsafe parsing, integer wrap or misleading telemetry.
6. Settings pages, playlist editing/uploads, EQ, SD playback, timers, destructive
   reset/format and all configuration queries are outside this minimal profile.
7. HA can send media URLs over MQTT. VoxOne accepts bounded HTTP/HTTPS payloads;
   the current decoder plays direct HTTP MP3 only and rejects unsupported HTTPS
   safely through the normal override restore path.
8. This document does not implement WS/MQTT, ICY metadata, station list/persistence,
   stream reconnect or AAC/FLAC/Opus. Advertise only codecs actually supported.
9. Retained status is not an availability heartbeat. Offline/availability policy
   needs a separate contract; never overload on for this purpose.

## Test acceptance criteria (all pending)

| ID | Required test |
| --- | --- |
| A | Pin yoPILOT version; change only IP/host; verify initialization, controls, playlist and reconnect. |
| B | Pin NexaPanel and NexaPanelMini versions; both work without protocol changes; capture WS/MQTT traffic. |
| C | Existing HA integration understands status/volume/playlist, idle/off/playing and turnon/turnoff. |
| D | RADIO shows station/title and consistent current station / playlist IDs like yoRadio. |
| E | BT shows peer name and AVRCP artist-title, including missing fields and escaped UTF-8. |
| F | MQTT exposes no topics beyond command/status/volume/playlist. |
| G | External volume 0..254 works: endpoints, HA decimals, repeated +/-1 and coherent quantization across inputs. |
| H | MQTT status has exactly five original keys/types; QoS/retain/reconnect publication match legacy behavior. |
| I | WS bitrate is compressed RADIO stream or actual SBC transport bitrate, never PCM; unknown is 0. |
| J | WS fmt is actual RADIO codec (MP3 now, AAC only if later implemented) or SBC for BT. |

Also test connecting/stopped/BT-paused states, grace before/after 10 seconds,
BT play N no-op, invalid/fragmented input, queue saturation, stale retained data,
source switching and the invariant that BT/Radio never write I2S simultaneously.
Protocol acceptance does not replace pending hardware audio/lifecycle/partition tests.

## Open questions

- Exact deployed client versions, initial requests and playlist retrieval flows?
  Obtain packet captures; server source cannot prove third-party behavior.
- Which extra queries/settings/direct-URL commands are actually used?
- How will the real station list/stable IDs work, including no-list startup?
- Which display-power/smartstart semantics will satisfy HA turnon/turnoff?
- Which station-selection and transport-source CommandQueue entries are needed?
- Can a supported encoded-SBC hook reveal current bitpool without a framework
  rebuild? Until proven, BT bitrate remains unknown rather than fabricated.
- Which VBR averaging window and integer rounding should be standardized?
- Is 101-level volume quantization acceptable? Do clients echo reported volume?
- Which fallback-name/STOP metadata behavior do the panels require?
- Which authentication policy should protect the existing four-topic boundary?
  Keep payload limits and reconnect snapshots deterministic.

## Future implementation boundary

WS/MQTT compatibility adapters -> validated CommandQueue -> App ownership ->
RadioService or RemoteBluetoothBackend -> AudioOutputManager (BT PCM after
future I2S RX/router wiring). Outbound adapters read
committed StateStore snapshots. No independent audio owner, direct callback I2S
path or transport-driven source heuristic is permitted. This standard is a baseline,
not a completed 0.5.0 implementation.


## VoxOne PLAY_MEDIA extension - first runtime stage

This future extension does not change legacy yoRadio WebSocket or MQTT.
VoxOne has three normal base states: RADIO, BT and STOP. PLAY_MEDIA is a
physical third audio owner but a temporary highest-priority override, never a
normal user source:

```text
RADIO -> PLAY_MEDIA -> RADIO
BT    -> PLAY_MEDIA -> BT
STOP  -> PLAY_MEDIA -> STOP
```

PLAY_MEDIA may carry TTS, MP3 files, notification sounds, HA-selected media or
supported audio URLs. Home Assistant generates and queues requests. It sends playback requests only;
it must not implement pause/wait/play timing, guess duration or restore source.
VoxOne snapshots base source, station and playback intent, takes PlayMedia
ownership, detects completion/error/timeout, releases PlayMedia and restores
the selected base source. Logical volume is shared globally: a user change
during PLAY_MEDIA remains in force after restore.

PLAY_MEDIA is represented through the standard yoRadio status payload with
`name="Player Media"`. It has no separate MQTT source or override topic and
never becomes a station ID or permanently replaces base ownership.

### PLAY_MEDIA volume policy

VoxOne internal logical volume is exclusively 0..100. volp and volm mean
logical +1 and -1, clamped to that range. There is no internal 0..254 model and
step commands use logical integer units only.

Stored future configuration:

- play_media.volume_mode=CURRENT uses current logical volume.
- play_media.volume_mode=FIXED uses play_media.fixed_volume in logical range 0..100.

The first runtime stage always uses the current shared logical volume.
`FIXED` and `fixed_volume` remain stored/validated but are not applied.
Volume changes during PLAY_MEDIA are intentionally not rolled back.
Legacy absolute vol x and published volume remain 0..254 only at the future
YoRadioCompatService boundary; this must not leak into VoxOne state.

### Temporary override lifecycle and failures

request -> snapshot base source/playback intent -> suspend/release producer
-> acquire PlayMedia -> play
-> completion/error/timeout/cancel -> cleanup -> restore source

For BT: suspend A2DP safely, release I2S, play media, then resume BT. For RADIO:
save station/URL and state, stop stream, play media, then restore radio. For STOP:
restore STOP. Errors and timeouts must clean up PlayMedia and restore
deterministically; they must never leave AudioOutputManager or I2S blocked.
BT changes during PLAY_MEDIA need a deterministic policy.

## Volume compatibility correction

Legacy yoRadio wire scale remains 0..254 for absolute vol x and published
volume. This is only a boundary representation. VoxOne logical scale remains
0..100; volp and volm are logical +/-1 with ordinary clamping.
Absolute conversion happens only in the future YoRadioCompatService.

## PLAY_MEDIA validation status

Build validation covers the first direct-HTTP MP3 runtime stage. Hardware/HA
acceptance still needs RADIO playing/paused restore, STOP restore, replacement
URLs, URL/Wi-Fi/decoder failures, volume persistence and repeated requests
without heap leaks. BT restore remains limited by the intentionally unwired
VoxOneBT PCM input in MAIN.

## Runtime configuration boundary - planned

The yoRadio adapter is enabled only when features.yoradio_ws_enabled is true.
WWW configuration is staged and applied only after ZAPISZ validates the complete
snapshot, writes all NVS values, sends the restart response, waits briefly and
restarts. There is no hot reload.

VoxOne logical volume remains 0..100. Absolute yoRadio 0..254 conversion is
performed only at the compatibility boundary. volp and volm are logical +/-1.
Feature flags can disable BT, Radio, PLAY_MEDIA, Display, Encoder, Buttons and
MQTT. The yoRadio WS flag remains stored-only. The historical HA Discovery flag
is stored/deprecated and ignored at runtime. Disabled runtime modules do not
initialize, reconnect or reserve resources. The complete field contract is in
CONFIG_SCHEMA.md.
