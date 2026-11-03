# VOXONE — RC1-S0-BT-CANDIDATE-PREPARE-1

Data: 2026-11-03 (Europe/Warsaw)

Repozytorium: `/home/piotrek/projects/VoxOne`

## Werdykt

**BLOCKED — source set jest rozdzielony i gotowy do checkpointu, ale nie
istnieje jeszcze czysty commit ani zbudowany z niego artefakt HIL.**

Ten werdykt zachowuje stan z chwili audytu PREPARE. Późniejsze informacje o
checkpoincie P0 i 27-plikowym manifeście P1 są dopiskiem kontrolnym; wynik
zadania `RC1-S0-BT-CHECKPOINT-1` wymaga osobnego raportu po clean verify.

Blokada jest proceduralna i sprzętowa, nie kompilacyjna:

1. implementacja S0 współistnieje z wcześniejszymi niezapisanymi zmianami;
2. bieżące BIN-y mają identity `8ff8706-dirty` i nie mogą być kandydatem HIL;
3. `docs/HARDWARE_TARGETS.md` i część opisowa `docs/STABLE_CONTRACT.md` nadal
   opisują stan sprzed implementacji i wymagają wąskiej aktualizacji przed
   checkpointem;
4. przed flashowaniem trzeba fizycznie potwierdzić model/SKU S0, strap
   `SD_MODE` MAX98357A, zasilanie, okablowanie i działającą drogę recovery.

Nie wykonano `git add`, commit, reset, clean, checkout, OTA, flashowania,
restartu ani operacji na urządzeniu.

## A. Stan repozytorium i pełna inwentaryzacja

- Branch: `main`.
- HEAD: `8ff87067f40610b851fc4491557a4c2c59df2908`.
- HEAD nie zmienił się podczas audytu.

### Zastane przed RC1-S0-BT-IMPLEMENT-1 — tracked modified

Te trzy pliki były zmienione przed rozpoczęciem implementacji S0:

- `docs/TODO.md` — zawiera wcześniejsze uzgodnienia i dowody HIL; obecnie ma
  także późniejsze, celowane hunki S0, więc nie wolno traktować całego pliku
  jako jednorodnej zmiany implementacji;
- `test/bt_volume_native.cpp` — wcześniejsza regresja toru volume/VU;
- `tools/verify.ps1` — wcześniejsze dodanie `volume_map.cpp` jako zależności
  powyższego testu.

### Zastane przed implementacją — untracked reports

1. `reports/VOXONE-BT-A2DP-WWW-DEPLOY-1.md`
2. `reports/VOXONE-BT-AUDIO-PROBE-WWW-RETRY-1.md`
3. `reports/VOXONE-BT-VOLUME-VU-PHYSICAL-PROBE-1.md`
4. `reports/VOXONE-BT-VOLUME-VU-ROOT-CAUSE-1.md`
5. `reports/VOXONE-L0-AHFM-REMOTE-HIL-2.md`
6. `reports/VOXONE-L0-AHFM-REMOTE-PHYSICAL-DIAG-1.md`
7. `reports/VOXONE-L0-BT-A2DP-LIVE-SMOKE-1.md`
8. `reports/VOXONE-L0-BT-LIFECYCLE-FINAL-HIL-1.md`
9. `reports/VOXONE-L0-BT-LIFECYCLE-HIL-1.md`
10. `reports/VOXONE-L0-BT-PAUSE-RESUME-FINAL-1.md`
11. `reports/VOXONE-L0-BT-STRESS-V2-FINAL-1.md`
12. `reports/VOXONE-L0-BT-STRESS-VU-HIL-1.md`
13. `reports/VOXONE-L0-BT-VU-RESUME-ROOT-CAUSE-1.md`
14. `reports/VOXONEBT-PAIRING-HCI-CAPTURE-1.md`

### RC1-S0-BT-IMPLEMENT-1 — tracked modified

Produkcja/profile:

- `profiles/l0.h`
- `profiles/profile_checks.h`
- `profiles/s0.h`
- `profiles/unavailable_hardware.h`
- `src/core/bt_firmware_staging.cpp`
- `src/core/config.cpp`
- `src/core/netserver.cpp`
- `src/core/ui_input.cpp`
- `src/core/ui_input.h`
- `src/core/ui_state.cpp`
- `src/hardware/hardware_descriptor.cpp`

Testy:

- `test/bt_transport_controls_native.cpp`
- `test/config_persistence_native.cpp`
- `test/ec11_direction_native.cpp`
- `test/hardware_descriptor_native.cpp`
- `test/local_input_activation_native.cpp`
- `test/ui_core_state_native.cpp`
- `test/ui_input_events_native.cpp`

### RC1-S0-BT-IMPLEMENT-1 — untracked

- `src/core/config_capability_migration.cpp`
- `src/core/config_capability_migration.h`
- `test/bt_update_profile_contract_native.cpp`
- `test/config_capability_migration_native.cpp`
- `reports/VOXONE-RC1-S0-BT-IMPLEMENT-1.md`

Niniejszy raport jest dodatkowym plikiem untracked:
`reports/VOXONE-RC1-S0-BT-CANDIDATE-PREPARE-1.md`.

### Zależności i ryzyko pominięcia

| Grupa | Pliki, które muszą pozostać razem | Skutek pominięcia |
|---|---|---|
| Capability profilu | `s0.h`, `l0.h`, `unavailable_hardware.h`, `profile_checks.h` | Brak flagi domyślnej, utrata updatera L0 albo niespójne reguły compile-time. |
| Deskryptor | `s0.h`, `hardware_descriptor.cpp`, test deskryptora | Makra mogłyby kompilować BT, lecz runtime/API nadal zgłaszałyby stare piny lub capability. |
| Brak updatera S0 | `l0.h`, `s0.h`, `netserver.cpp`, `bt_firmware_staging.cpp` | Możliwa aktywna trasa uploadu, linkowanie stagingu albo regresja updatera L0. |
| Migracja Config | `config.cpp`, oba `config_capability_migration.*`, dwa testy Config | Historyczne `btEnabled=0` blokowałoby BT albo późniejsze świadome wyłączenie byłoby nadpisywane. |
| Ochrona OLED | `ui_input.h`, `ui_input.cpp`, `ui_state.cpp` i testy UI | S0 mógłby wejść do nierenderowanego `BT_TRANSPORT`. |
| Dowód | dziewięć testów S0 | Build mógłby przejść bez regresji policy/API/persistence. |

Zastane `test/bt_volume_native.cpp` i `tools/verify.ps1` nie są zależnością
produkcji S0. Są jednak wzajemnie zależne jako wcześniejszy test i powinny
trafić do osobnego wcześniejszego checkpointu albo oba pozostać poza
checkpointem S0. Czternaście wcześniejszych raportów również nie jest
zależnością BIN, lecz nie wolno ich zgubić.

## Dokładny podział checkpointów

### Checkpoint P0 — wcześniejsze dowody i wcześniejszy test

Po osobnej kontroli właściciela został utworzony jako
`049cb026695f6afedb46f3fe01d07aa2ee4e290d` i zawiera dokładnie:

- wcześniejsze hunki `docs/TODO.md` niezwiązane z S0;
- `test/bt_volume_native.cpp`;
- `tools/verify.ps1`;
- czternaście wymienionych wyżej raportów HIL.

Zmiany te nie są częścią checkpointu S0; P0 jest jego jednoznaczną bazą.

### Checkpoint P1 — S0+VoxOneBT

Manifest P1 zawiera 27 plików:

- wszystkie 13 plików produkcyjnych/profilowych z grup capability,
  deskryptora, updatera, Config i UI wymienionych wyżej;
- wszystkie dziewięć testów implementacji S0;
- wyłącznie hunki S0 w `docs/TODO.md`;
- `reports/VOXONE-RC1-S0-BT-IMPLEMENT-1.md`;
- niniejszy raport;
- wąskie aktualizacje `docs/HARDWARE_TARGETS.md` i
  `docs/STABLE_CONTRACT.md`: bieżący S0 ma BT/audio RX na GPIO10–14,
  MAIN→BT update i VU pozostają wyłączone, software PASS, physical HIL
  pending. Nie zmieniać kryteriów Stable.

Pierwotna lista 23 plików implementacji/testów/raportu IMPLEMENT została
jawnie rozszerzona o `docs/TODO.md`, `docs/HARDWARE_TARGETS.md`,
`docs/STABLE_CONTRACT.md` i niniejszy raport. SHA-256 tekstowej, posortowanej
listy 27 ścieżek manifestu wynosi:

`437300dfd7c057b4521a3d857324071c94f5c303a408fa3b99a891895d88735c`

Dokładna lista ścieżek użyta do obliczenia tego hasha:

```text
docs/HARDWARE_TARGETS.md
docs/STABLE_CONTRACT.md
docs/TODO.md
profiles/l0.h
profiles/profile_checks.h
profiles/s0.h
profiles/unavailable_hardware.h
reports/VOXONE-RC1-S0-BT-CANDIDATE-PREPARE-1.md
reports/VOXONE-RC1-S0-BT-IMPLEMENT-1.md
src/core/bt_firmware_staging.cpp
src/core/config.cpp
src/core/config_capability_migration.cpp
src/core/config_capability_migration.h
src/core/netserver.cpp
src/core/ui_input.cpp
src/core/ui_input.h
src/core/ui_state.cpp
src/hardware/hardware_descriptor.cpp
test/bt_transport_controls_native.cpp
test/bt_update_profile_contract_native.cpp
test/config_capability_migration_native.cpp
test/config_persistence_native.cpp
test/ec11_direction_native.cpp
test/hardware_descriptor_native.cpp
test/local_input_activation_native.cpp
test/ui_core_state_native.cpp
test/ui_input_events_native.cpp
```

Hash kontrolny zawartości pierwotnych 23 plików implementacji/testów/raportu IMPLEMENT,
liczony jako SHA-256 tekstowej listy `sha256sum` w podanej kolejności, wynosi:

`85dc1fe04b1131c9bd4e6d7538d6516f909dc1e568c7784075a6f9b14b63b40b`

To hash roboczego source setu, nie commit ani release identity.

## B. Weryfikacja techniczna

### Deskryptor, capability i GPIO

| Właściwość | S0 | L0/regresja |
|---|---|---|
| MCU | ESP32-S3 | bez zmian |
| Flash / PSRAM | 4 MiB / 2 MiB | L0 bez zmian |
| VoxOneBT | `true` | `true` |
| BT audio RX | `true` | `true` |
| MAIN→BT update | `false` | `true` |
| BT_TRANSPORT UI | `false` | `true` |
| VU / RTC / DSP | `false / false / false` | bez zmian |

Mapa S0 jest rozłączna:

| Magistrala | Sygnały | GPIO |
|---|---|---|
| I2C OLED | SDA/SCL | 1/2 |
| I2S0 MAX98357A | BCLK/DOUT/WS | 5/6/4 |
| EC11 | A/B/KEY | 9/8/7 |
| UART1 VoxOneBT | RX/TX | 10/11 |
| I2S1 RX VoxOneBT | BCLK/WS/DATA | 12/13/14 |

`hasPinConflicts()` obejmuje oba I2S, UART, I2C, input i display, a test S0
potwierdza dokładne wartości. GPIO10–14 nie są strapami, USB-JTAG ani liniami
SPI0/1. Oficjalna dokumentacja ESP32-S3 wskazuje jako strapy GPIO0/3/45/46,
USB-JTAG na 19/20 oraz SPI0/1 przede wszystkim na 26–32; GPIO33–37 dotyczy
wariantów Octal. Źródło:
[Espressif GPIO & RTC GPIO](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html).

UART i audio są rozdzielone także kontrolerami:

- `BtLink`: `HardwareSerial(1)`, 921600 8N1, GPIO10/11;
- `BtAudioInput`: `I2S_NUM_1`, slave RX, stereo S16, GPIO12/13/14;
- Player/MAX98357A: `I2S_NUM_0`, master TX, GPIO5/4/6.

### MAX98357A i kontrakt mono

S0 inicjalizuje I2S0 przez istniejący `Audio`/Player i wysyła standardowe
interleaved stereo (`I2S_CHANNEL_FMT_RIGHT_LEFT`). `BtAudioInput` kopiuje
odebrane stereo S16 do I2S0 bez programowego downmixu. Nazwa
`MAX98357A MONO` oznacza jeden fizyczny wzmacniacz/moduł, nie wymuszone
software `forceMono`; `PLAYER_FORCE_MONO` pozostaje `false`.

To jest poprawny kontrakt pod warunkiem właściwego strapu `SD_MODE` modułu.
Według datasheet MAX98357A właśnie `SD_MODE` wybiera LEFT, RIGHT albo
`left/2 + right/2`; firmware nie może ustalić połączenia rezystora na
breakoucie. Źródło:
[Analog Devices MAX98357A datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf).

Wniosek: build potwierdza poprawne I2S, ale **mono mix pozostaje fizycznie
UNVERIFIED**. Przed HIL trzeba sfotografować/zmierzyć strap i użyć krótkiego
pliku L-only, R-only oraz both. Nie wolno uznać pojedynczego odsłuchu muzyki
za dowód sumowania obu kanałów.

### Config v7 i marker NVS

Marker `voxcap/s0bt1` jest poza zamrożonym 255-bajtowym Config v7:

1. tylko dokładny S0+BT i marker `Missing` uruchamiają introduction migration;
2. historyczne `false` jest włączane i zapisywane jako `true` dokładnie raz;
3. marker powstaje dopiero po poprawnym i zweryfikowanym zapisie Config;
4. restart/MAIN-only OTA widzą marker i nie powtarzają migracji;
5. późniejsze świadome `false` oraz VXCF restore `false` pozostają skuteczne;
6. niedostępny/niepoprawny marker i przyszły Config działają fail-closed —
   firmware nie zgaduje intencji;
7. starszy rollback nie zna dodatkowego namespace i go nie modyfikuje.

Ponownie uruchomione binaria testowe zakończyły się PASS dla planu migracji
i pełnej persistence. Trwałość na rzeczywistym NVS pozostaje częścią HIL.

### Brak updatera S0 / zachowanie L0

- S0 ma `VOXONE_BT_FW_UPDATE_ENABLED=0`, L0 ma `1`.
- Rejestracja `GET /api/bt/update`, obsługa `POST /update/bt` i upload są pod
  `VOXONE_BT_UPDATE_UPLOAD`.
- Implementacja stagingu w firmware jest kompilowana tylko dla flagi update.
- `nm -C` S0 nie pokazuje `BtFirmwareStaging`, `handleBtUpload` ani
  `activeBtUploadRequest`.
- ELF L0 zawiera wszystkie powyższe symbole.

Tekstowe URL-e mogą pozostać we wspólnych assetach/polityce security; nie są
dowodem zarejestrowanej trasy. Faktyczne S0 powinno zwrócić zwykłe 404/route
unavailable bez utworzenia stagingu, a capability API ma zgłosić false.

### QIO/DIO — rozstrzygnięcie

Konfiguracja jest spójna i nie wymaga zmiany:

- `platformio.ini`: `qio_qspi`, flash `qio`, PSRAM `qio`, 4 MiB;
- wybrany pakiet SDK: `CONFIG_ESPTOOLPY_FLASHMODE_QIO=1`, quad PSRAM,
  80 MHz;
- PlatformIO Espressif32 6.12.0 w `_get_board_flash_mode()` celowo mapuje
  `qio/qout` na `dio` podczas tworzenia nagłówka obrazu;
- nagłówki `bootloader.bin` i `firmware.bin` mają bajt mode `0x02` = DIO,
  size 4 MiB i 80 MHz;
- `esptool image_info --version 2` potwierdza DIO/80 MHz/4 MiB, właściwy
  ESP32-S3, poprawne checksumy i validation hashes.

To odpowiada oficjalnemu boot flow ESP32-S3: ROM nie startuje software
bootloadera w QIO/QOUT, więc ładuje go w DIO, a software bootloader włącza
Quad I/O zgodnie z konfiguracją i wykrytym flash. Źródło:
[Espressif Boot Mode Selection](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html).

Manifestowe `flashMode=dio` opisuje zatem nagłówek/etap ROM, nie zaprzecza
runtime QIO. Nie zmieniać `platformio.ini`.

### ESP32-S3FH4R2

Model odpowiada konfiguracji S0:

- 4 MiB in-package Quad SPI flash;
- 2 MiB in-package Quad SPI PSRAM;
- VDD_SPI 3.3 V;
- domyślne maksimum flash 80 MHz.

Potwierdzają to
[Espressif ESP32-S3 datasheet](https://documentation.espressif.com/esp32_s3_datasheet_en.pdf)
oraz
[Waveshare ESP32-S3-Zero](https://docs.waveshare.com/ESP32-S3-Zero).
Waveshare podaje także USB bez mostka UART, BOOT GPIO0, UART0 na 43/44,
WS2812 na GPIO21 i regulator 800 mA.

Nominalny board PlatformIO nazywa się `ESP32-S3-DevKitC-1-N8 (No PSRAM)`,
ale projekt jawnie nadpisuje 4 MiB, `qio_qspi`, quad PSRAM i używa własnego
pinoutu. Wybrany wariant SDK jest właściwy dla FH4R2. Nadal trzeba odczytać
oznaczenie na rzeczywistym chipie/SKU oraz runtime `flash/psram total`;
deklaracja builda nie jest dowodem fizycznym.

## Weryfikacja i obecne artefakty robocze

Od końcowego `tools/verify.ps1` nie zmienił się kod ani profile; zmieniła się
wyłącznie dokumentacja. Końcowy przebieg IMPLEMENT miał:

- native 160/160 PASS;
- strict transport 16/16 PASS;
- ASan/UBSan PASS;
- pięć buildów i full images PASS;
- negative full-image 8/8 PASS;
- L0 regression PASS;
- `git diff --check` PASS.

W tym audycie ponownie uruchomiono niezmienione binaria końcowego przebiegu:
`hardware_descriptor_native-S0`, `hardware_descriptor_native-L0`,
`config_capability_migration_native`, `config_persistence_native` i
`bt_update_profile_contract_native`; wszystkie zakończyły się kodem 0,
a pierwsze cztery jawnie wydrukowały PASS.

Obecne SHA są tylko punktem odniesienia dirty i **muszą się zmienić po
czystym commicie**, ponieważ build identity jest osadzane w obrazie:

| Artefakt | Rozmiar | SHA-256 |
|---|---:|---|
| S0 `firmware.bin` | 1685568 B | `c0653f941eb1da4c1bdff6306736a0b7b21f55bec0b9abc88b30c813482bb20d` |
| S0 `firmware.elf` | 31496504 B | `466fc4c93f1a6b0fb93913ba8c1cad0fa530792499f20491a401dc842f636e42` |
| S0 `spiffs.bin` | 196608 B | `b95578af4a8cf209c97d21918e8ac50d5e815e06f79f72c395cda0dfebc45ae2` |
| S0 full image | 4194304 B | `d0cc68e0dc926be4e83f2c8b6ffb016055f39c06f16f04b425a03f5d8db12407` |
| bootloader | 15104 B | `e01a2300de23c8d601e9f2a37684b5dbc83ae37132d5a8833e23520afd895f92` |
| partitions | 3072 B | `1ae446228d79cf83a4b83de41c33d1e01dc21a3557149d7da90fbcbfc303311d` |

Zajęcie slotu: 1685568/1966080 B, wolne 280512 B. Nie ma blokady rozmiaru.

## C. Bezpieczna procedura uzyskania czystego kandydata

Procedura do wykonania dopiero po zgodzie na checkpointy:

1. Zachować bieżący worktree bez stash/reset/clean i zapisać jego pełny
   `git status`, diff oraz SHA wszystkich untracked reports.
2. Ustalić z właścicielem los checkpointu P0. Najbezpieczniej najpierw
   utworzyć osobny P0 zawierający wyłącznie wcześniejsze dowody/test; nie
   mieszać go semantycznie z S0.
3. Utworzyć drugi, izolowany `git worktree` poza repozytorium roboczym z
   dokładnej zatwierdzonej bazy P0. Nie kopiować całego dirty katalogu.
4. Do izolowanego worktree zastosować wyłącznie jawny zestaw P1. Dla
   `docs/TODO.md` przenieść tylko hunki S0; nie zastępować całego pliku.
5. Zaktualizować dwa stale dokumenty statusowe bez zmiany kontraktu Stable.
6. Sprawdzić, że `git status --short` zawiera dokładnie oczekiwany P1, a
   `git diff --check` ma PASS. Dopiero po jawnej zgodzie można wykonać
   explicit-path `git add` i commit; żadnego `git add -A`.
7. Z checkoutu dokładnego nowego commita, przy pustym statusie, uruchomić:
   pełny `tools/verify.ps1`, bezpośredni build S0/L0, walidację full image,
   symbol audit i `esptool image_info --version 2`.
8. Wymagać identity będącego skrótem nowego commita bez `-dirty`.
9. Zapisać SHA-256 ELF, firmware, SPIFFS, bootloadera, partitions i full
   image oraz artifactSetId. Te nowe wartości są jedynymi dopuszczalnymi
   SHA dla HIL.
10. Zarchiwizować manifest źródeł, log pełnego verify i artefakty w osobnym
    katalogu HIL read-only. Nie używać obecnych dirty BIN-ów.

## Rollback i recovery przed przyszłym flashowaniem

Osobny fizyczny preflight, backup i zgoda muszą potwierdzić:

1. właściwe urządzenie po chip marking/SKU, MAC i USB identity;
2. działający BOOT GPIO0 + RESET i wykrycie ROM download przez natywne USB;
3. odczyt eFuse/chip info bez ich zapisu;
4. pełny backup 4 MiB oraz osobne kopie NVS, otadata, app0, app1 i SPIFFS z
   dwoma zgodnymi SHA; dodatkowo VXCF i playlista;
5. bieżące active/running/boot partition, stan `VALID`, dostępność drugiego
   slotu i faktycznie używalny rollback;
6. obraz ratunkowy i dokładną procedurę odtworzenia backupu przez ROM USB;
7. zasilanie, które nie wykazuje brownout/undervoltage podczas backupu i
   przyszłego zapisu.

Migrację istniejącego `btEnabled=0` należy testować MAIN-only na zachowanym
NVS, nie przez erase/full-image. Pełny obraz jest artefaktem recovery i
clean-install; jego destrukcyjne użycie wymaga odrębnej zgody. W przypadku
nieudanego health gate pozostawić poprzedni poprawny slot jako rollback;
nie kasować go przed terminalnym PASS.

## Warunki elektryczne przed flashowaniem

- Oba urządzenia wyłączone podczas okablowania.
- Potwierdzone 3.3 V logic i wspólna masa.
- UART skrzyżowany: S0 RX10 ← BT TX17, S0 TX11 → BT RX16.
- I2S jednokierunkowy: BT master BCLK18/WS19/DATA23 → S0 12/13/14.
- I2S0/OLED/EC11 pozostają dokładnie na 5/6/4, 1/2 i 9/8/7.
- Continuity każdej linii oraz brak short do GND/3V3 i sąsiednich GPIO.
- Krótkie przewody I2S z dobrym powrotem masy; ewentualne rezystory szeregowe
  tylko według zatwierdzonego schematu.
- Nie zasilać VoxOneBT z 3V3 S0; użyć osobnego stabilnego zasilania/USB i
  wykluczyć back-power między portami.
- Potwierdzić strap `SD_MODE`/GAIN i napięcie zasilania MAX98357A.
- Raspberry Pi 3B z aktywnym undervoltage nie może być hostem bezwarunkowego
  PASS; użyć stabilnego hosta/zasilacza albo oznaczyć przebieg PARTIAL.

## D. Plan jednego kontrolowanego fizycznego HIL

Każda faza ma osobny checkpoint dowodowy. Nie przechodzić dalej po warunku
STOP.

### 1. Zasilanie, masa i GPIO

- fotograficzny zapis wiring, continuity, short test i poziomy spoczynkowe;
- potwierdzenie osobnych zasileń, wspólnej masy i braku back-power;
- pasywne loggery uzbrojone przed pierwszym startem.

### 2. Pierwszy start i identity S0

- dokładny clean build identity i SHA zgodne z manifestem;
- profil S0, 4 MiB flash, 2 MiB PSRAM, READY, app slot/VALID, rollback;
- stały bootId, SSD1306 128x32, EC11, MAX98357A, BT/audio RX true;
- VU, RTC, DSP i BT firmware update false; endpointy BT update niedostępne.

### 3. UART protocol 2

- 921600 8N1 na RX10/TX11;
- dokładnie jeden pełny status PROTO 2, FW/CAPS/OTA, PING/PONG i diagnostics;
- brak framing error, overflow, heartbeat loss i wpływu na UART0/USB log.

### 4. I2S1 RX do mono MAX98357A

- bounded PCM 44.1 i 48 kHz;
- poprawny rate, rosnące RX/TX, zero read/write error i stabilne pełne okna;
- L-only, R-only i both potwierdzają rzeczywisty wybór/mix `SD_MODE`;
- fizyczny odsłuch bez trzasków, dropoutów i zamiany kanału w przejściu.

### 5. Bluetooth lifecycle i PCM

- jeden znany peer, connect bez niezamierzonego autoplay;
- PLAY/PAUSE/PLAY, disconnect podczas PLAY i jeden reconnect;
- brak automatycznych retry/floodingu; pełne snapshoty po każdym przejściu.

### 6. SourceManager RADIO/BT

- ręczne RADIO→BT→RADIO;
- istniejące połączenie nie odbiera ręcznie wybranego RADIO;
- disconnect/runtime loss respektuje RADIO PLAY/STOP intent;
- ponowny online daje pełny resnapshot bez resetu MAIN.

### 7. TTS i restore

- natural EOF oraz jeden bounded error/timeout;
- dokładnie jeden cleanup/restore;
- brak równoczesnego właściciela I2S0, powrót do właściwego source, stacji i
  intent bez autoplay.

### 8. Config v7, reboot i persistence

- wykonanie tylko na kopii z potwierdzonym legacy `btEnabled=0` i backupem;
- pierwsze uruchomienie zapisuje true i marker raz;
- jeden zatwierdzony reboot nie powtarza write;
- świadome BT OFF pozostaje po reboocie;
- kontrolowany VXCF restore false pozostaje skuteczny;
- MAIN-only OTA/power-loss są osobnymi destrukcyjnymi fazami i wymagają
  odrębnego preflightu oraz zgody.

### 9. Stabilność końcowa

- READY/HTTP/WS/ping, stały bootId i monotoniczny uptime;
- heap/minHeap, PSRAM total/free/min, task stacks i liczniki UART/I2S;
- brak panic, assert, WDT, deadlock, restartu, brownout i narastających
  errors/shortWrites;
- restore dokładnego stanu początkowego i zapis końcowych SHA/logów.

## STOP i cleanup

Natychmiast zatrzymać scenariusz przy brownout/undervoltage, nieoczekiwanym
reboocie, panic/assert/WDT, utracie READY/bootId, błędzie UART/I2S, niestabilnym
zasilaniu, nieoczekiwanym PLAY albo niezgodności SHA/identity. Nie ponawiać
automatycznie i nie wykonywać power-cycle jako „naprawy”. Rozłączyć peer,
zatrzymać generatory PCM/TTS, przywrócić bezpieczny RADIO/STOP lub zapisany
stan początkowy, zamknąć WS kodem 1000, zakończyć loggery i zachować dowody.

## Dokładne warunki zdjęcia blokady

Status może zmienić się z BLOCKED na **CANDIDATE READY** dopiero gdy:

1. P0 zostanie rozstrzygnięty bez utraty wcześniejszych zmian;
2. P1 istnieje jako jawnie zatwierdzony czysty commit;
3. dwa stale dokumenty sprzętowe zostaną zsynchronizowane bez zmiany zakresu;
4. pełny verify przejdzie z czystego checkoutu tego commita;
5. nowe artefakty mają identity bez `-dirty`, zapisane SHA i manifest;
6. chip/SKU S0 potwierdza FH4R2 4 MiB/2 MiB;
7. okablowanie, `SD_MODE`, zasilanie oraz USB ROM recovery są fizycznie
   potwierdzone;
8. istnieją pełny backup i jawny rollback plan;
9. Raspberry/host nie raportuje undervoltage;
10. osobno zatwierdzono właściwe flashowanie/HIL.

Do spełnienia tych warunków nie wolno używać obecnego dirty artefaktu jako
kandydata Stable ani deklarować S0 PHYSICAL HIL PASS.
