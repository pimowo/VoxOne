# VOXONE-RC1-TTS-STALL-D-CLOSURE-1

Data: 2026-10-30

Repozytorium: `/home/piotrek/projects/VoxOne`

HEAD: `fbbdfbcab76d5c990fac5a7bc3a972ff40afd206`

Werdykt: **READY_FOR_CHECKPOINT**

## 1. Zamknięcie scenariusza D

Scenariusz D ma fizyczny **L0 HIL PASS**. Zweryfikowano integralność `REPORT.md`, `SUMMARY.json`, wymaganych JSONL i ich `SHA256SUMS` w katalogu:

`/home/piotrek/voxone-hil-logs/VOXONE-RC1-TTS-STALL-D-HIL-L0-RETEST-2-20261030-105440/`

Łańcuch dowodowy:

- zatwierdzony obraz HIL: 1 775 584 B, SHA-256 `518200bbded961689c74d3ca64dcc681e8e81a29dde91bd8acc29c35a8041f99`;
- artefakt źródłowy nadal istnieje i ponownie ma dokładnie ten rozmiar i SHA-256;
- L0: `app1/app1 VALID`, `bootId=aaaf4356`, READY 231/231;
- dokładnie jeden przebieg i jedna publikacja MQTT;
- `httpContentLength=httpBodyBytesRead=bytesRead=3648044`;
- `watchdogTimeouts +0`, `transportErrors +0`;
- `naturalEofs +1`, `cleanups +1`;
- `endReason=natural_eof`, `state=cleaned`;
- 10 granic fragmentów potwierdzonych licznikami firmware, maksymalna przerwa fixture 4,266 s;
- 65,056 s stabilnej obserwacji po cleanup, bez zmiany BootId i bez pozostawionego TTS, handlera lub listenera.

API nie udostępnia runtime SHA. Tożsamość obrazu opiera się na udokumentowanym łańcuchu OTA-3: SHA payloadu, pojedynczy upload, zmiana partycji, nowy BootId i VALID; raport nie deklaruje bezpośredniej atestacji runtime.

PASS zamyka wyłącznie scenariusz D na L0. Nie zamyka całej bramki `RC1-TTS-RESTORE`, pięcioprofilowego HIL ani Stable 1.0.0.

## 2. Audyt worktree i przypisanie zmian

Stan wejściowy obejmował 8 zmodyfikowanych i 5 nowych plików. Wszystkie należą do trzech poprawek TTS albo do wyłącznie flagowanej diagnostyki HIL; nie znaleziono innej, niezwiązanej zmiany.

| Zakres | Pliki |
|---|---|
| Pełny WAV bez utraty 44 B | `src/audioI2S/Audio.cpp`, `src/audioI2S/AudioEx.h`, `src/audioI2S/wav_header_parser.h`, `src/audioI2S/web_file_bounds.h`, `test/wav_webfile_native.cpp` |
| Rozłączenie TCP vs watchdog | `src/core/temporary_transport_watchdog.h`, `src/core/player.cpp`, `test/temporary_audio_native.cpp` |
| Natural EOF po drenażu bufora | `src/audioI2S/Audio.cpp`, `src/audioI2S/AudioEx.h`, `src/core/player.cpp`, `src/core/temporary_transport_watchdog.h`, `test/temporary_audio_native.cpp`, `test/tts_hil_diagnostics_native.cpp` |
| Diagnostyka TTS HIL | `platformio.ini`, `src/core/tts_hil_diagnostics.h`, `src/core/netserver.cpp`, flagowane fragmenty `Audio.cpp`, `AudioEx.h` i `player.cpp`, `test/tts_hil_diagnostics_native.cpp` |
| Trwała integracja testów i sanitizerów | `tools/verify.ps1`, trzy pliki testowe wyżej |

Współdzielenie plików jest zamierzone: poprawki transportu potrzebują stanu pełnego body i poziomu `InBuff`, a diagnostyka rejestruje te same rzeczywiste przejścia produkcyjnego `Player`/`Audio`. Nie ma osobnego modelu testowego zastępującego produkcyjny watchdog.

Nowy raport zamknięcia jest jedynym plikiem utworzonym przez ten etap. Firmware, testy i `docs/TODO.md` nie zostały zmienione.

## 3. Pełna weryfikacja

Uruchomiono pełne `tools/verify.ps1`. Wynik terminalny: **`RESULT: PASS`**.

- Display boot branding assets: PASS;
- 158 testów natywnych: PASS;
- radio transport strict: 16/16 PASS;
- Native sanitizers (ASan/UBSan): PASS;
- Web Update headless: PASS;
- HIL diagnostics: PASS (wyłącznie mock/pseudoterminal, bez urządzeń);
- SPIFFS i WWW assets/repeatability: PASS;
- full-image validation regression: 8/8 PASS;
- L0 TX-only: PASS;
- `git diff --check`: PASS podczas verify i w osobnym ponowieniu.

### Standardowe profile

| Profil | Build/fullimage | firmware.bin | Wolne w partycji 1 966 080 B |
|---|---|---:|---:|
| L0 | PASS | 1 764 272 B | 201 808 B |
| M0 | PASS | 1 663 696 B | 302 384 B |
| S0 | PASS | 1 661 504 B | 304 576 B |
| DIN0 | PASS | 1 661 680 B | 304 400 B |
| W0 | PASS | 1 668 368 B | 297 712 B |

Żaden z pięciu standardowych profili nie zawiera `VOXONE_TTS_HIL_DIAGNOSTICS`. Flaga występuje wyłącznie w dedykowanych `m0-tts-hil-diagnostics` i `l0-tts-hil-diagnostics`. Test kontraktu wymusza brak flagi w standardowych M0/L0, a pełne macierze `hardware_descriptor`, runtime hardware, SECURITY i build identity dla pięciu profili mają PASS. Capability standardowych profili nie zmieniono.

## 4. Trwałość regresji

Regresje są częścią automatycznie odkrywanego zestawu `*_native.cpp` oraz sekcji sanitizerów pełnego verify:

- `wav_webfile_native`: kanoniczny WAV 44 B, pełne `data.chunkSize`, dodatkowe/paddingowane chunki, niepełny nagłówek, błędny RIFF, ucięte i niespójne długości, zabezpieczenia underflow/overflow, krótkie body oraz wspólne granice innych web-file;
- `temporary_audio_native`: 10 fragmentów co 4 s, prawdziwy stall >10 s, pełne body z natychmiastowym close i keep-alive, drenaż `InBuff`, niepełne body z close, ostatni dodatni odczyt i close w jednym cyklu, STOP oraz jednokrotny cleanup;
- `tts_hil_diagnostics_native`: identyfikacja runId, liczniki, natural EOF, pierwsza przyczyna końca, późne callbacki, jednokrotny cleanup, współbieżny snapshot, flaga kompilacji i autoryzowany istniejący endpoint.

Wszystkie trzy testy przeszły jako native; wszystkie trzy są także jawnie uruchamiane z ASan/UBSan. Rozmiar stałego magazynu diagnostyki: 168 B.

## 5. Otwarte prace po D

- A/B/C miały historyczny L0 PASS na czystym `fbbdfbc`, ale poprzedzały zmiany parsera WAV, klasyfikacji rozłączenia i drenażu EOF. Muszą zostać powtórzone na docelowym kandydacie; nie przenosi się automatycznie ich wyniku na bieżący dirty worktree.
- S0 wymaga odrębnego HIL TTS; L0 PASS nie kwalifikuje S0.
- Pozostają wymagania fizycznego routingu/odsłuchu wskazane w bramce TTS oraz końcowy HIL zamrożonego kandydata.
- Raport D nie ocenia analogowej jakości audio ani słyszalnych underrunów.

## 6. Proponowany diff `docs/TODO.md` — NIEZASTOSOWANY

Poniższy diff dokładnie zapisuje D jako L0 HIL PASS, pozostawiając bramkę `OPEN / PARTIAL`, regresje A/B/C na docelowym kandydacie i odrębny HIL S0 jako otwarte:

```diff
--- a/docs/TODO.md
+++ b/docs/TODO.md
@@ -37,7 +37,7 @@
 | 6 | `RC1-RADIO-ENDURANCE` | **CLOSED** | Terminalny endurance oraz wymagane dowody transportu zostały zachowane; części profilowe i odsłuchowe pozostają w odpowiednich bramkach profili. |
 | 7 | `RC1-BT-LIFECYCLE` | OPEN / PARTIAL | A2DP/AVRCP oraz fizyczne audio i pauza LG mają PASS; niezgodne raportowanie PAUSE jest zaakceptowanym znanym ograniczeniem i samo nie blokuje Stable. Pozostałe wymagane lifecycle, reconnect, metadata/volume i finalny kandydat pozostają otwarte. |
 | 8 | `RC1-SOURCE-MANAGER` | OPEN / PARTIAL | RADIO ↔ BT, intencje PLAY/STOP, disconnect/reconnect i runtime-loss mają etapowe PASS; terminalna stabilność na zamrożonym kandydacie pozostaje otwarta. |
-| 9 | `RC1-TTS-RESTORE` | OPEN / PARTIAL | Normal end, interruption i wybrane RADIO/BT restore mają PASS; error/timeout i pełny fizyczny routing pozostają otwarte. |
+| 9 | `RC1-TTS-RESTORE` | OPEN / PARTIAL | Normal end, interruption, wybrane RADIO/BT restore oraz scenariusz D mają L0 HIL PASS; regresje A/B/C na docelowym kandydacie, odrębny HIL S0 i pełny fizyczny routing pozostają otwarte. |
 | 10 | `RC1-UPDATE` | OPEN / PARTIAL | MAIN-only OTA ma dodatnie wyniki, lecz pełna macierz MAIN/SPIFFS, przerwań i profili nie jest zamknięta. |
 | 11 | `RC1-RECOVERY` | OPEN / PARTIAL | Software i wybrane HIL mają PASS; negatywne, power-loss i pozostałe profile pozostają otwarte. |
 | 12 | `RC1-CONFIG-V7` | OPEN / PARTIAL | Config/VXCF i zachowanie danych mają liczne PASS; pełna pięcioprofilowa trwałość i scenariusze błędów pozostają otwarte. |
@@ -384,7 +384,8 @@
 - [POST-STABLE] TTS-ADVANCED-1 — rozszerzyć TTS o kolejkę i priorytety komunikatów, konfigurowalne źródła/głosy oraz kontrolowane ducking, cache i restore; zachować jeden kontrakt temporary audio i nie traktować etapu jako blokującego Stable 1.0.0.
 - [POST-STABLE] Wprowadzić tryby TTS Volume `CURRENT`, `FIXED` i `AUTO`. `CURRENT` używa normalnego `userVolume`; `FIXED` ma własne 0–100 bez zmiany `userVolume`; `AUTO` wybiera wartość na początku komunikatu na podstawie rzeczywistego base playback i utrzymuje ją do końca TTS.
 - [POST-STABLE] Zachować Max Volume jako hard clamp i nie wysyłać BT Absolute Volume podczas TTS.
-- [STABLE][RC1-TTS-RESTORE PARTIAL][LEGACY GATE 7][SOFTWARE PASS][L0 NORMAL-END/BT-LOSS/INTERRUPTION HIL PASS][ERROR/TIMEOUT HIL PENDING] Fizycznie dokończyć ownership I2S0, sample rate i routing po błędzie i timeout; nie wymuszać ich sztuczną awarią sieci ani wadliwą trwałą konfiguracją. `VOXONE-RC1-L0-TTS-RESTORE-OFFLINE-1` domyka dowód programowy: rzeczywiste spięcie callbacków `audio_error`/EOF z tokenem bieżącej generacji, ścieżka błędu `connecttohost()`, fallback timeoutu przez `!isRunning()`, jednokrotne restore, odporność na spóźniony callback oraz aktualna intencja/source mają native i ASan/UBSan PASS; kod produkcyjny nie wymagał zmiany. `VOXONE-RC1-L0-TTS-RESTORE-HIL-2` ma PASS normalnego końca dla RADIO PLAY, RADIO STOP i BT PLAY. `VOXONE-RC1-L0-TTS-FINAL-HIL-1` dodatkowo potwierdził BT PAUSED → TTS → PAUSED, BT STOPPED → TTS → STOPPED oraz utratę BT podczas aktywnego TTS: temporary MP3 grał dalej, po końcu pozostało RADIO/STOP i przez ponad 30 s nie wystąpił stale RESUME/autoplay. `VOXONE-RC1-L0-TTS-CLOSURE-AFTER-WS-FIX-1` zamknął ręczne przerwanie na poprawionym `app1 VALID`: jedna publikacja MQTT QoS1 uruchomiła rzeczywisty temporary MP3, dokładnie jedno `stop=1` podczas aktywnego TTS dało RADIO/STOP po 36 ms i przez 75,707 s nie wystąpił opóźniony RESUME/autoplay; pojedynczy cleanup `start=1` przywrócił `Italo4you` RADIO/PLAY. Przez 300,268 s UART/WS zachowały `bootId=e68b1745`, READY i stabilny heap bez panic/assert/watchdog/korupcji/resetu; MQTT, SECURITY i playlista pozostały identyczne z checkpointem po poprawce WS. Error/timeout nie wykonywano fizycznie, ponieważ brak jawnej produkcyjnej iniekcji, a sztuczna awaria sieci lub wadliwy URL nie kwalifikują bramki. Nadal PENDING są error/timeout, fizyczny odsłuch oraz pomiar analogowego L/R/I2S0; WS dla RADIO raportuje `sampleRate=0`. Sprzętowe XSMT nie jest kryterium Stable. Raporty: `reports/VOXONE-RC1-L0-TTS-RESTORE-HIL-2.md`, `reports/VOXONE-RC1-L0-TTS-FINAL-HIL-1.md`, `reports/VOXONE-RC1-L0-BAD-HEAD-OTA-RETRY-1.md`, `reports/VOXONE-RC1-L0-TTS-CLOSURE-AFTER-WS-FIX-1.md`.
+- [STABLE][RC1-TTS-RESTORE PARTIAL][LEGACY GATE 7][SOFTWARE PASS][L0 NORMAL-END/BT-LOSS/INTERRUPTION HIL PASS][L0 STALL D HIL PASS][TARGET-CANDIDATE A/B/C + S0 HIL PENDING] Fizycznie dokończyć ownership I2S0, sample rate i routing po błędzie i timeout; nie wymuszać ich sztuczną awarią sieci ani wadliwą trwałą konfiguracją. `VOXONE-RC1-L0-TTS-RESTORE-OFFLINE-1` domyka dowód programowy: rzeczywiste spięcie callbacków `audio_error`/EOF z tokenem bieżącej generacji, ścieżka błędu `connecttohost()`, fallback timeoutu przez `!isRunning()`, jednokrotne restore, odporność na spóźniony callback oraz aktualna intencja/source mają native i ASan/UBSan PASS; kod produkcyjny nie wymagał zmiany. `VOXONE-RC1-L0-TTS-RESTORE-HIL-2` ma PASS normalnego końca dla RADIO PLAY, RADIO STOP i BT PLAY. `VOXONE-RC1-L0-TTS-FINAL-HIL-1` dodatkowo potwierdził BT PAUSED → TTS → PAUSED, BT STOPPED → TTS → STOPPED oraz utratę BT podczas aktywnego TTS: temporary MP3 grał dalej, po końcu pozostało RADIO/STOP i przez ponad 30 s nie wystąpił stale RESUME/autoplay. `VOXONE-RC1-L0-TTS-CLOSURE-AFTER-WS-FIX-1` zamknął ręczne przerwanie na poprawionym `app1 VALID`: jedna publikacja MQTT QoS1 uruchomiła rzeczywisty temporary MP3, dokładnie jedno `stop=1` podczas aktywnego TTS dało RADIO/STOP po 36 ms i przez 75,707 s nie wystąpił opóźniony RESUME/autoplay; pojedynczy cleanup `start=1` przywrócił `Italo4you` RADIO/PLAY. Przez 300,268 s UART/WS zachowały `bootId=e68b1745`, READY i stabilny heap bez panic/assert/watchdog/korupcji/resetu; MQTT, SECURITY i playlista pozostały identyczne z checkpointem po poprawce WS. Scenariusz D jest zamknięty na L0 osobnym HIL opisanym niżej. Nadal PENDING są regresje A/B/C na docelowym kandydacie, odrębny HIL S0, pozostałe przypadki error/timeout niewchodzące w A/B/C/D, fizyczny odsłuch oraz pomiar analogowego L/R/I2S0; WS dla RADIO raportuje `sampleRate=0`. Sprzętowe XSMT nie jest kryterium Stable. Raporty: `reports/VOXONE-RC1-L0-TTS-RESTORE-HIL-2.md`, `reports/VOXONE-RC1-L0-TTS-FINAL-HIL-1.md`, `reports/VOXONE-RC1-L0-BAD-HEAD-OTA-RETRY-1.md`, `reports/VOXONE-RC1-L0-TTS-CLOSURE-AFTER-WS-FIX-1.md`.
+  - [L0 STALL D PHYSICAL HIL PASS][TARGET-CANDIDATE A/B/C + S0 HIL PENDING] `VOXONE-RC1-TTS-STALL-D-HIL-L0-RETEST-2` 2026-10-30 ma PASS na fizycznym L0 dla wolnego, stale postępującego WAV: zatwierdzony obraz HIL 1 775 584 B o SHA-256 `518200bbded961689c74d3ca64dcc681e8e81a29dde91bd8acc29c35a8041f99`, `app1 VALID`, stały `bootId=aaaf4356`, pełne `3648044/3648044 B`, 10 fragmentów, maksymalna przerwa 4,266 s, `watchdogTimeouts +0`, `transportErrors +0`, `naturalEofs +1`, `cleanups +1`, `endReason=natural_eof`, `state=cleaned` i READY 231/231. Wynik zamyka wyłącznie scenariusz D na L0. Historyczne A/B/C miały PASS na czystym `fbbdfbc`, lecz po zmianach parsera WAV, klasyfikacji TCP i drenażu EOF wymagają regresji na docelowym kandydacie; odrębny HIL S0 pozostaje PENDING. Raport zamknięcia: `reports/VOXONE-RC1-TTS-STALL-D-CLOSURE-1.md`.
 - [POST-STABLE] Na LCD pokazywać dla TTS stację „KOMUNIKAT TTS”, artystę „Powiadomienie głosowe”, pusty utwór i poprawną etykietę źródła.
 - [POST-STABLE] Dodać HTTPS i redirect tylko jeśli wymagają tego używane endpointy.
 - [POST-STABLE] Rozszerzyć temporary audio restore na przyszłe DLNA i AUX bez zmiany pierwszeństwa ręcznego wyboru źródła.
```

## 7. Dokładny plan checkpointu Git

Po osobnym zatwierdzeniu aktualizacji TODO zakres jednego checkpointu powinien zawierać wyłącznie:

1. `platformio.ini`
2. `src/audioI2S/Audio.cpp`
3. `src/audioI2S/AudioEx.h`
4. `src/audioI2S/wav_header_parser.h`
5. `src/audioI2S/web_file_bounds.h`
6. `src/core/netserver.cpp`
7. `src/core/player.cpp`
8. `src/core/temporary_transport_watchdog.h`
9. `src/core/tts_hil_diagnostics.h`
10. `test/temporary_audio_native.cpp`
11. `test/tts_hil_diagnostics_native.cpp`
12. `test/wav_webfile_native.cpp`
13. `tools/verify.ps1`
14. `reports/VOXONE-RC1-TTS-STALL-D-CLOSURE-1.md`
15. `docs/TODO.md` — dopiero po zatwierdzeniu powyższego diffu.

Wykluczyć `.pio/`, zewnętrzne logi HIL, fixture, `firmware.bin`, backupy urządzenia i wszystkie sekrety. Przed commitem sprawdzić dokładną listę staged, `git diff --cached --check` oraz brak zmian poza listą. Nie przebudowywać obrazu tylko po to, aby wykonać commit; pełny verify bieżącego kodu ma już PASS.

Proponowany commit:

`fix(tts): close slow-transfer WAV EOF handling`

Proponowany tag anotowany po commicie i dopiero po osobnej zgodzie:

`voxone-rc1-tts-stall-d-closure-1`

Nie wykonywać push bez osobnego polecenia.

## 8. Stan końcowy

- `docs/TODO.md` pozostaje niezmienione.
- Nie wykonano commit, tag ani push.
- Nie wykonano OTA, flashowania, restartu ani HIL.
- Nie dotknięto M0 ani S0.
- Worktree jest gotowy do kontrolowanego checkpointu po zatwierdzeniu diffu TODO i osobnym poleceniu wykonania Git.

**Werdykt: READY_FOR_CHECKPOINT.**
