# VoxOne

VoxOne to firmware radia sieciowego dla klasycznego ESP32 i ESP32-S3. Bieżąca
linia rozwojowa ma wersję `0.8.0-dev`; Stable 1.0.0 nie został jeszcze wydany.
Projekt rozwija bazę yoRadio, zachowując jej atrybucję i zgodność tam, gdzie
jest ona nadal częścią działającego kodu.

## Obsługiwane profile builda

| Profil | Platforma i bieżący display | Stan kwalifikacji |
|---|---|---|
| `w0` | ESP32, ST7789 284×76 | build objęty gate'em; końcowy HIL Stable oczekuje |
| `m0` | ESP32-S3 4 MiB/PSRAM, runtime SSD1306 lub SSD1309 128×64 | częściowe fizyczne baseline'y istnieją; końcowy HIL Stable oczekuje |
| `l0` | ESP32-S3 N16R8, ST7796S 480×320 | etapowe fizyczne baseline'y istnieją; końcowy HIL Stable oczekuje |
| `s0` | ESP32-S3 4 MiB/PSRAM, tymczasowy kontrakt rewizji 0 zgodny z M0 | build PASS nie zastępuje HIL na sprzęcie S0 |
| `din0` | ESP32-S3 4 MiB/PSRAM, tymczasowy kontrakt rewizji 0 zgodny z M0 | build PASS nie zastępuje HIL na sprzęcie DIN0 |

S1, DIN1, XL i dodatkowe wyświetlacze opisane w roadmapie nie są capability
obecnych obrazów. Dokładny pinout i granice wsparcia zawiera
[dokument sprzętowy](docs/HARDWARE_TARGETS.md).

## Bieżący zakres

Firmware zawiera RADIO przez I2S, panel WWW i WebSocket, listę stacji,
konfigurację runtime obsługiwanych display/input, Config v7, backup/restore
`.vxcf`, MQTT, lokalny Web Update, Recovery AP i obrazy z rollbackiem OTA.
L0 integruje również zewnętrzny moduł VoxOneBT. Wspólny `ScrollGroupEngine`
jest obecnie aktywny tylko w M0; L0 i W0 zachowują własne działające
schedulery do czasu oddzielnej migracji i HIL.

Produkcyjny enforcement SECURITY pozostaje wyłączony (`observe-only`). Jego
kandydat i integracja OTA są zabezpieczonym WIP, a nie częścią gotowego
wydania. Online Update, Spotify Connect, DLNA, DSPmini, Multiroom, natywne HA
oraz Installer są pracami przyszłymi. Kanoniczny i aktualny zakres otwartych
prac znajduje się wyłącznie w [docs/TODO.md](docs/TODO.md).

## Build i walidacja

Wymagane są PlatformIO, Python 3, PowerShell (`pwsh`) oraz kompilator C++ dla
testów native. Przykładowy build profilu:

```sh
pio run -e l0
```

Pełny software gate (testy native, sanitizery, headless WWW, spójność siedmiu
gzipów i pięć buildów):

```sh
pwsh ./tools/verify.ps1
```

Kompletny obraz instalacyjny buduje target `fullimage`, np.:

```sh
pio run -e l0 -t fullimage
```

Samo powodzenie builda nie jest HIL PASS. Układ partycji, przeznaczenie full
image i zasady instalacji opisuje [docs/FULL_IMAGE.md](docs/FULL_IMAGE.md), a
bramki wydania [docs/STABLE_CONTRACT.md](docs/STABLE_CONTRACT.md).

## Zasoby WWW i bezpieczeństwo pracy

Źródła panelu znajdują się w `web-src/`; build odtwarza deterministyczne
zasoby `data/www/*.gz`. Nie należy edytować gzipów ręcznie ani używać obrazu
innego profilu. Pełna instalacja może skasować NVS, Wi-Fi, `otadata` i
SPIFFS, dlatego wymaga właściwego backupu i osobno zatwierdzonej procedury.

Nie commituj haseł, tokenów, zrzutów Flash ani logów zawierających dane
prywatne. Repozytorium jest udostępniane na
warunkach [GPLv3](LICENSE); zależności zachowują własne licencje.
