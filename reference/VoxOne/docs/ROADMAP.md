# ROADMAP

## Current milestone checkpoint

- [x] RadioService and StationStore with HTTP MP3/Helix, ICY metadata and
  RadioList placeholder
- [x] WWW always-on, five Wi-Fi profiles and AP fallback
- [x] yoRadio-only MQTT command/status/volume/playlist compatibility
- [x] PLAY_MEDIA/TTS override and one-shot duplicate-stop guard
- [x] RADIO/BT/STOP source model and double-click source selection
- [x] Logical BT CONNECTED/DISCONNECTED source switching
- [x] Stabilize MP3 128/256/320 kbps with DisplayTask core 0, network reads
  up to 4096 bytes, PCM writes up to 4608 bytes, I2S DMA 8 x 512 and
  maximum gain Q15=16384 (about -6.02 dB)
- [ ] Hardware validation with a connected VoxOneBT module

## Current post-diagnostic baseline

The values above are the retained runtime architecture, not temporary A/B
switches. Keep the 8192-byte MP3 input buffer, 4096-byte network-read cap,
4608-byte RadioService PCM-write cap, I2S DMA 8 x 512 and shared
Q15=16384 maximum gain unless a separate measured change is requested.
DisplayTask remains the sole ST7789/SPI owner on core 0.

MQTT remains the narrow yoRadio/ha_yoradio adapter only:
command, status, volume and retained playlist. Native MQTT extensions and
native HA Discovery are intentionally absent. The next audio tasks are
long-duration regression and the separate MAIN-to-VoxOneBT PCM integration;
AAC/AAC+/M3U/PLS/FLAC/Opus/Vorbis are separate future work.

## Current external-BT migration

- [x] Single MAIN runtime: WWW always on, STA preferred, AP immediate with
  no enabled profile or fallback after about 10 s; no encoder boot hold.

- [x] Independent VoxOneBT firmware and UART v1 control/status prototype.
- [x] MAIN remote UART backend with Disabled/Waiting/Ready/Unavailable;
  no-module RADIO/WWW operation and safe BT rejection without PCM RX.
- [x] Remove legacy local Bluetooth sources, coexistence/HCI diagnostics and
  ESP32-A2DP dependency from MAIN; keep A2DP only in VoxOneBT.
- [ ] Wire and test MAIN I2S RX slave, bounded PCM buffering, routing and
  sample-rate changes before enabling BT as an audio source.
- [ ] Hardware-test UART, pairing, metadata, volume, source switching,
  missing module and long-run HCI stability. No second board is available.

Older single-MCU BT milestones below are historical checkpoints.

## Aktualny plan — DESK priorytet (zastępuje poniższe historyczne checklisty)

Poniższe sekcje M1–M6 zachowano jako checkpointy historyczne. Ich puste pola
nie są automatycznie aktualnym statusem; obecny stan jest w
[MASTER_SPEC](MASTER_SPEC.md). Nie wykonywać teraz migracji na ESP32-S3.

| Obszar | Status dziś | Najbliższa praca / warunek ukończenia |
|---|---|---|
| DESK boot/config | jeden tryb: WWW stale, STA preferowane, AP natychmiast bez profili lub fallback po ~10 s | regresja LAN/AP+STA/RADIO na sprzęcie |
| Wi-Fi | schema v8, 5 profili, last_good, retry, reason 208, late connect, RADIO/BT policy | stress cold boot i router loss/recovery |
| LCD/encoder | Sony Dark, polskie glify, scroller ` | `, Volume, BT NAV | zachować layout; RadioList i dwuklik osobno |
| Radio | trzy testowe stacje, HTTP MP3/Helix, retry, Wi-Fi recovery; gra na sprzęcie; parser ICY w kodzie | sprzętowy test ICY, potem lista stacji i trwała playlista |
| BT | A2DP/AVRCP, metadata, initial volume sync; wariant B pozwala na RADIO i powrót BT po połączeniu telefonu | ustalić przyczynę HCI allocation assert i wykonać wielokrotne cykle |
| Audio | wspólny gain 0..100, krzywa `pow(v/100,2.2)`, jedno I2S TX | uznane; bez powrotu do oddzielnych gain |
| Integracje | yoRadio MQTT implemented; yoRadio WS stored/planned | validate `ha_yoradio` on hardware |
| PLAY_MEDIA | model i NVS, bez runtime | override/restore BT, Radio, Stop i volume; test błędów |
| Sprzęt przyszły | DIN/SALON/VoxOneBT są projektem | osobne etapy, bez zmian działającego DESK |

Kolejność małych etapów DESK:

1. Ustabilizować BT/RADIO: zebrać heap internal 8-bit przed HCI assert,
   potwierdzić przyczynę, testować cold boot, utratę Wi-Fi, bonding i
   powtarzalne przełączenia B; nie ogłaszać sukcesu przed testem sprzętowym.
2. Sprawdzić sprzętowo ICY metadata dla radia (station/artist/title do
   StateStore/HOME); parser jest w kodzie, niepotwierdzony na sprzęcie.
3. Lista stacji, trwałe przechowywanie i RadioList; obecne trzy rekordy
   pozostają testowymi danymi, nie docelową playlistą.
4. yoRadio MQTT for `ha_yoradio` implemented; hardware validation remains.
5. Native HA Discovery removed; Home Assistant uses `ha_yoradio`.
6. PLAY_MEDIA jako czasowy override z timeout/error cleanup i powrotem
   do bazy oraz logical volume; minimalny arbitraż źródeł może powstać
   w tym kroku bez wielkiego refaktoru.
7. Wydzielić SourceManager/TemporaryOverrideManager dopiero po
   sprawdzonym przepływie i testach regresji.
8. Dwuklik enkodera RADIO -> BT -> RADIO; nie dodawać PLAY_MEDIA do cyklu.
9. Jeśli ESP32U nadal ma problem RAM/HCI albo powrót BT wymaga ręcznej
   interwencji, prototyp osobnego VoxOneBT i I2S RX/UART, dopiero po
   ustaleniu kontraktu i budżetu pamięci.

Otwarte TODO zachowane: długie teksty i dalsze dopracowanie LCD poza obecnym
layoutem; RadioList; dwuklik source switch; test ICY, redirecty/playlisty/AAC;
MQTT hardware validation and PlayMedia; BT auto-switch on PLAY; SSD1306, ST7796S,
NoDisplay, MAX98357A, TDA7719; brightness/screensaver, encoder acceleration,
docelowe etykiety bez odtwarzania; board profiles i nowe env. Statusy są w
[ARCHITECTURE](ARCHITECTURE.md), [HARDWARE_PROFILES](HARDWARE_PROFILES.md),
[AUDIO_ARCHITECTURE](AUDIO_ARCHITECTURE.md) i
[BLUETOOTH_ARCHITECTURE](BLUETOOTH_ARCHITECTURE.md).

## Minimal MP3 RadioService checkpoint

- [x] code RadioService with HTTP MP3 and Helix MP3
- [x] start, stop, loop and output through AudioOutputManager
- [x] controlled BT suspend/resume with exclusive I2S ownership
- [x] Helix MP3 retained in ELF; AAC is not linked
- [ ] hardware audio test
- [ ] new partition-table test
- [ ] runtime BT -> Radio -> BT
- [ ] ICY metadata, stream reconnect and station list

This checkpoint does not include full radio implementation or firmware 0.5.0.

## Aktualizacje — decyzja projektowa

VoxOne nie obsługuje OTA ani rollbacku OTA. Firmware aktualizuje się przez USB/serial.
Jeden duży slot aplikacji factory: 0x3E0000 = 4 063 232 B.
Zmiana tabeli partycji wymaga pierwszego wgrania przewodowego.
Wzmianki o OTA w zakończonych milestone'ach są historyczne.

## 0.3.0 — display i NTP ✅

ST7789 284×76 oraz zegar NTP Europe/Warsaw zostały przetestowane sprzętowo.

## 0.4.0 — Bluetooth ownership i reconnect grace ✓`r`n- App zarządza ownership; grace period 10 s i reconnect ostatniego urządzenia.`r`n- Config schema 3 z migracją zachowującą istniejącą konfigurację.`r`n- Przetestowane sprzętowo.`r`n`r`n## M1 — FOUNDATION ✅
- BoardConfig
- StateStore
- CommandQueue
- TFT
- enkoder
- PCM5102A
- Wi-Fi
- AP setup
- WWW
- mDNS
- OTA

## M2 — BLUETOOTH

### M2.1 — A2DP baseline ✅
- A2DP Sink
- PCM5102A audio
- coexistence BT + Wi-Fi
- WWW and OTA

### M2.2 — AVRCP and metadata ✅
- AVRCP Play/Pause/Next/Previous
- metadata artist/title
- peer/device info when available
- bidirectional volume sync
- TFT BT status and metadata

M2.2 is hardware-tested and accepted on firmware 0.2.0.

### 0.2.1 — runtime configuration foundation

Wewnętrzny fundament konfiguracji runtime: ConfigModel, ConfigManager,
schemaVersion 1 oraz sprzętowo przetestowana migracja istniejącej konfiguracji NVS.

### Remaining M2 verification
- pairing management
- reconnect stress test

## M3 — RADIO
- HTTP/HTTPS stream
- playlist
- retry/reconnect
- codecs
- Next/Prev

## 0.4.0 — audio output ownership checkpoint

- [x] wspólny `AudioOutputManager` i wyłączny ownership fizycznego I2S
- [x] lifecycle Bluetooth `suspend/resume` przygotowany pod przyszłe źródła
- [x] czasowe zatrzymanie A2DP przez `end(false)`; `end(true)` nieużywane
- [x] reconnect grace 10 s pozostaje bez zmian
- [ ] sprzętowy test runtime suspend/resume
- [ ] sprzętowy test nowej tabeli partycji

To przygotowanie kodowe nie jest implementacją radia ani wersji 0.5.0.

## M4 — SOURCE MANAGER
- RADIO ↔ BT
- temporary PLAY_MEDIA override over RADIO, BT or STOP
- restore previous base source and logical volume
- technical mute
- fade transitions

PLAY_MEDIA is a physical third audio owner but remains a temporary override,
not a normal user source. It can carry TTS or ordinary media. Home Assistant
provides the request queue; VoxOne provides the PlayMedia lifecycle. Planned;
requires hardware validation.

## M5 — PROTOCOLS
- yoRadio `/ws`
- yoRadio MQTT for `ha_yoradio`

## M6 — FINALIZATION
- final WWW
- storage migrations
- backup/restore
- safe mode
- diagnostics
- przewodowe recovery firmware przez USB/serial
## VoxOneBT parallel prototype milestone

- [x] Separate `VoxOneBT/` build: A2DP Sink/AVRCP, I2S TX, UART v1.
- [x] Main-side remote `BtLink` adapter and unwired AudioRouter API.
- [ ] Confirm exact Wemos D1 mini ESP32 PCB/pin exposure and voltage wiring.
- [ ] Hardware-test BT pairing, AVRCP, metadata, volume, rate and long-run HCI.
- [ ] Design Main spare pins, I2S RX slave, bounded PCM buffers and clocking.
- [ ] Route remote PCM through common gain/I2S TX and test dropout recovery.
- [x] Wire App to RemoteBluetoothBackend; local fallback removed.
- [ ] Validate RADIO/BT/PLAY_MEDIA source switches while phone stays connected.
- [x] Retire local BluetoothService/lifecycle/coexistence diagnostics from MAIN.
- [x] WWW always on with automatic AP fallback; hardware regression remains.

Build-only status is not hardware acceptance. MAIN radio/audio remain active;
BT stays unavailable as an audio source until the separate PCM RX path is wired.
