# Rodziny sprzętowe VoxOne

Litera oznacza rodzinę, cyfra rewizję: `0` to prototyp, `1` pierwsza docelowa PCB, `2..n` następne rewizje tej samej rodziny. Rewizje mogą mieć różne GPIO. Capability wynika z konkretnego profilu i potwierdzonego sprzętu, a nie z nazwy rodziny. VoxOneBT jest osobnym projektem/repo i posiada własną rodzinę sprzętową Vx.

Profil rewizji jest historycznym kontraktem: nowe PCB nie zmienia pinoutu ani
capability istniejącego profilu. S0 i DIN0 zachowują zatwierdzoną mapę GPIO M0
jako mapę własnej rewizji 0. Pierwsze dedykowane PCB rodzin S i DIN będą miały
osobne profile S1 i DIN1 z własnymi definicjami sprzętu.

| Rodzina | Przeznaczenie | Docelowe elementy rodziny — roadmapa, nie deklaracja rewizji 0 |
|---|---|---|
| Lx | rozbudowana | ESP32-S3 N16R8, PCM5102, DSPmini, W5500, SSD1306/SSD1309/CH1116/SH1106/ST7290 128×64, ST7789V 320×240, SSD1322 256×64, ST7796S 480×320, GC9A01 240×240, EC11, 3×SW, VoxOneBT, DS3231 |
| Mx | uniwersalna | ESP32-S3 Zero, PCM5102, MAX98357A, SSD1306/SSD1309/CH1116/SH1106/ST7290 128×64, ST7789V 320×240, SSD1322 256×64, ST7796S 480×320, GC9A01 240×240, EC11, 3×SW, pomiar baterii, VoxOneBT, DS3231 |
| Sx | mini | ESP32-S3 Zero, MAX98357A, SSD1306 128×32/128×64, EC11, 3×SW, pomiar baterii, VoxOneBT, DS3231 |
| DINx | szyna DIN | ESP32-S3 Zero, PCM5102, SSD1306 128×32/128×64, EC11, 3×SW, VoxOneBT |
| Wx | wspierany klasyczny ESP32 DevKit | ESP32 DevKit, PCM5102, SSD1306/SSD1309/CH1116/SH1106 128×64, ST7789V 320×240, ST7789 284×76, EC11, 3×SW, VoxOneBT |

## Bieżące profile i status wydania

| Profil / env | MCU i pamięć deklarowana przez build/kod | Bieżący BIN i capability | Status hardware |
|---|---|---|---|
| `w0` | klasyczny ESP32; build `esp32dev`, bez PSRAM; deskryptor nie deklaruje fizycznego rozmiaru flash | PCM5102A, ST7789 284×76, EC11/Buttons3/NONE, RADIO/WWW/MQTT/Web Update; bez BT, RTC i VU | Istnieje wcześniejszy pinout fizyczny; pełna regresja Stable po rename i SECURITY/OTA pozostaje PENDING |
| `m0` | ESP32-S3 Zero, flash 4 MiB, PSRAM 2 MiB | PCM5102A, runtime SSD1306/SSD1309 128×64/NONE, EC11/Buttons3/NONE; bez BT, RTC i VU | SSD1306, SSD1309, PCM5102A, EC11 i wybrane kombinacje NONE mają wcześniejszy PHYSICAL PASS; końcowy HIL Stable PENDING |
| `l0` | ESP32-S3; build flash 16 MiB i PSRAM OPI; rozmiar PSRAM nie jest utrwalony w deskryptorze | PCM5102A, ST7796S 480×320, EC11/Buttons3/NONE, DS3231, VoxOneBT UART + I2S RX, VU i aktualizacja VoxOneBT | Funkcje mają etapowe baseline'y, lecz skonsolidowany HIL Stable, endurance i negatywne update pozostają PENDING |
| `s0` | ESP32-S3 Zero, flash 4 MiB, PSRAM 2 MiB | Zatwierdzony pinout rewizji 0 i bieżące capability jak M0: PCM5102A, OLED 128×64 i local input; przyszłe capability rodziny S nie są włączone | Obowiązkowy profil Stable; brak fizycznego PASS S0 na odpowiednim sprzęcie rewizji 0 jest blockerem wydania |
| `din0` | ESP32-S3 Zero, flash 4 MiB, PSRAM 2 MiB | Zatwierdzony pinout rewizji 0 i bieżące capability jak M0: PCM5102A, OLED 128×64 i local input; przyszłe wyposażenie DIN nie jest włączone | Obowiązkowy profil Stable; brak fizycznego PASS DIN0 na odpowiednim sprzęcie rewizji 0 jest blockerem wydania |

Zatwierdzony zakres Stable 1.0.0 wymaga fizycznego HIL PASS profili W0, M0,
L0, S0 i DIN0. Build-only nie kwalifikuje żadnego z nich. Pełny kontrakt i
skondensowaną macierz HIL zawiera
[STABLE_CONTRACT.md](STABLE_CONTRACT.md).

### Faktyczny pinout bieżących BIN

`—` oznacza brak przypisanego GPIO. Linie `A/B/KEY` są wspólne dla EC11 i
Buttons3; firmware używa zwykłego `INPUT`, więc potrzebne są zewnętrzne
pull-upy i filtracja.

| Profil | Audio PCM5102A `BCLK / DOUT / LRCK` | Display | Local input `A / B / KEY` | Pozostałe |
|---|---|---|---|---|
| W0 | `26 / 27 / 25` | ST7789: `CS5`, `DC4`, `RST—`; SCK/MOSI korzystają z domyślnej magistrali frameworka i nie są jawnie zakontraktowane w deskryptorze | `33 / 35 / 32` | brak PSRAM, BT, RTC, VU i XSMT |
| M0 | `1 / 2 / 3` | OLED I2C: `SDA7`, `SCL8`, `RST—`, adres `0x3C` | `5 / 6 / 4` | SSD1306 lub SSD1309 w jednym BIN |
| S0 | `1 / 2 / 3` | OLED I2C: `SDA7`, `SCL8`, `RST—`, adres `0x3C` | `5 / 6 / 4` | zatwierdzona mapa rewizji 0 współdzielona z M0; HIL PENDING |
| DIN0 | `1 / 2 / 3` | OLED I2C: `SDA7`, `SCL8`, `RST—`, adres `0x3C` | `5 / 6 / 4` | zatwierdzona mapa rewizji 0 współdzielona z M0; HIL PENDING |
| L0 | `5 / 4 / 6` | SPI `SCK12`, `MOSI11`, `MISO13`; ST7796S `CS10`, `DC9`, `RST—`, backlight `14` | `41 / 40 / 39` | RTC `SDA8/SCL7`; BT UART `RX15/TX16`; BT I2S RX `BCLK1/WS2/DIN17`; XSMT nieprzypisany |

W kodzie `HardwareDescriptor.audioOut` prezentuje kolejność `BCLK/WS/DOUT`;
powyższa tabela zachowuje kolejność nazw makr profili, aby uniknąć pomylenia
GPIO. Żadne GPIO działających prototypów nie zostało zmienione przez rename.

### Rewizje 0 i rzeczywiste braki S0/DIN0

`VOXONE_PIN_MAP_COMPLETE=1` oraz zgodność `profiles/s0.h` i
`profiles/din0.h` z M0 odzwierciedlają zatwierdzony pinout rewizji 0. Nie ma
wymagania zmiany tych GPIO przed Stable. Brakiem pozostaje dowód fizyczny:
każdy profil musi przejść HIL na odpowiednim urządzeniu S0 albo DIN0, z
potwierdzeniem deklarowanych flash/PSRAM, zasilania i złączy oraz działania
PCM5102A, OLED i local input. Uruchomienie właściwego BIN wyłącznie na
urządzeniu M0 nie zastępuje identyfikacji i PASS osobnego targetu.

Bieżący kontrakt obu profili obejmuje PCM5102A, runtime SSD1306/SSD1309
128×64/NONE oraz EC11/Buttons3/NONE. Nie obejmuje MAX98357A, OLED 128×32,
pomiaru baterii, VoxOneBT, RTC ani VU na S0; na DIN0 nie obejmuje OLED 128×32,
VoxOneBT, RTC ani VU. Są to braki względem szerszej roadmapy rodzin, nie
ukryte capability aktualnych BIN. Rozszerzenie tego zestawu wymaga osobnej
decyzji zakresowej i implementacji; samo obowiązkowe HIL Stable nie może
zadeklarować tych funkcji jako działających.

Dedykowane PCB S1 i DIN1 są przyszłymi rewizjami z własnym pinoutem i
capability. Ich definicje należy dodać jako osobne profile, bez modyfikowania
historycznych S0/DIN0. Projekt, implementacja i HIL S1/DIN1 są POST-STABLE i
nie blokują Stable 1.0.0 dla obowiązkowych rewizji 0.

### Flash i partycje

Wszystkie env używają `partitions_yoradio.csv`: NVS `0x5000`, `otadata`
`0x2000`, dwa sloty OTA po `0x1E0000` (1,875 MiB) i SPIFFS `0x30000`
(192 KiB). Układ zajmuje pierwsze 4 MiB flash. Dla L0 build i pełny obraz
mogą mieć 16 MiB, ale bieżąca tablica nie przeznacza pozostałych 12 MiB na
dane. Dla W0 fizyczny rozmiar flash wymaga potwierdzenia mimo 4-miB układu
partycji.

`HardwareDescriptor` podaje tożsamość PCB, magistrale, GPIO i aktualne capabilities, w tym jawny `supportedInputs`. `RuntimeHardwareConfig` wybiera na M0/S0/DIN0 jeden z dwóch skompilowanych backendów OLED w tym samym BIN oraz na każdym bieżącym profilu dokładnie jeden backend lokalnego wejścia: EC11, Buttons3 albo NONE. Audio pozostaje wyborem bieżącego BIN; pozycje audio i modułów widoczne w katalogu `SPRZĘT` mogą mieć status `PLANOWANE` i nie są wtedy selectable. Buttons3 używa tych samych linii A/B/KEY co EC11 i ma BUILD PASS, ale nie ma jeszcze PHYSICAL PASS. Nowe warianty wymagają fizycznej weryfikacji i osobnych capabilities. W szczególności MAX98357A, OLED 128×32, pomiar baterii, DS3231 i VoxOneBT na S0/DIN0 nie są teraz deklarowane jako działające.

Złącze local input ma wspólny kontrakt `A / LEFT`, `B / RIGHT`, `KEY / OK`, `GND`. Zarówno EC11, jak i klawiatura 3×SW używają zewnętrznych pull-upów oraz filtracji na module lub PCB; przyciski NO zwierają wejścia do GND. Firmware konfiguruje A, B i KEY jako zwykłe `INPUT` i nie włącza dla nich `INPUT_PULLUP`.

Każda bieżąca rodzina dopuszcza runtime stan „sprzęt nie jest zainstalowany” niezależnie dla display i local input:

| Profil | Runtime display | Runtime input |
|---|---|---|
| `l0` | NONE, ST7796S_480X320 | NONE, EC11, BUTTONS3 |
| `m0` | NONE, SSD1306_128X64, SSD1309_128X64 | NONE, EC11, BUTTONS3 |
| `s0` | NONE, SSD1306_128X64, SSD1309_128X64 | NONE, EC11, BUTTONS3 |
| `din0` | NONE, SSD1306_128X64, SSD1309_128X64 | NONE, EC11, BUTTONS3 |
| `w0` | NONE, ST7789_284X76 | NONE, EC11, BUTTONS3 |

`display=NONE` nie uruchamia kontrolera ani magistrali display, nie alokuje framebufferów i nie uruchamia renderera. Techniczny DisplayTask pozostaje aktywny, ponieważ obsługuje również `timekeeper.loop0()` oraz — zależnie od konfiguracji — `netserver.loop()`; operacje display są wtedy no-op. `input=NONE` nie konfiguruje A/B/KEY, ISR, EC11 ani OneButton local input. NONE nie jest capability fizycznego urządzenia i nie zmienia build defaults.

Wariant display nazywamy `STEROWNIK_ROZDZIELCZOŚĆ` (np. `SSD1306_128X64`, `SSD1309_128X64`, `ST7796S_480X320`). Layout `OLED_128X64` jest wspólny dla SSD1306 i SSD1309. Oznaczenie `ST7290_128X64` wymaga sprawdzenia kontrolera na fizycznym module przed nazwaniem backendu.

Dla przyszłego wariantu `SSD1306_128X32` wspólny `DisplayProfile`/layout nosi nazwę `OLED_128X32`; specyfikację ekranów PLAYER, STATIONS i VOL zawiera sekcja Hardware targets w [TODO.md](TODO.md). `SSD1322_256X64` jest wyłącznie zatwierdzonym projektem POST-STABLE; jego obecność w katalogu roadmapy nie oznacza backendu ani wsparcia runtime w obecnym BIN.

Aktualizacja VoxOneBT przez MAIN wymaga modułu, capability BT firmware update oraz PSRAM/staging. Na Wx przewidziana jest aktualizacja bezpośrednio przez USB. Zadania i decyzje pozostają wyłącznie w [TODO.md](TODO.md).
