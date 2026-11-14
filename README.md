# VoxOne

VoxOne to firmware radia sieciowego dla ESP32-S3. Bieżąca
linia rozwojowa ma wersję `1.0.0-rc2`; Stable 1.0.0 nie został jeszcze wydany.
Projekt rozwija bazę yoRadio, zachowując jej atrybucję i zgodność tam, gdzie
jest ona nadal częścią działającego kodu.

## Obsługiwane profile builda

| Profil | Platforma i bieżący display | Rola Stable 1.0.0 |
|---|---|---|
| `m0` | ESP32-S3 N16R8 DevKit, runtime OLED | zachowany standardowy build i wcześniejsze baseline'y; fizyczny HIL nieblokujący |
| `l0` | istniejąca PCB V-Tom, ESP32-S3 N16R8, ST7796S 480×320, VoxOneBT | obowiązkowy fizyczny HIL; hardware i GPIO bez zmian |
| `s0` | ESP32-S3 Zero 4 MiB/2 MiB, własny pinout rev. 0, OLED SSD1306; docelowo VoxOneBT | obowiązkowy fizyczny HIL; BT i jego GPIO jeszcze niewdrożone/niezatwierdzone, VU oraz MAIN→BT update wyłączone |
| `din0` | ESP32-S3 Zero 4 MiB/2 MiB, bazowy pinout wspólny z S0 | zachowany standardowy build; fizyczny HIL nieblokujący |

S1, DIN1, XL i dodatkowe wyświetlacze opisane w roadmapie nie są capability
obecnych obrazów. Dokładny pinout i granice wsparcia zawiera
[dokument sprzętowy](docs/HARDWARE_TARGETS.md).

## Bieżący zakres

Firmware zawiera RADIO przez I2S, panel WWW i WebSocket, listę stacji,
konfigurację runtime obsługiwanych display/input, Config v7, backup/restore
`.vxcf`, MQTT, lokalny Web Update, Recovery AP i obrazy z rollbackiem OTA.
L0 integruje również zewnętrzny moduł VoxOneBT. S0+VoxOneBT jest zatwierdzonym
celem Stable, ale bieżący profil S0 nadal ma BT wyłączone; moduł będzie
aktualizowany przez osobne USB. Wspólny `ScrollGroupEngine`
jest obecnie aktywny w M0 i L0; S0 i DIN0 zachowują dotychczasowe
mechanizmy do czasu oddzielnej migracji i HIL.

Formalny release gate obejmuje 12 grup: obowiązkowe profile S0 i L0 oraz
dziesięć wspólnych grup. M0 i DIN0 pozostają częścią czteroprofilowego
software gate, ale nie wymagają końcowego fizycznego HIL przed Stable.

Standardowe profile M0/L0/S0/DIN0 mają włączony produkcyjny enforcement
SECURITY: OPEN działa bez hasła, a PROTECTED wymaga ważnej sesji dla
chronionego HTTP/API i WebSocket. Software gate ma PASS, natomiast końcowy HIL
produkcyjnej ochrony pozostaje wymagany. Online Update, DLNA, DSPmini,
Multiroom, natywne HA oraz Installer są pracami przyszłymi.
Kanoniczny i aktualny zakres otwartych prac znajduje się wyłącznie w
[docs/TODO.md](docs/TODO.md).

## Build i walidacja

Wymagane są PlatformIO, Python 3, PowerShell (`pwsh`) oraz kompilator C++ dla
testów native. Przykładowy build profilu:

```sh
pio run -e l0
```

Pełny software gate (testy native, sanitizery, headless WWW, spójność zasobów
i cztery standardowe buildy):

```sh
pwsh ./tools/verify.ps1
```

Kompletny obraz instalacyjny buduje target `fullimage`, np.:

```sh
pio run -e l0 -t fullimage
```

Samo powodzenie builda nie jest HIL PASS. Układ partycji, przeznaczenie full
image, FIRST BOOT, WWW, aktualizację, backup i SECURITY Recovery opisuje
[instrukcja instalacji i odzyskiwania](docs/FULL_IMAGE.md), a bramki wydania
[docs/STABLE_CONTRACT.md](docs/STABLE_CONTRACT.md).

## Zasoby WWW i bezpieczeństwo pracy

Źródła panelu znajdują się w `web-src/`; build odtwarza deterministyczne
zasoby `data/www/*.gz`. Nie należy edytować gzipów ręcznie ani używać obrazu
innego profilu. Pełna instalacja może skasować NVS, Wi-Fi, `otadata` i
SPIFFS, dlatego wymaga właściwego backupu i osobno zatwierdzonej procedury.

Nie commituj haseł, tokenów, zrzutów Flash ani logów zawierających dane
prywatne. Repozytorium jest udostępniane na
warunkach [GPLv3](LICENSE); zależności zachowują własne licencje.
