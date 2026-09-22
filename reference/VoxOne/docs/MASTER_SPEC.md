# VoxOne — MASTER SPEC

## Current milestone summary

StationStore v2 is the single ordered station source for RadioService,
RadioList, next/previous, the WWW Stations tab, MQTT/yoRadio selection and
`/data/playlist.csv`. It persists a version-1 CRC-protected NVS blob with
stable internal IDs. The factory list remains Groove Salad, Drone Zone and
Secret Agent; users may add, edit, remove and reorder up to 16 stations.
yoRadio `play N` and status use one-based list positions while runtime radio
routing keeps stable IDs. `volumeTrim` is persisted and exported but remains
stored-only; this milestone does not add a new gain stage.

The current runtime includes RadioService/StationStore, HTTP/HTTPS MP3/Helix
with bounded redirect handling,
ICY metadata, RadioList placeholder navigation, always-on WWW, five Wi-Fi
profiles with AP fallback, yoRadio-only MQTT command/status/volume/playlist,
and PLAY_MEDIA/TTS override handling. MAIN uses BtLink and
RemoteBluetoothBackend; local Bluetooth was removed and VoxOneBT is separate.
The base source model is RADIO / BT / STOP. Double-click and logical BT
CONNECTED/DISCONNECTED switching select the new source stopped. Automatic BT
switching remains hardware-test-pending. A one-shot 1500 ms guard handles the
duplicate yoRadio stop after PLAY_MEDIA restore.

The accepted MP3/audio baseline is: 8192-byte compressed input buffer,
4096-byte maximum network read, 4608-byte maximum decoded PCM write,
I2S DMA 8 x 512 frames and maximum shared gain Q15=16384
(0.5 amplitude, about -6.02 dB). DisplayService renders only from its
FreeRTOS DisplayTask pinned to core 0; App submits coalesced state snapshots.

Status: baza po zakończeniu M2.2 i wdrożeniu fundamentu konfiguracji runtime
Aktualna wersja firmware: 0.4.0

**Bieżący stan DESK i decyzje przyszłej architektury są na końcu dokumentu,
w sekcji „Aktualny stan DESK”. Wcześniejsze sekcje M1–M3 są historycznymi
checkpointami i nie opisują kompletnego obecnego runtime.**

## Aktualizacje i partycje

VoxOne nie obsługuje OTA. Firmware aktualizuje się przez USB/serial.
Jeden slot aplikacji factory zajmuje 0x10000 .. 0x3EFFFF i ma 4 063 232 B.
NVS i coredump zachowują dotychczasowe offsety i rozmiary; brak otadata.
Zmiana tabeli wymaga pierwszego wgrania przewodowego bootloadera, tabeli i aplikacji.
Procedura: [układ partycji](../docs_FINAL_PARTITIONS.md).
Wzmianki o OTA w zakończonych milestone'ach opisują wcześniejsze wersje.

Wersjonowanie firmware stosuje Semantic Versioning MAJOR.MINOR.PATCH: 0.x.y oznacza
okres przed stabilnym 1.0.0, PATCH oznacza poprawki błędów, MINOR nowe funkcje, a
MAJOR niekompatybilne zmiany architektury lub API. M1, M2.1 i M2.2 są historycznymi
nazwami milestone'ów.

## Cel

## Current dual-MCU migration checkpoint

MAIN is being migrated to a network/radio/display controller without local
Bluetooth Classic. Optional VoxOneBT firmware owns A2DP/AVRCP on a separate
classic ESP32. `features.bluetooth_enabled` on MAIN controls an external UART
link, not local A2DP. Absent keys default to OFF; a stored `true` remains
`true`. Schema v8 is unchanged. No module is needed for RADIO or WWW.
MAIN now starts WWW on every boot. It prefers STA/LAN, starts AP immediately
without enabled Wi-Fi profiles, and starts AP fallback after about 10 s
without LAN. Encoder hold no longer selects a CONFIG boot mode.
Until PCM RX is wired, BT audio is unavailable and must not claim MAIN I2S.
Older single-MCU BT descriptions below are historical checkpoints. The old
MAIN `BluetoothService`, local lifecycle and ESP32-A2DP dependency
have now been removed. VoxOneBT retains its own A2DP dependency. The MAIN
size comparison and hardware validation are reported separately.

The current VoxOneBT contract is UART 115200 8N1, protocol v1 status and
commands (`READY`, connection/playback states, device and metadata,
`SAMPLE_RATE`, `VOLUME`, transport controls and `SET_VOLUME`).
Its audio side is Philips I2S master TX with signed 16-bit stereo PCM at the
negotiated native rate; future MAIN is I2S slave RX. This cleanup does not
implement that PCM integration.

## Source model and temporary audio override

Normal VoxOne base states are RADIO, BLUETOOTH and STOP. PLAY_MEDIA is a
temporary highest-priority override and physical third audio owner:

```text
RADIO -> PLAY_MEDIA -> RADIO
BT    -> PLAY_MEDIA -> BT
STOP  -> PLAY_MEDIA -> STOP
```

PLAY_MEDIA may play TTS, MP3, notification sounds,
HA media or a supported audio URL. Home Assistant owns the queue of requests;
VoxOne snapshots the base source and logical volume, acquires
PlayMedia, applies policy, detects completion/error/timeout, cleans up and
restores the saved base source. HA must not guess duration or perform its own
pause/wait/resume sequence. PLAY_MEDIA is a system service and is not exposed
as an enabled switch or a manually selectable source.

The two user source flags create four supported variants:

| Radio | External VoxOneBT | Result |
|---|---|---|
| OFF | OFF | TTS / PLAY_MEDIA only; base remains STOP |
| ON | OFF | TTS + Radio |
| OFF | ON | TTS + Bluetooth |
| ON | ON | TTS + Radio + Bluetooth |

STOP means no base playback; it is not a third configurable source.

Stored future play_media.volume_mode is CURRENT or FIXED. CURRENT uses current logical volume;
FIXED uses play_media.fixed_volume in logical range 0..100 and respects the physical limit.
The previous volume is always restored. Internal logical volume is exclusively
0..100; yoRadio 0..254 conversion belongs only at the future compatibility
boundary. volp/volm remain logical +/-1.

The lifecycle is snapshot -> suspend/release -> acquire PlayMedia -> play ->
completion/error/timeout -> cleanup -> restore. BT requires A2DP suspend, I2S
release and resume; RADIO saves station/URL and playback state; STOP remains
STOP. Failures must never leave PlayMedia or I2S locked.
## Minimal MP3 RadioService checkpoint

## Runtime configuration model - planned

VoxOne uses one firmware and a runtime configuration. WWW edits are staged;
only ZAPISZ validates the complete snapshot, writes all values to NVS, sends a
restart response, waits briefly for HTTP delivery and calls ESP.restart().
There is no hot reload and invalid configuration produces no partial NVS write.

The WWW exposes `bluetooth_enabled` and `radio_enabled` as the only source
switches. `play_media_enabled` is retained only for schema/NVS compatibility
and no longer gates runtime. Buttons and yoRadio WS flags are stored-only and
hidden until their runtime exists. Native HA Discovery was removed; its legacy
flag is hidden, stored-only and ignored because the target integration uses
`ha_yoradio`. `web_enabled` is a
schema-7 stored/deprecated field with no runtime effect and is hidden from WWW.
MAIN always starts Wi-Fi and WebService in one normal mode,
alongside enabled audio services. Encoder hold does not select a boot mode.
Disabled optional modules do not initialize or reserve I2S/GPIO resources.

Wi-Fi schema 8 supports five saved profiles (SSID, secret password, enabled,
priority 0..100) and a persistent last-good index. MAIN tries last-good
first, then asynchronously scans known enabled SSIDs after failure. With no
enabled profile, AP starts immediately. With profiles but no LAN after about
10 seconds, AP fallback starts while STA retry continues. Once AP starts it
stays on until restart, even after a late LAN connection. WWW listens on
both interfaces; mDNS advertises HTTP when STA has an address.

The full field contract, defaults, visibility, validation and restart requirement
is in CONFIG_SCHEMA.md. Runtime profiles are combinations of these flags, not
separate firmware variants. Logical volume remains 0..100; yoRadio 0..254 is
only a compatibility-boundary representation. Every persistent save requires
restart.

Minimal RadioService is code-complete for measurement and hardware testing.
It uses Helix MP3, HTTP/HTTPS streams and the existing AudioOutputManager.
HTTPS currently uses `NetworkClientSecure::setInsecure()`: transport is
encrypted, but the server certificate and hostname are not authenticated.
Bluetooth and Radio never write I2S/PCM5102A concurrently; switching uses BT
suspend/resume and the shared ownership manager.

Helix MP3 build/link is confirmed and AAC is not linked. Hardware audio,
the new partition table and runtime BT -> Radio -> BT remain pending.
ICY metadata, stream reconnect, station list and AAC are not implemented.

VoxOne to autonomiczny moduł audio na klasycznym ESP32:
- radio internetowe,
- Bluetooth A2DP,
- `play_media` from Home Assistant, including TTS as one use case,
- WWW,
- yoRadio-compatible MQTT for `ha_yoradio`,
- kompatybilny WebSocket yoRadio,
- pełna praca także bez HA i MQTT.

## Potwierdzony sprzęt referencyjny

Profil: `yoradio-esp32u-st7789-76-pcm5102a`

- ESP32-D0WD-V3 rev. 3.1
- flash 4 MB
- TFT ST7789 284×76
- PCM5102A
- enkoder GPIO35 / GPIO33 / GPIO32
- `ENCODER_DIRECTION = -1`
- I2S DOUT 27 / BCLK 26 / WS 25
- TFT SCK 18 / MOSI 23 / CS 5 / DC 4 / RST -1
- TFT init 76×284, rotation 1

## Architektura

- `StateStore` — jedno źródło prawdy.
- `CommandQueue` — centralna kolejka komend.
- display tylko obserwuje stan.
- input tylko generuje komendy.
- GPIO tylko w `BoardConfig`.
- Core niezależny od konkretnego sprzętu.
- moduły pracują nieblokująco.

## Priorytety źródeł

1. PLAY_MEDIA
2. Bluetooth po połączeniu telefonu
3. Radio
4. STOP

## M1 zakończone

- TFT
- enkoder
- PCM5102A
- Wi-Fi
- AP konfiguracji
- WWW
- mDNS
- OTA przez WWW

## M2.2 zakończone sprzętowo

Wersja: 0.2.0

## 0.2.1 — fundament konfiguracji runtime

Wersja 0.2.1 wprowadza ConfigModel, ConfigManager oraz schemaVersion 1.
Zachowano istniejące klucze NVS, a migracja istniejącej konfiguracji została
przetestowana sprzętowo.

## 0.3.0 — display i zegar

Wersja 0.3.0 obejmuje przebudowę ekranu ST7789 284×76 oraz TimeService
z configTzTime() dla Europe/Warsaw. Ekran playera, ekran głośności i NTP
zostały przetestowane sprzętowo.

- Bluetooth A2DP przez PCM5102A
- AVRCP Play/Pause/Next/Previous
- dwukierunkowa synchronizacja głośności
- metadata artist/title oraz peer/device info, jeśli dostępne
- równoległa praca Bluetooth, Wi-Fi, WWW i OTA
- TFT z BT status/metadata bez migotania

## 0.4.0 — Bluetooth ownership i reconnect grace`r`n`r`nApp zarządza Bluetooth ownership. Po utracie połączenia działa 10-sekundowy grace period z próbami reconnectu do ostatniego urządzenia; ekran pokazuje BT RECONNECT, a metadata jest czyszczona dopiero po wygaśnięciu okna. Config schema 3 i migracja 1 → 2 → 3 zachowują istniejącą konfigurację. Zakres został przetestowany sprzętowo.`r`n`r`n## Następny milestone

M3 — Radio.

## Audio output ownership checkpoint — 0.4.0

`AudioOutputManager` jest jedynym właścicielem fizycznego I2S/PCM5102A.
BluetoothService ma przygotowany lifecycle `suspend/resume`; czasowe zatrzymanie
A2DP używa `end(false)`, a `end(true)` nie jest używane. Reconnect grace 10 s
pozostaje osobną ścieżką bez teardown A2DP.

Jest to kodowy fundament pod przyszły RadioService, nie implementacja radia ani
wersji 0.5.0. Runtime suspend/resume oraz test sprzętowy nowej tabeli partycji
pozostają jeszcze niewykonane.

## Aktualny stan DESK — nadrzędny wobec historycznych checkpointów

VoxOne DESK/BIURKO na klasycznym ESP32U jest jedynym aktywnie rozwijanym
firmware. Sprzęt: ST7789 284×76, PCM5102A, enkoder, Wi-Fi i Radio
HTTP/HTTPS MP3. Lokalny Bluetooth Classic został usunięty z MAIN;
`voxone`/`voxone_debug`
pozostają środowiskami DESK. Wersja firmware w kodzie pozostaje 0.4.0;
numer nie oznacza, że wszystkie opisane niżej funkcje są stabilne.
Aktualizacja tylko USB/serial, bez OTA.

### Bieżący kod i historyczne testy sprzętowe

- ST7789 Sony Dark: stacja, artysta/utwór, zegar, RSSI, status, ikona głośnika,
  polskie glify i scroller z separatorem ` | `. Stacja wykorzystuje szerokość
  ekranu; artist/title mają viewport przed panelem zegara/RSSI. Działają
  Volume overlay; BT NAV pozostaje w UI, ale BT audio jest niedostępne
  do czasu podłączenia PCM RX. RadioList istnieje tylko jako placeholder.
- Enkoder: obrót zmienia głośność w HOME/VOLUME, w BT NAV steruje
  NEXT/PREVIOUS; krótki klik w HOME steruje play/pause BT lub stop RADIO,
  a w overlay wraca HOME. Długi przytrzymany klik w HOME otwiera BT NAV,
  gdy źródłem jest BT. Dwuklik source switch to TODO.
- Lokalny A2DP/AVRCP działał w historycznych testach DESK, ale nie należy
  już do MAIN. Osobny VoxOneBT nie był jeszcze testowany sprzętowo.
  MAIN zachowuje wspólny gain PCM 0..100 dla RADIO i przyszłego BT PCM;
  AVRCP jest granicą protokołu na module zewnętrznym.
- RADIO: StationStore v2 ma dynamiczną listę NVS z trzema factory defaults;
  RadioService obsługuje HTTP/HTTPS MP3/Helix, do 5 redirectów
  301/302/303/307/308 (także HTTP↔HTTPS i względne `Location`), wybór stacji,
  stop, ownership I2S,
  WaitingForNetwork, anulowanie retry przez generation oraz backoff
  2/5/10/30 s. Test sprzętowy potwierdził odtwarzanie radia i powrót po
  zwolnieniu RAM przez wariant B. Parser ICY name/br/metaint/StreamTitle
  jest teraz w kodzie, ale wymaga testu sprzętowego z rzeczywistymi
  stacjami. Akceptowane końcowe typy to `audio/mpeg`, `audio/mp3` i
  `application/octet-stream`; AAC i parsery M3U/PLS nie działają.
- Boot ma jeden normalny tryb: WWW i Wi-Fi startują zawsze razem z aktywnymi
  usługami audio. Brak aktywnego profilu uruchamia AP natychmiast; przy
  profilach bez LAN przez około 10 s uruchamia się AP fallback. Późny LAN
  nie wyłącza AP w tej sesji. Timeout jest nieblokujący; przytrzymanie
  enkodera nie zmienia trybu boot.
- Schema v8 przechowuje pięć profili Wi-Fi i `last_good`. WiFiService ma
  state machine, retry/backoff, skanowanie, akceptację późnego
  `WL_CONNECTED` i osobną politykę
  sieciową dla wybranego RADIO (cap odstępu około 10 s; przy `NO_AP_FOUND`
  3/5/10 s). Po reason 208 odracza własny retry, gdy Arduino/STA może
  jeszcze asocjować, zamiast ślepo ponawiać `WiFi.begin()`. Po reconnect
  odświeża RSSI/mDNS; App uruchamia hook NTP.
- Source intent `STOP/RADIO/BT` jest obecnie w App, nie w osobnym
  SourceManager. Default source z NVS ustawia początek sesji; późniejsze
  `source bt/radio/stop` nie wymagają zapisu configu. Żądanie BT jest
  obecnie odrzucane bez dzierżawy I2S, dopóki MAIN nie ma
  aktywnego wejścia PCM RX. Zapisane
  `radio.defaultStation` służy do wyboru tylko poprawnego ID, natomiast
  `radio.autostart` nie jest jeszcze odrębną polityką runtime: start wynika
  z `audio.defaultSource=RADIO` i poprawnej stacji.

### Historyczne eksperymenty jednoukładowego BT

- Lifecycle BT A (stop profilu, pozostawienie Bluedroid/controllera) jest
  odrzucony: Helix nie mieścił się w RAM. B (stop A2DP/AVRCP, deinit
  Bluedroid, controller retained) był kandydatem testowym: Helix działał,
  bonding pozostaje, telefon może ponownie połączyć się bez pairingu.
  Wymuszone `connect_to(last BDA)` po source switch jest zawodnym
  outbound reconnect; telefon-initiated reconnect działa. C (pełny stop
  controllera) zwalnia RAM, ale reconnect jest problematyczny.
- `host_recv_pkt_cb hci_hal_h4.c:580 (0)` to assert po nieudanej alokacji
  bufora przychodzącego pakietu HCI. Wystąpił także podczas pierwszej sesji
  BT po boot. Jego przyczyna źródłowa i stan heap bezpośrednio przed
  awarią nie zostały potwierdzone. Lokalna diagnostyka HCI została usunięta
  z MAIN; dalszy test stosu BT dotyczy osobnego VoxOneBT.
- Sprzętowe testy pojedynczych ścieżek BT i RADIO nie są równoznaczne
  z zaliczeniem wielokrotnych cykli BT/RADIO, pełnego cold-boot recovery
  czy przyszłego PLAY_MEDIA.

### Model docelowy wspólny dla DESK/DIN/SALON

`baseSource = STOP | RADIO | BT` oraz osobny
`temporaryOverride = NONE | PLAY_MEDIA`. PLAY_MEDIA jest chwilowym
override, nie pozycją ręcznego cyklu. RADIO -> PLAY_MEDIA -> ta sama stacja;
BT -> PLAY_MEDIA -> BT; STOP -> PLAY_MEDIA -> STOP. VoxOne zapisuje bazę,
wybraną stację i logical volume, odtwarza media, sprząta i przywraca stan
także po błędzie/timeout; HA prowadzi kolejkę. Runtime override URL jest
zaimplementowany; kolejka pozostaje po stronie HA. Powrót BT ma odbywać się bez ręcznego parowania, wyboru źródła i
restartu — to wymaganie do spełnienia, nie obecna gwarancja wariantu B.

Jedna przyszła ścieżka `AudioRouter -> common volume -> I2S TX -> DAC`
przyjmuje RADIO PCM, BT PCM (lokalny lub I2S RX) i PLAY_MEDIA PCM.
Zewnętrzny VoxOneBT na klasycznym ESP32U obsłuży BT Classic/A2DP/AVRCP,
metadata i komendy UART, ale nie Wi-Fi/WWW/HA/radia/display. Telefon może
pozostać połączony podczas PLAY_MEDIA: Main wysyła PAUSE, po media PLAY,
jeśli BT wcześniej grał.
Nie implementować dwóch końcowych DAC. BT CONNECTED samo nie musi zmienić
bazy; przyszłe `bluetooth.auto_switch_on_play` (proponowany default `true`)
może przełączyć ją przy BT PLAYING. Przy aktywnym override zmiana czeka.

Plan profili: DESK = obecny ESP32U/ST7789/PCM5102A; DIN = ESP32-S3 Zero,
SSD1306 128×32/128×64 lub NoDisplay; SALON = ESP32-S3 N16R8, ST7796S
3.5", planowany TDA7719. ESP32-S3 nie zapewnia Bluetooth Classic, więc
DIN/SALON z BT potrzebują osobnego ESP32U. Wspólny core bez forków,
planowany `AudioProcessor` z implementacjami NoAudioProcessor dla
DESK/DIN i TDA7719Processor dla SALON. Szczegóły i status granic:
[ARCHITECTURE](ARCHITECTURE.md), [HARDWARE_PROFILES](HARDWARE_PROFILES.md),
[AUDIO_ARCHITECTURE](AUDIO_ARCHITECTURE.md) oraz
[BLUETOOTH_ARCHITECTURE](BLUETOOTH_ARCHITECTURE.md).

Ręczny dwuklik ma w przyszłości przełączać RADIO -> BT -> RADIO, bez
PLAY_MEDIA w cyklu. Na pustym ekranie stacji docelowe etykiety to „Radio”,
„Bluetooth” i podczas override „Player Media”. Obecny renderer nie musi ich
jeszcze pokazywać. Nie implementować teraz DIN/SALON, VoxOneBT, I2S RX,
UART ani TDA7719.
