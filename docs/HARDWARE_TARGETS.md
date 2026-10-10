# Rodziny sprzętowe VoxOne

Litera oznacza rodzinę, cyfra rewizję: `0` to prototyp, `1` pierwsza docelowa PCB, `2..n` następne rewizje tej samej rodziny. Rewizje mogą mieć różne GPIO. Capability wynika z konkretnego profilu i potwierdzonego sprzętu, a nie z nazwy rodziny. VoxOneBT jest osobnym projektem/repo i posiada własną rodzinę sprzętową Vx.

| Rodzina | Przeznaczenie | Docelowe elementy rodziny — roadmapa, nie deklaracja rewizji 0 |
|---|---|---|
| Lx | rozbudowana | ESP32-S3 N16R8, PCM5102, DSPmini, W5500, SSD1306/SSD1309/CH1116/SH1106/ST7290 128×64, ST7789V 320×240, SSD1322 256×64, ST7796S 480×320, GC9A01 240×240, EC11, 3×SW, VoxOneBT, DS3231 |
| Mx | uniwersalna | ESP32-S3 Zero, PCM5102, MAX98357A, SSD1306/SSD1309/CH1116/SH1106/ST7290 128×64, ST7789V 320×240, SSD1322 256×64, ST7796S 480×320, GC9A01 240×240, EC11, 3×SW, pomiar baterii, VoxOneBT, DS3231 |
| Sx | mini | ESP32-S3 Zero, MAX98357A, SSD1306 128×32/128×64, EC11, 3×SW, pomiar baterii, VoxOneBT, DS3231 |
| DINx | szyna DIN | ESP32-S3 Zero, PCM5102, SSD1306 128×32/128×64, EC11, 3×SW, VoxOneBT |
| Wx | wspierany klasyczny ESP32 DevKit | ESP32 DevKit, PCM5102, SSD1306/SSD1309/CH1116/SH1106 128×64, ST7789V 320×240, ST7789 284×76, EC11, 3×SW, VoxOneBT |

## Bieżące profile i prototypy

| Profil / env | Sprzęt testowy | Aktualne możliwości w kodzie |
|---|---|---|
| `l0` | prototyp ESP32-S3 N16R8 | ST7796S 480×320, PCM5102A, EC11, VoxOneBT UART/I2S RX i firmware update, RTC, VU; dotychczasowe GPIO |
| `m0` | ESP32-S3 Zero | PCM5102A GPIO1/2/3, EC11 GPIO4/5/6, SSD1306 i SSD1309 128×64 I2C GPIO7/8, `OLED_128X64`, runtime display selection; oba OLED miały PHYSICAL PASS na jednym BIN przed rename |
| `s0` | obecny prototyp ESP32-S3 Zero | osobny profil, na razie ten sam roboczy pinout i zaimplementowane PCM5102A, EC11, OLED 128×64 oraz runtime selection; sprzęt kierunku mini wymaga przyszłej PCB/driverów |
| `din0` | obecny prototyp ESP32-S3 Zero | osobny profil, na razie ten sam roboczy pinout i zaimplementowane PCM5102A, EC11, OLED 128×64 oraz runtime selection; wyposażenie DIN wymaga przyszłej PCB/driverów |
| `w0` | prototyp klasyczny ESP32 DevKit | PCM5102A, ST7789 284×76, EC11 i dotychczasowe GPIO |

L0 zachowuje SPI SCK12/MOSI11/MISO13, ST7796S CS10/DC9/backlight14, EC11 GPIO41/40/39, I2S GPIO4/5/6, RTC SDA8/SCL7 i VoxOneBT UART15/16 oraz I2S RX1/2/17. W0 zachowuje ST7789 CS5/DC4, I2S 27/26/25 i EC11 33/35/32. Żadne GPIO działających prototypów nie zostało zmienione przez rename.

`HardwareDescriptor` podaje tożsamość PCB, magistrale, GPIO i aktualne capabilities. `RuntimeHardwareConfig` wybiera na M0/S0/DIN0 jeden z dwóch skompilowanych backendów OLED w tym samym BIN. Nowe warianty wymagają fizycznej weryfikacji i osobnych capabilities. W szczególności MAX98357A, OLED 128×32, pomiar baterii, 3×SW, DS3231 i VoxOneBT na S0/DIN0 nie są teraz deklarowane jako działające.

Wariant display nazywamy `STEROWNIK_ROZDZIELCZOŚĆ` (np. `SSD1306_128X64`, `SSD1309_128X64`, `ST7796S_480X320`). Layout `OLED_128X64` jest wspólny dla SSD1306 i SSD1309. Oznaczenie `ST7290_128X64` wymaga sprawdzenia kontrolera na fizycznym module przed nazwaniem backendu.

Dla przyszłego wariantu `SSD1306_128X32` wspólny `DisplayProfile`/layout nosi nazwę `OLED_128X32`; specyfikację ekranów PLAYER, STATIONS i VOL zawiera sekcja Hardware targets w [TODO.md](TODO.md).

Aktualizacja VoxOneBT przez MAIN wymaga modułu, capability BT firmware update oraz PSRAM/staging. Na Wx przewidziana jest aktualizacja bezpośrednio przez USB. Zadania i decyzje pozostają wyłącznie w [TODO.md](TODO.md).
