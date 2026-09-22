# ARCHITECTURE

## Current milestone (authoritative)

MAIN now includes RadioService and StationStore with HTTP/HTTPS MP3/Helix and
bounded redirects,
ICY metadata parsing and a RadioList placeholder. WWW is always on; Wi-Fi has
five profiles and AP fallback. MQTT is intentionally limited to the yoRadio
command, status, volume and retained playlist contract used by ha_yoradio;
native MQTT extensions and native HA Discovery are not active.
PLAY_MEDIA/TTS is the highest-priority
temporary override. The base source model is RADIO / BT / STOP.

DisplayService owns ST7789 exclusively from a pinned FreeRTOS DisplayTask on
core 0 (priority 2, 4096-byte stack). App only submits coalesced snapshots;
it does not perform LCD SPI rendering. The radio pipeline uses an 8192-byte
MP3 input buffer, maximum network read 4096 bytes, maximum PCM write 4608
bytes, I2S DMA 8 x 512 frames and maximum shared gain Q15=16384
(about -6.02 dB).

Local Bluetooth was removed from MAIN. BT is represented by BtLink and
RemoteBluetoothBackend; VoxOneBT is a separate firmware. Double-click and
BT CONNECTED/DISCONNECTED source switching select the new source stopped.
Automatic BT switching is logically implemented and hardware-test-pending.
The legacy yoRadio duplicate-stop guard is one stop for 1500 ms after restore.

## Current migration status

`App` uses `RemoteBluetoothBackend` over UART2 when external
BT is enabled. The module state is Disabled, Waiting, Ready or Unavailable.
RADIO and WWW do not require VoxOneBT. BT PCM input and routing are still
unwired, so BT cannot be selected as an audio source yet. Local Bluetooth
source files and ESP32-A2DP dependency have been removed from MAIN. The
single-MCU description below is historical, not the current `App` call path.

MAIN has a single runtime mode. Wi-Fi and WebService start on every boot.
No enabled profile starts AP immediately; otherwise STA is preferred, with
AP fallback after about 10 s without LAN. AP remains up after late STA
recovery. mDNS advertises HTTP on LAN. The encoder has no boot-mode hold.

Status: DESK działa na jednym klasycznym ESP32U. Poniższe granice dla DIN,
SALON i VoxOneBT są projektem, nie gotową implementacją. Aktualny stan i
odstępstwa od starszych checkpointów określa [MASTER_SPEC](MASTER_SPEC.md).

## Historyczny opis DESK sprzed migracji WWW/BT

`App` koordynuje boot, source intent i arbitraż źródeł; `StateStore` publikuje
stan obserwowany przez UI, a `CommandQueue` przenosi komendy wejściowe.
`AudioOutputManager` posiada jeden I2S TX/PCM5102A, wyłączną dzierżawę
Bluetooth/Radio i wspólny gain PCM. `BluetoothService` obsługuje A2DP/AVRCP,
`RadioService` HTTP/HTTPS MP3/Helix, `StationStore` dynamiczną listę NVS, a
`WiFiService` osobny automat STA/AP. `DisplayService` jest obecnie konkretnym
rendererem ST7789 284×76, nie abstrakcją wielu ekranów.

Historyczny tryb CONFIG został usunięty. Aktualny flow jednego trybu opisano
powyżej; szczegóły schema v8: [CONFIG_SCHEMA](CONFIG_SCHEMA.md).

## Docelowy wspólny core

Proponowane granice katalogów; nie przenosić działającego kodu tylko po to,
aby odpowiadał temu drzewu:

| Granica | Odpowiedzialność | Dziś |
|---|---|---|
| `src/core/` | SourceManager, TemporaryOverrideManager, StateStore, CommandQueue | arbitraż i source intent w App; managerów jeszcze nie ma |
| `src/audio/` | AudioRouter, końcowy gain, I2S TX, backend DAC | AudioOutputManager i PcmGainOutput |
| `src/radio/` | RadioService, StationStore, później playlisty | HTTP/HTTPS MP3/Helix, ICY, retry i dynamiczne stacje |
| `src/network/` | WiFiService, WWW stale i przyszły WS | Wi-Fi i WWW |
| `src/mqtt/` | adapter MQTT yoRadio dla `ha_yoradio` | command/status/volume/playlist |
| `src/ui/` oraz przyszłe `src/displays/` | model widoku i renderery | ST7789 w DisplayService |
| `src/config/` | RuntimeConfig, NVS, walidacja i migracje | schema v8 |
| przyszłe `src/boards/` | piny i możliwości Desk/DIN/Salon | BoardConfig i config runtime |
| `src/btlink/` | UART control/status do VoxOneBT | tylko RemoteBluetoothBackend |
| przyszłe `src/processors/` | NoAudioProcessor/TDA7719Processor | brak |
| `src/hal/`, `src/time/`, `src/diagnostics/` | wejścia, zegar, logi | działające moduły |

Wspólny core ma podejmować decyzje o źródle, wolumenie 0..100, kolejności
override, stanie i błędach bez zależności od konkretnego GPIO, ekranu czy
topologii BT. Desk pozostaje aktywnym celem. DIN/SALON nie wymagają forka
logiki, ale potrzebują osobnej walidacji sprzętu i zasobów.

## Granice, których nie wolno pomylić

- `baseSource = STOP | RADIO | BT` jest trwałą intencją użytkownika w sesji.
  `temporaryOverride = NONE | PLAY_MEDIA` jest nakładką; PLAY_MEDIA nie jest
  kolejną pozycją ręcznego przełączania.
- Przy override zachowuje się bazowe źródło, stację/transport i logical volume,
  po czym po sukcesie, błędzie lub timeout przywraca stan bazowy. Szczegóły:
  [AUDIO_ARCHITECTURE](AUDIO_ARCHITECTURE.md). To jest **plan**, nie obecny runtime.
- BT CONNECTED jest informacją o transporcie; dopiero BT PLAYING może
  automatycznie wybrać bazę BT (przyszła polityka, z opcją
  `bluetooth.auto_switch_on_play`, domyślnie proponowane `true`).
  Podczas PLAY_MEDIA zmiana bazy czeka do końca override.
- UI tylko obserwuje stan; enkoder wysyła komendy. Aktualny BT NAV działa,
  RadioList i dwuklik RADIO/BT są TODO.
- AudioOutputManager/AudioRouter ma zawsze jeden końcowy tor PCM i jeden
  backend TX. Osobny ESP32U BT dostarcza PCM przez I2S RX Main, nie osobny DAC.
- Runtime pętli sieci/display/radia nie powinien być blokowany długim retry;
  AP fallback używa timera millis(), bez boot-hold ani osobnego trybu.

Plan profili: [HARDWARE_PROFILES](HARDWARE_PROFILES.md). Kontrakt przyszłego
VoxOneBT: [BLUETOOTH_ARCHITECTURE](BLUETOOTH_ARCHITECTURE.md).
## Parallel VoxOneBT checkpoint

`VoxOneBT/` is a separate classic-ESP32 firmware: A2DP/AVRCP, I2S PCM TX,
UART v1. MAIN `App` uses its remote BtLink adapter, while I2S RX and
the future AudioRouter remain unwired. Local Bluetooth source files and
the MAIN A2DP dependency have been removed.
Protocol: [VOXONE_BT_PROTOCOL](VOXONE_BT_PROTOCOL.md).

The external contract is UART 115200 8N1 with `PROTO 1`, `READY`,
transport/playback/metadata/sample-rate/volume events and AVRCP commands.
VoxOneBT is Philips-I2S master TX; future MAIN audio input is slave RX,
signed 16-bit stereo at the negotiated native sample rate.

WebService is active now, independently of module presence. Provisioning
uses automatic AP fallback in the same normal runtime, not a CONFIG mode.
