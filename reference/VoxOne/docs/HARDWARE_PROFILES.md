# HARDWARE PROFILES

## Current MAIN UART and proposed future PCM wiring

The current MAIN DESK firmware initializes UART2 TX GPIO17/RX GPIO16 only
when external BT is enabled in NORMAL. These pins are separate from the
existing ST7789 SPI (18/23/5/4), encoder (35/33/32), and PCM5102A I2S TX
(BCLK26/WS25/DATA27). This is code allocation, not verified physical wiring.
GPIO16/17 are unsuitable if a particular board uses them for PSRAM.
When external BT is enabled, configuration validation reserves both UART
pins against active user-configurable I2S, display and encoder assignments.

Candidate MAIN I2S RX slave pins, **not configured in firmware**:

| Signal from VoxOneBT | Candidate MAIN GPIO | Status |
|---|---:|---|
| BCLK | 21 | free in active ST7789 DESK; conflicts with stored SSD1306 SDA |
| LRCLK/WS | 22 | free in active ST7789 DESK; conflicts with stored SSD1306 SCL |
| DATA | 34 | input-only, suitable for RX data; verify header access |
| Optional module reset | 13 | proposal only; do not drive until wiring is verified |

No second board is available for validation. Do not wire I2S data to the
existing PCM5102A output or share a data line. Common GND, 3.3 V UART,
clock direction and board pin exposure require physical verification.
The older DESK local-BT statements below are historical checkpoints.

Status: tylko DESK jest obecnie sprawdzonym fizycznie celem Main.
VoxOneBT ma osobny, jeszcze nieprzetestowany firmware. DIN i SALON
to projekt profili, bez przydziału GPIO i bez nowych env PlatformIO.
Nazwy i priorytety są zgodne z [MASTER_SPEC](MASTER_SPEC.md).

## `yoradio-esp32u-st7789-76-pcm5102a`

| Funkcja | GPIO / wartość |
|---|---:|
| TFT SCK | 18 |
| TFT MOSI | 23 |
| TFT CS | 5 |
| TFT DC | 4 |
| TFT RST | -1 |
| TFT init | 76×284 |
| TFT rotation | 1 |
| Encoder A/B | 35 / 33 |
| Encoder button | 32 |
| Encoder direction | -1 |
| I2S DOUT | 27 |
| I2S BCLK | 26 |
| I2S WS | 25 |

Capabilities:
- display: tak
- encoder: tak
- buttons: nie
- amp mute: nie
- PSRAM: nie

Nowy sprzęt ma być dodawany przez nowy profil bez zmian w Core.

## Docelowe warianty

| Profil | Main i ekran | Końcowy tor / procesor | Status |
|---|---|---|---|
| VoxOne DESK | obecny klasyczny ESP32U, ST7789 284×76, enkoder | PCM5102A, NoAudioProcessor | priorytet; istniejący firmware |
| VoxOne DIN | planowany ESP32-S3 Zero, SSD1306 128×32 albo 128×64 albo NoDisplay | backend wybrany po projekcie sprzętu, NoAudioProcessor | projekt; brak drivera i pinów |
| VoxOne SALON | planowany ESP32-S3 N16R8, ST7796S 3.5" | jeden końcowy tor audio, planowany TDA7719Processor | projekt; brak drivera, pinów i DSP runtime |

ESP32-S3 nie obsługuje Bluetooth Classic/A2DP Sink. Jeżeli DIN lub SALON
mają oferować BT, wymagają osobnego klasycznego ESP32U VoxOneBT; nie należy
obiecywać lokalnego A2DP na S3. Kontrakt modułu: [BLUETOOTH_ARCHITECTURE](BLUETOOTH_ARCHITECTURE.md).
MAIN DESK nie ma już lokalnego BluetoothService. Bez modułu VoxOneBT
działają RADIO i WWW; BT audio wymaga przyszłego I2S RX w MAIN.

## Display — kontrakt layoutu, nie implementacja

| Renderer | Docelowy układ |
|---|---|
| ST7789 284×76 | zachować działający Sony Dark: stacja przez pełną szerokość, artysta/utwór kończą się przed panelem zegara/RSSI, status i głośność |
| SSD1306 128×32 | stacja i dolny pasek stanu odtwarzania / głośności / RSSI |
| SSD1306 128×64 | stacja, artysta, utwór i dolny pasek |
| ST7796S 3.5" | bogatszy widok, możliwe album art, większy zegar i kontrolki DSP |
| NoDisplay | renderer bez inicjalizacji/pollingu wyświetlacza |

Docelowy scroller przyjmuje viewport `(x, y, width, height)` od layoutu.
Obecnego ST7789 nie refaktorować przed testami regresji DESK.
Zapisane i walidowane ustawienie SSD1306 w schema v8 nie jest driverem runtime.

## Procesor audio SALON — plan

Interfejs `AudioProcessor`: `setInput`, `setVolume`, `setMute`, `setBass`,
`setMid`, `setTreble`, `setBalance`, `setFader`, `setLoudness`.
`NoAudioProcessor` dla Desk/DIN; `TDA7719Processor` dla Salon. Zakresy,
mapowanie głośności i fizyczne wpięcie TDA7719 wymagają projektu sprzętu.
Nie tworzyć osobnego forka Salon ani drugiego DAC dla Bluetooth.

## Środowiska

Obecne `voxone` i `voxone_debug` budują DESK i pozostają bez zmian.
Docelowe nazwy `voxone_desk`, `voxone_din`, `voxone_salon` są wyłącznie
propozycją. Migracja env dopiero po ustaleniu board profiles i testach DESK.
## Provisional VoxOneBT board profile

`VoxOneBT/include/BtBoard.h` targets a classic ESP32-WROOM-32 in a
D1-mini-format board, built with PlatformIO `wemos_d1_mini32`. This is a proposed
**GPIO-number** mapping, not a claim that every clone exposes identical
headers. Verify the exact board schematic/silkscreen and 3.3 V levels before
wiring or upload.

| VoxOneBT signal | GPIO | Rationale |
|---|---:|---|
| I2S TX BCLK | 26 | Output-capable, not a boot strapping/flash GPIO |
| I2S TX LRCLK/WS | 25 | Output-capable, not a boot strapping/flash GPIO |
| I2S TX DATA | 27 | Output-capable, not a boot strapping/flash GPIO |
| UART2 TX to Main RX | 17 | General-purpose output, separate from USB UART0 |
| UART2 RX from Main TX | 16 | General-purpose input, separate from USB UART0 |

Avoid ESP32 GPIO0/2/5/12/15 (strapping), 6–11 (flash), 34–39
(input-only, hence unsuitable for TX). GPIO16/17 may be occupied by PSRAM
on a WROVER-style module; this profile assumes WROOM without PSRAM.
The exact D1-mini-format board must expose all five pins. Common GND is
mandatory. VoxOneBT I2S TX master BCLK/WS/DATA must go to **Main I2S RX
slave**, never directly to PCM5102A. Main's current DAC pins 26/25/27
remain untouched; future Main RX pins are deliberately **not assigned**
until its actual spare GPIO and peripheral conflicts are checked.

See [VOXONE_BT_PROTOCOL](VOXONE_BT_PROTOCOL.md) for UART wiring and PCM
constraints. This is a buildable prototype, not a validated wiring diagram.
