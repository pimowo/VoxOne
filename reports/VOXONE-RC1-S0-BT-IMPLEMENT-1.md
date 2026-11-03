# RC1-S0-BT-IMPLEMENT-1

Data: 2026-11-03

Repozytorium: `/home/piotrek/projects/VoxOne`

## Werdykt

**SOFTWARE PASS / PHYSICAL S0 HIL PENDING**

Minimalna obsługa VoxOneBT została włączona dla S0 zgodnie z aktualnym
kontraktem Stable 1.0.0. Pełna weryfikacja offline, sanitizery, buildy S0/L0
i walidacja pełnych obrazów mają PASS. Nie wykonano OTA, flashowania,
restartu ani fizycznego testu urządzenia, dlatego raport nie stanowi
fizycznego S0 HIL PASS ani zatwierdzenia kandydata wydania.

## Preflight i stan drzewa

- Branch: `main`.
- HEAD przed i po pracy: `8ff87067f40610b851fc4491557a4c2c59df2908`, zgodny z podanym baseline.
- Build identity artefaktów: `8ff8706-dirty`, kanał `dev`, security
  `production`, wersja `0.8.0`.
- Przed edycją istniały zachowane modyfikacje `docs/TODO.md`,
  `test/bt_volume_native.cpp`, `tools/verify.ps1` oraz nieśledzone raporty
  HIL. Nie zostały cofnięte, nadpisane ani przypisane temu zadaniu.
- Odczytano lokalne `docs/TODO.md`, `docs/STABLE_CONTRACT.md`, instrukcje
  `AGENTS.md` oraz
  `/home/piotrek/voxone-hil/RC1-S0-BT-PREFLIGHT-1/REPORT.md`.
- Nie wykonano commit, tag ani push.

## Zakres implementacji

### Profil S0 i capability

S0 zachowuje ESP32-S3 Zero, 4 MiB Flash, deklarowane 2 MiB PSRAM, SSD1306
128x32, EC11 i wyjście I2S0 MAX98357A. Dodano dokładnie wymagane połączenia:

| Funkcja | GPIO S0 |
|---|---:|
| VoxOneBT UART1 RX | 10 |
| VoxOneBT UART1 TX | 11 |
| I2S1 RX BCLK | 12 |
| I2S1 RX WS | 13 |
| I2S1 RX DATA | 14 |

`VOXONE_HAS_BT` i odbiór I2S1 aktywują istniejące `BtLink`,
`BtAudioInput` oraz `SourceManager`; protokół UART 2 nie został zmieniony.
VU, RTC, DSP i nowe ekrany pozostają wyłączone. Istniejące GPIO I2S0
5/6/4, I2C 1/2 oraz EC11 9/8/7 nie zostały zmienione.

### Audio BT a updater modułu

Rozdzielono dwie niezależne capability:

- S0: BT audio włączone, `VOXONE_BT_FW_UPDATE_ENABLED=0`;
- L0: BT audio oraz dotychczasowy updater pozostają włączone;
- niezależna flaga widoku `BT_TRANSPORT` jest wyłączona dla małego OLED S0
  i zachowana dla L0/ST7796.

Rejestracja `GET /api/bt/update`, obsługa `POST /update/bt`, staging i stan
uploadu są na S0 wycięte na etapie kompilacji. Kontrola symboli ELF nie
znalazła w S0 `BtFirmwareStaging`, `handleBtUpload` ani
`activeBtUploadRequest`; te symbole nadal występują w L0. Wspólne assety WWW
i polityka bezpieczeństwa mogą nadal zawierać tekstowe nazwy URL, ale S0 nie
rejestruje tras updatera, więc endpointy są niedostępne zgodnie z capability.

### Config v7

Format binarny Config v7, jego rozmiar i VXCF nie zostały zmienione.
Wprowadzono osobny marker NVS `voxcap/s0bt1` z następującą polityką:

1. Tylko dokładny profil S0 z BT i brak markera uruchamia migrację
   wprowadzenia capability.
2. Dawny zapis `btEnabled=0` jest dla tego jednego przypadku włączany w
   bieżącym starcie i trwale zapisywany jako `true`.
3. Marker jest zapisywany dopiero po poprawnym, zweryfikowanym zapisie
   Config v7; awaria nie publikuje fałszywego sukcesu.
4. Po markerze każde późniejsze świadome `btEnabled=0` pozostaje skuteczne.
5. VXCF nie przenosi markera. Restore wykonany po migracji może więc
   świadomie przywrócić `false`, które nie zostanie ponownie nadpisane.
6. OTA zachowuje NVS i marker; starszy rollback ignoruje obcy namespace.
7. Dla niedostępnego lub niepoprawnego markera oraz przyszłej wersji Config
   kod nie zgaduje intencji i zachowuje zapisane ustawienie.

To jest jawna polityka rozwiązująca nierozróżnialność historycznego `false`
od hipotetycznego świadomego wyłączenia sprzed wprowadzenia capability.
Jest idempotentna, ale trwałość na rzeczywistym NVS nadal wymaga HIL.

### UI i routing PCM

- S0 nie może wejść do nieobsługiwanego widoku `BT_TRANSPORT` ani z EC11,
  ani przez programatyczną zmianę stanu.
- Widok PLAYER, wybór BT, PLAY/PAUSE i przełączanie RADIO/BT nadal używają
  istniejącej ścieżki wspólnej.
- Odbiór stereo S16 z VoxOneBT i przekazanie do istniejącego I2S0 przeszły
  testy ścieżki programowej. Nie stanowi to dowodu elektrycznego ani
  fizycznego odsłuchu mono MAX98357A.
- Zachowanie L0 z dedykowanym widokiem BT i updaterem pozostało aktywne.

## Zmienione pliki

Kod i profile:

- `profiles/s0.h`
- `profiles/l0.h`
- `profiles/unavailable_hardware.h`
- `profiles/profile_checks.h`
- `src/hardware/hardware_descriptor.cpp`
- `src/core/bt_firmware_staging.cpp`
- `src/core/config.cpp`
- `src/core/config_capability_migration.h`
- `src/core/config_capability_migration.cpp`
- `src/core/netserver.cpp`
- `src/core/ui_input.h`
- `src/core/ui_input.cpp`
- `src/core/ui_state.cpp`

Testy:

- `test/bt_update_profile_contract_native.cpp`
- `test/config_capability_migration_native.cpp`
- `test/config_persistence_native.cpp`
- `test/bt_transport_controls_native.cpp`
- `test/ec11_direction_native.cpp`
- `test/hardware_descriptor_native.cpp`
- `test/local_input_activation_native.cpp`
- `test/ui_core_state_native.cpp`
- `test/ui_input_events_native.cpp`

Dokumentacja stanu po ocenie wyników:

- `docs/TODO.md`
- `reports/VOXONE-RC1-S0-BT-IMPLEMENT-1.md`

Zastane zmiany w `test/bt_volume_native.cpp`, `tools/verify.ps1` i innych
raportach nie należą do tej implementacji.

## Wyniki testów offline

| Zakres | Wynik |
|---|---|
| Hardware descriptors W0/M0/S0/DIN0/L0, GPIO i capability | PASS |
| Config v7: plan migracji, trwałość, restart, świadome disable i restore | PASS |
| BT/UART/I2S/SourceManager/TTS oraz UI/local input | PASS |
| Brak updatera i widoku BT_TRANSPORT w S0, zachowanie ich w L0 | PASS |
| Native suite | **160/160 PASS** |
| Strict radio transport | **16/16 PASS** |
| ASan/UBSan | PASS |
| Web Update headless i offline HIL diagnostics | PASS |
| Build S0 i L0 | PASS |
| `tools/verify.ps1` — pełny przebieg końcowy | **PASS** |
| Full images W0/M0/S0/DIN0/L0 | PASS |
| Negatywna walidacja full image | **8/8 PASS** |
| L0 TX-only i WWW repeatability | PASS |
| `git diff --check` | PASS |

Nie uruchomiono żadnego obciążenia ani testu na urządzeniu.

## Zużycie zasobów i artefakty

### S0

| Zasób | Wynik |
|---|---:|
| Statyczny RAM | 81932 / 327680 B = 25.0% |
| Program wg PlatformIO | 1685145 / 1966080 B = 85.7% |
| Rzeczywisty `firmware.bin` w slocie OTA | 1685568 / 1966080 B = 85.73% |
| Wolne w slocie OTA | 280512 B |
| SPIFFS | 196608 B |
| Full image | 4194304 B |

SHA-256 S0:

- `firmware.bin`:
  `c0653f941eb1da4c1bdff6306736a0b7b21f55bec0b9abc88b30c813482bb20d`
- `firmware.elf`:
  `466fc4c93f1a6b0fb93913ba8c1cad0fa530792499f20491a401dc842f636e42`
- `spiffs.bin`:
  `b95578af4a8cf209c97d21918e8ac50d5e815e06f79f72c395cda0dfebc45ae2`
- `VoxOne-0.8.0-full.bin`:
  `d0cc68e0dc926be4e83f2c8b6ffb016055f39c06f16f04b425a03f5d8db12407`
- full-image `artifactSetId`:
  `bb2c88ca8bd498e2e339901d99cf5993df5a8c593786ebdef6604a11fbd8166e`

Deklarowane 2 MiB PSRAM są częścią profilu i flag builda, ale rzeczywistego
`total/free/min` PSRAM nie można uczciwie zmierzyć bez uruchomienia fizycznego
S0. Updater/staging BT nie jest linkowany do S0. Runtime footprint zadań,
buforów UART i DMA należy zmierzyć w HIL.

### Regresja L0

L0 przeszedł build, pełny verify i walidację full image. Statycznie:

- RAM: 82216 / 327680 B = 25.1%;
- program wg PlatformIO: 1764321 / 1966080 B = 89.7%;
- `firmware.bin`: 1764736 B, SHA-256
  `87cc79650f5d6e2a924903ef944f119a98fca4d539b1fec721547c35e442502b`;
- full image: 16777216 B, SHA-256
  `79c8af18bf7f67013abc450d5fa55e7e6569d5d89b965b556f5d9181bececab0`.

Nie zmieniono pinów L0, protokołu VoxOneBT ani semantyki jego updatera.
Jest to regresja software, nie nowy fizyczny L0 HIL.

## QIO/DIO

`platformio.ini` deklaruje dla S0 `qio`, `qio_qspi` i PSRAM `qio`, natomiast
końcowy manifest walidacyjny oraz builder full image zapisują
`flashMode=dio`, 80 MHz. Obraz przeszedł kontrolę formatu, checksum i
embedded ELF SHA. Konfiguracji flash nie zmieniono bez dowodu sprzętowego.
Rozbieżność narzędziowa pozostaje jawnie otwarta i musi zostać zweryfikowana
na dokładnym module przed uznaniem HIL.

## Otwarte problemy i warunki gotowości do HIL

Kod jest gotowy do przygotowania osobnego, czystego kandydata S0, ale przed
fizycznym PASS wymagane są:

1. Przebudowanie dokładnego czystego commita i zapisanie nowych SHA; obecne
   artefakty mają celowo identyfikator `dirty`.
2. Potwierdzenie fizycznego ESP32-S3 Zero 4 MiB/2 MiB, rewizji płytki,
   wspólnej masy, poziomów 3.3 V i dokładnego okablowania GPIO 10-14 bez
   konfliktu z USB, boot-strappingiem, OLED, EC11 i I2S0.
3. Boot/READY bez reset loop oraz runtime odczyt Flash, PSRAM total/free/min,
   heap i stosów; rozstrzygnięcie QIO/DIO.
4. VoxOneBT online z protokołem 2, poprawny UART RX/TX i I2S1 clock/rate;
   brak updatera MAIN→BT w capability, UI i odpowiedziach HTTP.
5. RADIO -> BT -> RADIO, connect/disconnect/reconnect, PLAY/PAUSE, naturalny
   fallback i pełne snapshoty SourceManager bez panic/assert/WDT.
6. Fizyczny odsłuch MAX98357A, poprawność mono i brak trzasków, przy czym
   VU ma pozostać nieobecne.
7. OLED/EC11: PLAYER działa, a żadne wejście nie otwiera BT_TRANSPORT.
8. Config v7 na kontrolowanej kopii: legacy `btEnabled=0` migruje raz,
   restart i MAIN-only OTA nie powtarzają migracji, późniejsze świadome
   disable oraz VXCF restore pozostają skuteczne. Test wymaga backupu i
   bezpiecznej ścieżki recovery.
9. Pełna obowiązkowa macierz S0 z `docs/STABLE_CONTRACT.md`, w tym
   update/recovery/rollback, na dokładnym zatwierdzonym BIN.

Do tego czasu `RC1-PROFILE-S0` pozostaje **OPEN / PARTIAL**.

## Ograniczenia wykonania

- Bez OTA, flashowania, restartu i zmiany konfiguracji urządzeń.
- Bez zmian firmware VoxOneBT.
- Bez instalacji pakietów.
- Bez fizycznego HIL i bez deklarowania odsłuchu.
- Bez commitów i push.
- Zastane niezapisane zmiany zachowano.
