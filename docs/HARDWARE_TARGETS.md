# Hardware naming contract i targety PCB

Produkty nazywają się VoxOne i VoxOneBT. Litera identyfikuje rodzinę PCB, a cyfra rewizję: `0` oznacza prototyp przed pierwszą właściwą PCB, `1` pierwszą wersję PCB. Dlatego A0 i A1 należą do tej samej rodziny o wspólnej logice produktu, lecz mogą różnić się mapą GPIO, magistralami, złączami, LCD, enkoderem i zasilaniem. Nazwa rodziny ani rewizji nie jest capability.

| Family | Prototype | MCU | Current role |
|---|---|---|---|
| A | A0 | ESP32-S3 N16R8 | VoxOne, główny prototyp z LCD ST7796S |
| B | B0 | ESP32-S3 Zero | VoxOne, kompaktowy prototyp bez LCD |
| C | C0 | ESP32-S3 Zero | VoxOne, przyszły prototyp przenośny |
| D | D0 | ESP32-S3 Zero | VoxOne, przyszły prototyp głosowy |
| X | X0 | klasyczny ESP32 | VoxOne, aktywny prototyp legacy |
| V | V0 | Wemos D1 mini ESP32 | VoxOneBT, prototyp w osobnym repozytorium |

Pierwsze finalne PCB to odpowiednio A1, B1, C1, D1, X1 i V1. Aktywne środowiska PlatformIO w tym repozytorium to `a0`, `b0`, `x0`. C0/D0 nie mają jeszcze buildów ani potwierdzonych map GPIO; V0 należy do repozytorium VoxOneBT. Nazwa produktu VoxOneBT pozostaje bez zmian.

Jednorazowa uwaga migracyjna: A0 = formerly SALON, B0 = formerly DIN, X0 = formerly DESK. Nazwy sprzed migracji nie są już identyfikatorami buildów ani nazwami pokazywanymi użytkownikowi.

## Aktywne prototypy

| Target | Potwierdzone elementy |
|---|---|
| X0 | ESP32, ST7789 284×76, enkoder, PCM5102A, bez BT i PSRAM |
| B0 | ESP32-S3 Zero, bez LCD i enkodera, PCM5102A, VoxOneBT UART; BT I2S piny zarezerwowane, RX wyłączony |
| A0 | ESP32-S3 N16R8, ST7796S 480×320, enkoder, PCM5102A, DS3231/I2C, VoxOneBT UART i BT I2S RX |

`src/hardware/hardware_descriptor.h` oddziela tożsamość PCB (`family`, `revision`, `name`, MCU), magistrale, GPIO i capabilities. `kNoPin` oznacza brak przypisania; `fromLegacyPin` tłumaczy dawne `255`/`-1`. Zero w rozmiarze pamięci oznacza brak potwierdzonej wartości, nie brak fizycznej pamięci. Runtime korzysta z descriptora dla DAC mute, RTC, BT UART/I2S, enkodera i pinów LCD; część modułów nadal korzysta z `profiles/` i Config. Różnice A0→A1, B0→B1 itd. powinny mieścić się w descriptorze/profilu, bez rozgałęzień logiki funkcjonalnej według rewizji.

X0 nie deklaruje pinów domyślnego SPI. A0 deklaruje SCK 12, MOSI 11, MISO 13 jako jedną magistralę; sterownik LCD nadal używa domyślnej inicjalizacji SPI. Podświetlenie A0 to GPIO14 sterowane przez Config. DS3231 i przyszłe urządzenie I2C mogą współdzielić SDA/SCL. XSMT A0 nie ma przypisanego GPIO. Fizyczny DSP i MAX98357 są wyłączone na aktualnych trzech targetach. Niekompletny wariant `a0_dsp` nie jest targetem weryfikacyjnym i nie ma potwierdzonej mapy pinów.

Capabilities, a nie oznaczenie A/B/C/D/X lub numer rewizji, decydują o dostępności LCD, BT, PSRAM, DSP, RTC i przyszłych funkcji. Przykładowo A0 ma LCD/BT/RTC i zasoby PSRAM, B0 ma BT i PSRAM bez LCD, a X0 nie ma PSRAM. `supportsVoxOneBt` opisuje możliwość PCB, podczas gdy `btEnabled`, `btOnline` i `btConnected` to odrębne stany runtime. Start z BT OFF nie uruchamia UART ani I2S; późniejsze fizyczne hot-start wymaga osobnej obsługi. Nie należy wywodzić nowej funkcji tylko z `family == 'A'`.

## Aktualizacja VoxOneBT

Rodziny Ax/Bx/Cx/Dx mogą aktualizować VoxOneBT przez `WWW → MAIN → UART → VoxOneBT` wyłącznie gdy konkretny profil ma moduł VoxOneBT, jawne capability aktualizacji i wystarczające zasoby PSRAM/staging. Sama rodzina S3 nie daje takiej zgody. BT-FW-5 dodaje staging i upload dla obecnych A0/B0, z obowiązkowym sprawdzeniem zasobów runtime; nie oznacza to potwierdzenia PSRAM na fizycznym B0. Dla Xx aktualizacja VoxOneBT odbywa się wyłącznie bezpośrednio przez USB. Nie ma stagingu dla Xx.

## Planowane PCB i LCD

A1 może używać PCM5102A lub DSPmini; B1 jest kompaktowe; C1 może używać PCM5102A albo MAX98357; D1 jest wariantem głosowym. Ich mapy GPIO, peryferia i capabilities wymagają potwierdzenia. Planowane klasy LCD: wspólne 128×64 dla SSD1306/SH1106, osobne SSD1322 256×64, GC9A01 240×240, ST7789 320×240 i ST7796S 480×320. SSD1309 i utrzymanie legacy ST7789 284×76 wymagają osobnej decyzji. Ten etap nie dodaje DisplayManagera ani sterowników.
