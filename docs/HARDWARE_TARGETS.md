# Rodziny sprzętowe VoxOne

Litera oznacza rodzinę, cyfra rewizję: `0` to prototyp, `1` pierwsza docelowa PCB, `2..n` następne rewizje tej samej rodziny. Rewizje mogą mieć różne GPIO. Capability wynika z konkretnego profilu i potwierdzonego sprzętu, a nie z nazwy rodziny. VoxOneBT jest osobnym projektem/repo i posiada własną rodzinę sprzętową Vx.

Profil rewizji jest kontraktem konkretnego prototypu. S0 opisuje obecnie
rzeczywisty, ręcznie okablowany ESP32-S3 Zero; DIN0 zachowuje tymczasową mapę
GPIO M0. Przyszłe PCB S1 i DIN1 wymagają osobnych profili i nie mogą
nadpisywać tych definicji.

Obowiązkowy fizyczny zakres Stable 1.0.0 obejmuje S0 i L0, oba z VoxOneBT.
W0, M0 i DIN0 pozostają w repozytorium, standardowych buildach, full-image i
testach offline, lecz ich fizyczny HIL nie blokuje wydania. Po Stable W0
przechodzi do LEGACY, zachowując możliwość krytycznego utrzymania linii 1.0.0.

Stan implementacji poniżej uwzględnia checkpoint
`RC1-S0-BT-IMPLEMENT-1`. Lista capability opisuje to, co kod kandydata
pozwala zbudować i wybrać; nie oznacza fizycznego HIL PASS żadnego wariantu.
Obowiązkowy baseline oraz opcjonalne, fizycznie niezakwalifikowane
rozszerzenia są rozdzielone w
[STABLE_CONTRACT.md](STABLE_CONTRACT.md).

| Rodzina | Przeznaczenie | Docelowe elementy rodziny — roadmapa, nie deklaracja rewizji 0 |
|---|---|---|
| Lx | rozbudowana | ESP32-S3 N16R8, PCM5102, DSPmini, W5500, SSD1306/SSD1309/CH1116/SH1106/ST7290 128×64, ST7789V 320×240, SSD1322 256×64, ST7796S 480×320, GC9A01 240×240, EC11, 3×SW, VoxOneBT, DS3231 |
| Mx | uniwersalna | ESP32-S3 Zero, PCM5102, MAX98357A, SSD1306/SSD1309/CH1116/SH1106/ST7290 128×64, ST7789V 320×240, SSD1322 256×64, ST7796S 480×320, GC9A01 240×240, EC11, 3×SW, pomiar baterii, VoxOneBT, DS3231 |
| Sx | mini | ESP32-S3 Zero, audio PCM5102A/MAX98357A/NONE, OLED SSD1306 128×32/128×64 oraz SSD1309/CH1116/SH1106 128×64/NONE, EC11/3×SW/NONE, opcjonalny pomiar baterii i DS3231; S0 docelowo ma VoxOneBT RX/TX aktualizowany przez osobne USB, bez MAIN→BT; rewizja S1 może otrzymać nowy pinout |
| DINx | szyna DIN | ESP32-S3 Zero, PCM5102, SSD1306 128×32/128×64, EC11, 3×SW, VoxOneBT |
| Wx | wspierany klasyczny ESP32 DevKit | ESP32 DevKit, PCM5102, SSD1306/SSD1309/CH1116/SH1106 128×64, ST7789V 320×240, ST7789 284×76, EC11, 3×SW, VoxOneBT |

Wiersz Sx opisuje rodzinę i zatwierdzony cel S0+VoxOneBT. Kandydat S0 ma
`VOXONE_HAS_BT=1`, audio BT RX i rzeczywisty backend w
`HardwareCapabilities`; VU i MAIN→BT update pozostają wyłączone. Pełny HIL
Stable S0 nadal jest otwarty.

## Bieżące profile i status wydania

| Profil / env | MCU i pamięć deklarowana przez build/kod | Bieżący BIN i capability | Rola wydania | Status hardware |
|---|---|---|---|---|
| `w0` | klasyczny ESP32; build `esp32dev`, bez PSRAM; deskryptor nie deklaruje fizycznego rozmiaru flash | PCM5102A; ST7789 284×76/NONE; EC11/Buttons3/NONE; RADIO/WWW/MQTT/Web Update; bez BT, RTC i VU | `RETAINED BUILD / HIL NON-BLOCKING` | Bez nowych funkcji; po wydaniu LEGACY. Stary fizyczny baseline zachowuje zakres historyczny. |
| `m0` | ESP32-S3 Zero, flash 4 MiB, PSRAM 2 MiB | Audio PCM5102A/MAX98357A MONO/MAX98357A STEREO/NONE; display NONE, SSD1306 128×32/128×64, SSD1309, SH1106 i CH1116G 128×64; EC11/Buttons3/NONE | `RETAINED BUILD / HIL NON-BLOCKING` | Wcześniejsze PHYSICAL PASS pozostają ważne; dalszy fizyczny HIL nie blokuje Stable. |
| `l0` | istniejąca PCB V-Tom, ESP32-S3 N16R8; build flash 16 MiB i PSRAM OPI | Audio PCM5102A/NONE; display ST7796S 480×320, pięć OLED I2C lub NONE; EC11/Buttons3/NONE; DS3231, VoxOneBT UART + I2S RX, VU i aktualizacja VoxOneBT | **`RELEASE-GATING HIL REQUIRED`** | Hardware i GPIO bez zmian. Etapowe PASS, w tym TTS D, są zachowane; pełny baseline i otwarte negatywne ścieżki PENDING. |
| `s0` | ESP32-S3 Zero, flash 4 MiB, PSRAM 2 MiB | Audio PCM5102A/MAX98357A MONO/MAX98357A STEREO/NONE; OLED-y; EC11/Buttons3/NONE; VoxOneBT UART protocol 2 + I2S RX; `VOXONE_HAS_BT=1`, `VOXONE_HAS_VU=0`, updater BT wyłączony | **`RELEASE-GATING HIL REQUIRED`** | Rewizja laptopowa: UART1 RX10/TX11, I2S1 RX 12/13/14 i logi aplikacji przez native USB CDC. VU oraz MAIN→BT update pozostają wyłączone; update modułu przez osobne USB. Software PASS / fizyczny HIL PENDING. |
| `din0` | ESP32-S3 Zero, flash 4 MiB, PSRAM 2 MiB | Pinout rewizji DIN0 jak M0; PCM5102A; OLED-y; EC11/Buttons3/NONE | `RETAINED BUILD / HIL NON-BLOCKING` | Brak fizycznego HIL obrazu DIN0 nie blokuje Stable; profil, buildy i historia pozostają zachowane. |

Zatwierdzony zakres Stable 1.0.0 wymaga fizycznego HIL PASS bazowej
konfiguracji S0 i L0. Build-only nie kwalifikuje żadnej z tych dwóch bramek.
W0/M0/DIN0 pozostają obowiązkowe w standardowym verify, ale bez profilowej
bramki fizycznej. Pełny kontrakt i skondensowaną macierz zawiera
[STABLE_CONTRACT.md](STABLE_CONTRACT.md).

### Faktyczny pinout bieżących BIN

`—` oznacza brak przypisanego GPIO. Linie `A/B/KEY` są wspólne dla EC11 i
Buttons3; firmware używa zwykłego `INPUT`, więc potrzebne są zewnętrzne
pull-upy i filtracja.

| Profil | Audio I²S `BCLK / DOUT / LRCK` | Display | Local input `A / B / KEY` | Pozostałe |
|---|---|---|---|---|
| W0 | `26 / 27 / 25` | ST7789: `CS5`, `DC4`, `RST—`; SCK/MOSI korzystają z domyślnej magistrali frameworka i nie są jawnie zakontraktowane w deskryptorze | `33 / 35 / 32` | brak PSRAM, BT, RTC, VU i XSMT |
| M0 | `1 / 2 / 3` | OLED I2C: `SDA7`, `SCL8`, `RST—`, adres `0x3C` | `5 / 6 / 4` | SSD1306 lub SSD1309 w jednym BIN |
| S0 | MAX98357A `5 / 6 / 4` | SSD1306 I2C: `SDA1`, `SCL2`, `RST—`, adres `0x3C`; runtime 128×32/128×64 | `9 / 8 / 7` | BT UART1 `RX10/TX11`; BT I2S1 RX `BCLK12/WS13/DIN14`; native USB CDC dla logów; VU i MAIN→BT update wyłączone; fizyczny HIL PENDING |
| DIN0 | `1 / 2 / 3` | OLED I2C: `SDA7`, `SCL8`, `RST—`, adres `0x3C` | `5 / 6 / 4` | zatwierdzona mapa rewizji 0 współdzielona z M0; HIL PENDING |
| L0 | `5 / 4 / 6` | SPI `SCK12`, `MOSI11`, `MISO13`; ST7796S `CS10`, `DC9`, `RST—`, backlight `14` | `41 / 40 / 39` | RTC `SDA8/SCL7`; BT UART `RX15/TX16`; BT I2S RX `BCLK1/WS2/DIN17`; XSMT poza baseline Stable, `VOXONE_DAC_XSMT_PIN=255`, bez przypisanego GPIO; opcjonalne XSMT/MUTE/ENABLE dla przyszłych PCB pozostaje POST-STABLE |

W kodzie `HardwareDescriptor.audioOut` prezentuje kolejność `BCLK/WS/DOUT`;
powyższa tabela zachowuje kolejność nazw makr profili, aby uniknąć pomylenia
GPIO. Żadne GPIO działających prototypów nie zostało zmienione przez rename.

### Rewizje 0 i rzeczywiste braki S0/DIN0

`VOXONE_PIN_MAP_COMPLETE=1` oznacza, że bieżący profil ma kompletną własną
mapę dla obecnie deklarowanych capability. S0 nie współdzieli GPIO M0:
odpowiada ręcznie okablowanej rewizji S0 z MAX98357A, SSD1306 i EC11, a jego
VoxOneBT używa UART1 GPIO10/11 i I2S1 RX GPIO12/13/14. DIN0 nadal zachowuje
mapę M0.
W ramach Stable S0 i L0 mają
przejść własny HIL z potwierdzeniem flash/PSRAM, zasilania, złączy, audio,
display, local input i VoxOneBT. Build ani test M0 nie zastępują S0.

Historyczny baseline S0 obejmuje MAX98357A MONO, runtime SSD1306
128×32/128×64/NONE oraz EC11/Buttons3/NONE. Bieżący BIN implementuje ponadto
PCM5102A, MAX98357A STEREO, audio NONE oraz SSD1309/SH1106/CH1116G 128×64.
Historyczny baseline DIN0 pozostaje przy PCM5102A, runtime SSD1306/SSD1309
128×64/NONE i EC11/Buttons3/NONE; bieżący BIN implementuje ponadto SSD1306
128×32, SH1106 i CH1116G 128×64. S0 ma capability VoxOneBT audio; bateria,
RTC, VU i aktualizacja firmware modułu przez MAIN pozostają wyłączone.
VoxOneBT i RTC nie są capability DIN0. Status dodatkowych, już
zaimplementowanych wariantów to
`OPTIONAL / OFFLINE PASS / PHYSICAL HIL NOT QUALIFIED`; ich fizyczna
kwalifikacja nie jest obowiązkowa, z wyjątkiem docelowej integracji BT S0.
Każdy istniejący PASS zachowuje swój udokumentowany zakres.

Dedykowane PCB S1 i DIN1 są przyszłymi rewizjami z własnym pinoutem i
capability. Ich definicje należy dodać jako osobne profile, bez modyfikowania
historycznych S0/DIN0. Projekt i HIL S1/DIN1 są POST-STABLE i nie zastępują
obowiązkowego HIL S0 rev. 0; fizyczny HIL DIN0 jest nieblokujący.

Zatwierdzony kierunek dalszych rewizji, nadal niezaimplementowany:

- S1 otrzyma własny pinout dopiero po decyzji sprzętowej; nie dziedziczy
  automatycznie GPIO S0;
- DIN1 ma startować bez zainstalowanego display i input (`NONE`/`NONE`), z
  opcjonalnym wyświetlaczem I²C oraz EC11 albo 3×SW dopiero po zdefiniowaniu
  pinoutu i capabilities;
- przyszła rodzina XL oraz wybór I²C/SPI dla docelowych M/L/XL wymagają
  osobnej decyzji o magistralach, GPIO, zasilaniu i pamięci.

Te kierunki nie zmieniają GPIO, defaultów ani deklarowanych możliwości S0 i
DIN0. Ich szczegółowy backlog pozostaje w [TODO.md](TODO.md).

### Flash i partycje

Wszystkie env używają `partitions_yoradio.csv`: NVS `0x5000`, `otadata`
`0x2000`, dwa sloty OTA po `0x1E0000` (1,875 MiB) i SPIFFS `0x30000`
(192 KiB). Układ zajmuje pierwsze 4 MiB flash. Dla L0 build i pełny obraz
mogą mieć 16 MiB, ale bieżąca tablica nie przeznacza pozostałych 12 MiB na
dane. Dla W0 fizyczny rozmiar flash wymaga potwierdzenia mimo 4-miB układu
partycji.

`HardwareDescriptor` podaje tożsamość PCB, magistrale, GPIO i aktualne
capabilities, w tym jawne `supportedInputs`, `supportedDisplays` i
`supportedAudioOutputs`. `RuntimeHardwareConfig` przechowuje display, input i
audio w istniejącym rekordzie 10 B z CRC. Każdy profil wybiera EC11, Buttons3
albo NONE. Buttons3 używa tych samych linii A/B/KEY co EC11 i ma BUILD PASS,
ale nie ma jeszcze pełnego PHYSICAL PASS. Obecność wariantu w deskryptorze i
WWW oznacza IMPLEMENTED, nie PHYSICAL PASS.

Złącze local input ma wspólny kontrakt `A / LEFT`, `B / RIGHT`, `KEY / OK`, `GND`. Zarówno EC11, jak i klawiatura 3×SW używają zewnętrznych pull-upów oraz filtracji na module lub PCB; przyciski NO zwierają wejścia do GND. Firmware konfiguruje A, B i KEY jako zwykłe `INPUT` i nie włącza dla nich `INPUT_PULLUP`.

Magistrala I²C S0 pracuje z częstotliwością 400 kHz. Moduł OLED lub
okablowanie musi zapewnić pull-upy SDA/SCL do właściwego napięcia logiki.

Każda bieżąca rodzina dopuszcza runtime stan „sprzęt nie jest zainstalowany” niezależnie dla display i local input:

| Profil | Runtime display | Runtime input |
|---|---|---|
| `l0` | NONE, ST7796S_480X320, SSD1306_128X32, SSD1306_128X64, SSD1309_128X64, SH1106_128X64, CH1116G_128X64 | NONE, EC11, BUTTONS3 |
| `m0` | NONE, SSD1306_128X32, SSD1306_128X64, SSD1309_128X64, SH1106_128X64, CH1116G_128X64 | NONE, EC11, BUTTONS3 |
| `s0` | NONE, SSD1306_128X32, SSD1306_128X64, SSD1309_128X64, SH1106_128X64, CH1116G_128X64 | NONE, EC11, BUTTONS3 |
| `din0` | NONE, SSD1306_128X32, SSD1306_128X64, SSD1309_128X64, SH1106_128X64, CH1116G_128X64 | NONE, EC11, BUTTONS3 |
| `w0` | NONE, ST7789_284X76 | NONE, EC11, BUTTONS3 |

| Profil | Runtime audio |
|---|---|
| `l0` | PCM5102A, NONE |
| `m0` | PCM5102A, MAX98357A MONO, MAX98357A STEREO, NONE |
| `s0` | PCM5102A, MAX98357A MONO, MAX98357A STEREO, NONE |
| `din0` | PCM5102A |
| `w0` | PCM5102A |

`display=NONE` nie uruchamia kontrolera ani magistrali display, nie alokuje framebufferów i nie uruchamia renderera. Techniczny DisplayTask pozostaje aktywny, ponieważ obsługuje również `timekeeper.loop0()` oraz — zależnie od konfiguracji — `netserver.loop()`; operacje display są wtedy no-op. `input=NONE` nie konfiguruje A/B/KEY, ISR, EC11 ani OneButton local input. NONE nie jest capability fizycznego urządzenia i nie zmienia build defaults.

Wariant display nazywamy `STEROWNIK_ROZDZIELCZOŚĆ` (np. `SSD1306_128X64`, `SSD1309_128X64`, `ST7796S_480X320`). Layout `OLED_128X64` jest wspólny dla SSD1306 i SSD1309. Oznaczenie `ST7290_128X64` wymaga sprawdzenia kontrolera na fizycznym module przed nazwaniem backendu.

Dla wariantu S0 `SSD1306_128X32` osobny `DisplayProfile`/layout nosi nazwę `OLED_128X32`; PLAYER, STATIONS, VOL, setup i update są ograniczone do fizycznych 32 pikseli. `SSD1322_256X64` jest wyłącznie zatwierdzonym projektem POST-STABLE; jego obecność w katalogu roadmapy nie oznacza backendu ani wsparcia runtime w obecnym BIN.

Aktualizacja VoxOneBT przez MAIN wymaga modułu, capability BT firmware update
oraz PSRAM/staging i pozostaje istniejącą funkcją L0. S0 aktualizuje VoxOneBT
bezpośrednio przez osobne USB; nie otrzymuje capability ani endpointu
MAIN→BT. Zadania i decyzje pozostają wyłącznie w [TODO.md](TODO.md).
