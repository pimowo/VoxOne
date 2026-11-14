# VOXONE - MATRIX SPRZĘTOWY (v2)

| SPRZĘT / FUNKCJA | DIN | S | M | L | STATUS |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **[MIKROKONTROLERY]** | | | | | |
| Baza DevKit — ESP32-S3 N16R8 | NIE | NIE | TAK | TAK | 🟢 ZROBIONE |
| Baza Zero — ESP32-S3 Zero N4R2 | TAK | TAK | NIE | NIE | 🟢 ZROBIONE |
| **[WYŚWIETLACZE]** | | | | | |
| SSD1306 128x64, SSD1309, SH1106 | TAK | TAK | TAK | TAK | 🟢 ZROBIONE |
| CH1116G 128x64 | TAK | TAK | NIE | NIE | 🟢 ZROBIONE |
| SSD1322 256x64, GC9A01 240x240 | NIE | NIE | TAK | TAK | 🟡 W TESTACH |
| ST7796S 480x320 | NIE | NIE | TAK | TAK | 🟢 ZROBIONE |
| **[STEROWANIE]** | | | | | |
| Enkoder EC11 / 3xSW | TAK | TAK | TAK | TAK | 🟢 ZROBIONE |
| Przycisk POWER | TAK | TAK | TAK | TAK | 🟡 W TESTACH |
| Pilot IR | NIE | TAK | TAK | TAK | 🔴 W PLANACH |
| **[AUDIO]** | | | | | |
| PCM5102A | TAK | TAK | TAK | TAK | 🟢 ZROBIONE |
| MAX98357A | NIE | TAK | TAK | NIE | 🟢 ZROBIONE |
| DSPmini | NIE | NIE | NIE | TAK | 🟡 W TESTACH |
| VoxOneBT RX | NIE | TAK | NIE | TAK | 🟢 ZROBIONE |
| VoxOneBT TX | NIE | NIE | NIE | NIE | 🔴 W PLANACH |
| AUX | NIE | NIE | TAK | TAK | 🔴 W PLANACH |
| **[SIEĆ I PAMIĘĆ]** | | | | | |
| Ethernet W5500 | NIE | NIE | NIE | TAK | 🔴 W PLANACH |
| Karta SD | NIE | NIE | TAK | TAK | 🔴 W PLANACH |
| DLNA | TAK | NIE | TAK | TAK | ⚠️ DO POPRAWY |
| **[ZASILANIE I RTC]** | | | | | |
| RTC DS3231 | NIE | NIE | NIE | TAK | 🟢 ZROBIONE |
| Przekaźnik / Pomiar napięcia | NIE | NIE | NIE | NIE | 🔴 W PLANACH |

`TAK` przy elemencie ze statusem `W PLANACH` oznacza zadeklarowane capability
profilu, a nie gotowy sterownik. Piny AMP_ENABLE i MUTE/XTSM zostaną
uzupełnione po weryfikacji sprzętowej.

## Wspólne pinouty baz MCU

| Baza / profile | Audio I²S `BCLK / DOUT / LRCK` | Display / I2C | Local input `A / B / KEY` | Pozostałe |
|---|---|---|---|---|
| DevKit / M0, L0 | `5 / 4 / 6` | SPI `SCK12`, `MOSI11`, `MISO13`, `CS10`, `DC9`, `RST—`; I2C `SDA8/SCL7`; L0 backlight `14` | `41 / 40 / 39` | SD `CS18` na wspólnym SPI; IR `GPIO38`; POWER `GPIO42`; XSMT `255`; L0 dodatkowo RTC oraz BT UART `RX15/TX16` i BT I2S RX `BCLK1/WS2/DIN17` |
| Zero / S0, DIN0 | `5 / 6 / 4` | I2C `SDA1`, `SCL2`, `RST—`, adres `0x3C` | `9 / 8 / 7` | POWER `GPIO3`; S0 dodatkowo IR `GPIO38`, BT UART1 `RX44/TX43` i BT I2S1 RX `BCLK11/WS12/DIN13` |

Powyższe wspólne GPIO są zdefiniowane w `profiles/bases/devkit.h` oraz
`profiles/bases/zero.h`. Profile konkretnego urządzenia dodają tylko piny
opcjonalnego wyposażenia i capability.

## Rodziny sprzętowe VoxOne

Litera oznacza rodzinę, cyfra rewizję: `0` to prototyp, `1` pierwsza docelowa PCB, `2..n` następne rewizje tej samej rodziny. Rewizje mogą mieć różne GPIO. Capability wynika z konkretnego profilu i potwierdzonego sprzętu, a nie z nazwy rodziny. VoxOneBT jest osobnym projektem/repo i posiada własną rodzinę sprzętową Vx.

Profil rewizji jest kontraktem konkretnego prototypu. S0 opisuje obecnie
rzeczywisty, ręcznie okablowany ESP32-S3 Zero; DIN0 dziedziczy tę samą bazową
mapę GPIO. Przyszłe PCB S1 i DIN1 wymagają osobnych profili i nie mogą
nadpisywać tych definicji.

Obowiązkowy fizyczny zakres Stable 1.0.0 obejmuje S0 i L0, oba z VoxOneBT.
M0 i DIN0 pozostają w standardowych buildach, full-image i testach offline,
lecz ich fizyczny HIL nie blokuje wydania.

Stan implementacji poniżej uwzględnia checkpoint
`RC1-S0-BT-IMPLEMENT-1`. Lista capability opisuje to, co kod kandydata
pozwala zbudować i wybrać; nie oznacza fizycznego HIL PASS żadnego wariantu.
Obowiązkowy baseline oraz opcjonalne, fizycznie niezakwalifikowane
rozszerzenia są rozdzielone w
[STABLE_CONTRACT.md](STABLE_CONTRACT.md).

| Rodzina | Przeznaczenie | Docelowe elementy rodziny — roadmapa, nie deklaracja rewizji 0 |
|---|---|---|
| Lx | rozbudowana | ESP32-S3 N16R8, PCM5102, DSPmini, W5500, SSD1306/SSD1309/CH1116/SH1106/ST7290 128×64, ST7789V 320×240, SSD1322 256×64, ST7796S 480×320, GC9A01 240×240, EC11, 3×SW, VoxOneBT, DS3231 |
| Mx | uniwersalna | ESP32-S3 N16R8 DevKit, PCM5102, MAX98357A, SSD1306/SSD1309/CH1116/SH1106/ST7290 128×64, ST7789V 320×240, SSD1322 256×64, ST7796S 480×320, GC9A01 240×240, EC11, 3×SW, pomiar baterii, VoxOneBT, DS3231 |
| Sx | mini | ESP32-S3 Zero, audio PCM5102A/MAX98357A/NONE, OLED SSD1306 128×32/128×64 oraz SSD1309/CH1116/SH1106 128×64/NONE, EC11/3×SW/NONE, opcjonalny pomiar baterii i DS3231; S0 docelowo ma VoxOneBT RX/TX aktualizowany przez osobne USB, bez MAIN→BT; rewizja S1 może otrzymać nowy pinout |
| DINx | szyna DIN | ESP32-S3 Zero, PCM5102, SSD1306 128×32/128×64, EC11, 3×SW, VoxOneBT |

Wiersz Sx opisuje rodzinę i zatwierdzony cel S0+VoxOneBT. Kandydat S0 ma
`VOXONE_HAS_BT=1`, audio BT RX i rzeczywisty backend w
`HardwareCapabilities`; VU i MAIN→BT update pozostają wyłączone. Pełny HIL
Stable S0 nadal jest otwarty.

## Bieżące profile i status wydania

| Profil / env | MCU i pamięć deklarowana przez build/kod | Bieżący BIN i capability | Rola wydania | Status hardware |
|---|---|---|---|---|
| `m0` | baza DevKit, ESP32-S3 N16R8, flash 16 MiB, PSRAM 8 MiB | Domyślnie SSD1322 256×64; wspierane ST7796S, SSD1322, GC9A01, SSD1306, SSD1309 i SH1106; audio PCM5102A/MAX98357A MONO/MAX98357A STEREO/NONE; EC11/Buttons3/NONE; bez DSP | `RETAINED BUILD / HIL NON-BLOCKING` | Nowy default SSD1322 i pinout SPI wymagają potwierdzenia fizycznego. |
| `l0` | istniejąca PCB V-Tom, ESP32-S3 N16R8; build flash 16 MiB i PSRAM OPI | Domyślnie ST7796S 480×320; wspierane ST7796S, SSD1322, GC9A01, SSD1306, SSD1309 i SH1106; audio PCM5102A/NONE; EC11/Buttons3/NONE; DS3231, VoxOneBT UART + I2S RX, VU i aktualizacja VoxOneBT | **`RELEASE-GATING HIL REQUIRED`** | Hardware i GPIO bez zmian. Historyczne etapowe PASS są zachowane; pełny baseline i otwarte negatywne ścieżki PENDING. |
| `s0` | ESP32-S3 Zero, flash 4 MiB, PSRAM 2 MiB | Domyślnie SSD1306 128×64; wspierane SSD1306, SSD1309, SH1106 i CH1116G; audio PCM5102A/MAX98357A MONO/MAX98357A STEREO/NONE; EC11/Buttons3/NONE; VoxOneBT UART protocol 2 + I2S RX | **`RELEASE-GATING HIL REQUIRED`** | Fizycznie używana rewizja: UART1 RX44/TX43, I2S1 RX BCLK11/WS12/DIN13 i logi aplikacji przez native USB CDC. VU oraz MAIN→BT update pozostają wyłączone; update modułu przez osobne USB. Software PASS / fizyczny HIL PENDING. |
| `din0` | ESP32-S3 Zero, flash 4 MiB, PSRAM 2 MiB | Domyślnie SSD1306 128×64; wspierane SSD1306, SSD1309, SH1106 i CH1116G; bazowy pinout Zero wspólny z S0; PCM5102A; EC11/Buttons3/NONE | `RETAINED BUILD / HIL NON-BLOCKING` | Brak fizycznego HIL obrazu DIN0 nie blokuje Stable; profil, buildy i historia pozostają zachowane. |

Zatwierdzony zakres Stable 1.0.0 wymaga fizycznego HIL PASS bazowej
konfiguracji S0 i L0. Build-only nie kwalifikuje żadnej z tych dwóch bramek.
M0/DIN0 pozostają obowiązkowe w standardowym verify, ale bez profilowej
bramki fizycznej. Pełny kontrakt i skondensowaną macierz zawiera
[STABLE_CONTRACT.md](STABLE_CONTRACT.md).

### Faktyczny pinout bieżących BIN

`—` oznacza brak przypisanego GPIO. Linie `A/B/KEY` są wspólne dla EC11 i
Buttons3; firmware używa zwykłego `INPUT`, więc potrzebne są zewnętrzne
pull-upy i filtracja.

| Profil | Audio I²S `BCLK / DOUT / LRCK` | Display | Local input `A / B / KEY` | Pozostałe |
|---|---|---|---|---|
| M0 | `5 / 4 / 6` | SSD1322 SPI: `SCK12`, `MOSI11`, `MISO13`, `CS10`, `DC9`, `RST—` | `41 / 40 / 39` | SD `CS18` na wspólnym SPI; IR `GPIO38`; POWER `GPIO42`; XSMT `255`; HIL PENDING po migracji |
| S0 | MAX98357A `5 / 6 / 4` | SSD1306 I2C: `SDA1`, `SCL2`, `RST—`, adres `0x3C` | `9 / 8 / 7` | IR `GPIO38`; BT UART1 `RX44/TX43`; BT I2S1 RX `BCLK11/WS12/DIN13`; native USB CDC dla logów; VU i MAIN→BT update wyłączone; fizyczny HIL PENDING |
| DIN0 | `5 / 6 / 4` | OLED I2C: `SDA1`, `SCL2`, `RST—`, adres `0x3C` | `9 / 8 / 7` | baza Zero wspólna z S0; GPIO3 jest wejściem POWER; HIL PENDING |
| L0 | `5 / 4 / 6` | SPI `SCK12`, `MOSI11`, `MISO13`; ST7796S `CS10`, `DC9`, `RST—`, backlight `14` | `41 / 40 / 39` | SD na wspólnym SPI z `CS18`; IR `GPIO38`; RTC `SDA8/SCL7`; BT UART `RX15/TX16`; BT I2S RX `BCLK1/WS2/DIN17`; W5500 i AUX mają capability bez docelowych pinów; XSMT `255` |

W kodzie `HardwareDescriptor.audioOut` prezentuje kolejność `BCLK/WS/DOUT`;
powyższa tabela zachowuje kolejność nazw makr profili, aby uniknąć pomylenia
GPIO. Migracja M0 i DIN0 na wspólne bazy zmienia ich wcześniejsze tymczasowe
mapy pinów i dlatego pozostaje do fizycznej weryfikacji.

### Rewizje 0 i rzeczywiste braki S0/DIN0

`VOXONE_PIN_MAP_COMPLETE=1` oznacza kompletną mapę uruchamianego obecnie
baseline. Planowane capability W5500 i AUX mogą być zadeklarowane przed
ustaleniem docelowych pinów i napisaniem sterowników. S0 i DIN0 współdzielą
bazowe GPIO Zero.
S0 odpowiada ręcznie okablowanej rewizji z MAX98357A, SSD1306 i EC11, a jego
VoxOneBT używa UART1 RX GPIO44/TX GPIO43 i I2S1 RX GPIO11/12/13. S0 kieruje
diagnostykę aplikacji przez native USB CDC, aby UART0 nie konkurował o
GPIO43/44. DIN0 nie deklaruje opcjonalnych pinów VoxOneBT.
W ramach Stable S0 i L0 mają
przejść własny HIL z potwierdzeniem flash/PSRAM, zasilania, złączy, audio,
display, local input i VoxOneBT. Build ani test M0 nie zastępują S0.

Historyczny baseline S0 obejmuje MAX98357A MONO i SSD1306 128×64. Bieżący
BIN implementuje ponadto PCM5102A, MAX98357A STEREO, audio NONE oraz
SSD1309/SH1106/CH1116G 128×64. Historyczny baseline DIN0 pozostaje przy
PCM5102A i SSD1306/SSD1309 128×64; bieżący BIN implementuje ponadto SH1106 i
CH1116G 128×64. Oba profile Zero zachowują runtime `NONE` dla display i
EC11/Buttons3/NONE dla wejścia lokalnego. S0 ma capability VoxOneBT audio;
bateria, RTC, VU i aktualizacja firmware modułu przez MAIN pozostają
wyłączone.
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
dane. Dotyczy to profili DevKit M0 i L0.

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
| `l0` | NONE, ST7796S_480X320, SSD1322_256X64, GC9A01_240X240, SSD1306_128X64, SSD1309_128X64, SH1106_128X64 | NONE, EC11, BUTTONS3 |
| `m0` | NONE, ST7796S_480X320, SSD1322_256X64, GC9A01_240X240, SSD1306_128X64, SSD1309_128X64, SH1106_128X64 | NONE, EC11, BUTTONS3 |
| `s0` | NONE, SSD1306_128X64, SSD1309_128X64, SH1106_128X64, CH1116G_128X64 | NONE, EC11, BUTTONS3 |
| `din0` | NONE, SSD1306_128X64, SSD1309_128X64, SH1106_128X64, CH1116G_128X64 | NONE, EC11, BUTTONS3 |

| Profil | Runtime audio |
|---|---|
| `l0` | PCM5102A, DSPmini (ADAU1701; I2S bez konfiguracji I2C), NONE |
| `m0` | PCM5102A, MAX98357A MONO, MAX98357A STEREO, NONE |
| `s0` | PCM5102A, MAX98357A MONO, MAX98357A STEREO, NONE |
| `din0` | PCM5102A |

L0 zapisuje aktywną ścieżkę `AUDIO_PCM5102` / `AUDIO_DSPMINI` jako
kompatybilne rozszerzenie Config v7 i synchronizuje ją z wyborem sprzętowym w
NVS. Kreator First Boot oraz zakładka Sprzęt udostępniają oba warianty. Panel
WWW pokazuje zakładkę Audio dla PCM5102 albo DSP dla DSPmini; menu urządzenia
analogicznie ukrywa ustawienia właściwe wyłącznie dla PCM5102.

`display=NONE` nie uruchamia kontrolera ani magistrali display, nie alokuje framebufferów i nie uruchamia renderera. Techniczny DisplayTask pozostaje aktywny, ponieważ obsługuje również `timekeeper.loop0()` oraz — zależnie od konfiguracji — `netserver.loop()`; operacje display są wtedy no-op. `input=NONE` nie konfiguruje A/B/KEY, ISR, EC11 ani OneButton local input. NONE nie jest capability fizycznego urządzenia i nie zmienia build defaults.

Wariant display nazywamy `STEROWNIK_ROZDZIELCZOŚĆ` (np. `SSD1306_128X64`, `SSD1309_128X64`, `ST7796S_480X320`). Layout `OLED_128X64` jest wspólny dla SSD1306 i SSD1309. Oznaczenie `ST7290_128X64` wymaga sprawdzenia kontrolera na fizycznym module przed nazwaniem backendu.

M0 używa domyślnie backendu i layoutu `SSD1322_256X64`. Identyfikatory
SSD1322 oraz GC9A01 zostały dopisane na końcu istniejącej listy runtime, aby
nie zmieniać utrwalonych wartości wcześniejszych kontrolerów.

Aktualizacja VoxOneBT przez MAIN wymaga modułu, capability BT firmware update
oraz PSRAM/staging i pozostaje istniejącą funkcją L0. S0 aktualizuje VoxOneBT
bezpośrednio przez osobne USB; nie otrzymuje capability ani endpointu
MAIN→BT. Zadania i decyzje pozostają wyłącznie w [TODO.md](TODO.md).
