# VOXONE — RC1-TTS-OFFLINE-CLOSURE-EVIDENCE-1

Data: 2026-11-04 (Europe/Warsaw)

Zakres: udokumentowanie zakończonego audytu
`RC1-L0-TTS-DIAGNOSTICS-OFFLINE-1`, zachowanie istniejących wyników A/B/C/D
oraz wyznaczenie minimalnej, nieprzenoszonej z L0 macierzy fizycznego HIL TTS
dla S0. W tym etapie nie kontaktowano się z urządzeniami, nie uruchamiano TTS,
nie wykonywano OTA, flashowania ani restartów.

## Werdykt

**OFFLINE QUALIFICATION PASS / RC1-TTS-RESTORE PARTIAL.** Dokładna rewizja
źródłowa raportowana przez L0 została odtworzona i zbudowana w izolacji, a
celowane testy kontraktu TTS, SourceManager i diagnostyki przeszły. Nie ma
podstaw do powtarzania fizycznych A/B/C na L0 wyłącznie w celu odczytania
wewnętrznych liczników. Pełna bramka pozostaje otwarta dla fizycznej
kwalifikacji S0, odsłuchu/routingu I2S oraz końcowego zamrożonego kandydata.

## 1. Rewizje i zakres różnic

- L0 raportował build `4ef1e53`; pełny obiekt Git istnieje jako
  `4ef1e53e32d78af94165addf3d2953e0b3939451`.
- HEAD podczas audytu i podczas tego zapisu:
  `81f984e459ab590cc2765651eaa986b1398747aa`.
- `4ef1e53` jest przodkiem `81f984e`; dzieli je pięć commitów.
- Między `4ef1e53` i HEAD obiekty Git `player.cpp/.h`,
  `source_manager.cpp/.h`, `temporary_transport_watchdog.h`,
  `tts_hil_diagnostics.h`, `Audio.cpp`, `AudioEx.h` oraz dwa podstawowe testy
  TTS są identyczne. Późniejsze zmiany dotyczą przede wszystkim S0/VoxOneBT,
  capability i lokalnego UI.
- Ścieżki TTS/cleanup użyte w fizycznych A/B/C na `bf1d147` są identyczne z
  `4ef1e53`. Zmiana między tymi rewizjami w kodzie wykonawczym dotyczyła
  zachowania komend transportu w snapshotach protokołu BT, nie scenariuszy
  RADIO A/B/C ani jednorazowego cleanup.

Build ID nie jest skrótem zawartości flash. Audyt nie deklaruje bitowej
atestacji aktualnie uruchomionej partycji L0.

## 2. Build dokładnego `4ef1e53`

Utworzono czystą, odłączoną kopię dokładnego commita poza repozytorium
roboczym. PlatformIO 6.2.0 z przypiętym `platformio/espressif32@6.12.0`
zbudowało oba warianty:

| Środowisko | Wynik | RAM linkera | Flash linkera | Rozmiar BIN | SHA-256 BIN |
|---|---:|---:|---:|---:|---|
| `l0` | PASS | 82 216 B | 1 764 233 B | 1 764 656 B | `b00ce9deb42b6ab1d5d07c9c34a7b1c458a8e19562aa488e003235c2cd5521c4` |
| `l0-tts-hil-diagnostics` | PASS | 82 400 B | 1 775 569 B | 1 775 984 B | `3d191340bb872983eb1486b7f2745490bd81543d4f7ab07a6a28b2e9d2303c8c` |

SHA-256 ELF wynosi odpowiednio
`29b497e20acc15fc1ff3831a80eb3f6868027b5bce8fca4da37740309fff7a50`
oraz
`ea462a840b5f7599d7128a37fa7711c86696aa21d971952452b179a0e68ebb23`.
Logi, wynik i kopie BIN są poza repozytorium w
`~/voxone-hil/tts-diagnostics-offline/RC1-L0-TTS-DIAGNOSTICS-OFFLINE-1-20261104T181255+0100/`.

## 3. Weryfikacja natywna

Osiem celowanych testów z `-O2 -Wall -Wextra -Werror` ma **8/8 PASS**:

1. `temporary_audio_native`;
2. `tts_hil_diagnostics_native` (`storage=168 bytes`);
3. `source_manager_native`;
4. `source_stop_native`;
5. `source_connect_restore_native`;
6. `radio_source_intent_native`;
7. `bt_source_switch_native`;
8. `bt_loss_audio_safety_native`.

`temporary_audio_native` i `tts_hil_diagnostics_native` mają dodatkowo
**2/2 PASS** z ASan/UBSan.

Potwierdzony kontrakt offline obejmuje:

- token bieżącej generacji i odrzucanie callbacku starej generacji;
- pierwszą zaakceptowaną przyczynę terminalną bez nadpisania przez późny EOF,
  transport error albo watchdog;
- dokładnie jeden `finish`/cleanup i jeden restore mimo duplikatu terminalnego;
- terminal state `cleaned` oraz spójny snapshot przy współbieżnym odczycie;
- wspólną ścieżkę cleanup dla connect failure, transport error, watchdog i
  natural EOF;
- zachowanie bieżącego source i RADIO PLAY/STOP intent zamiast źródła z chwili
  startu komunikatu;
- brak syntetycznej komendy AVRCP po TTS nad BT PAUSED/STOPPED;
- stłumienie RADIO autoplay po utracie BT podczas aktywnego TTS.

## 4. Diagnostyka a firmware produkcyjne

`VOXONE_TTS_HIL_DIAGNOSTICS` nie zmienia rozstrzygnięć state machine ani
warunków restore, ale wykonuje dodatkowe atomiki i liczenie postępu w
rzeczywistej ścieżce Player/Audio. Dodaje 184 B RAM i 11 336 B Flash według
linkera, zmienia layout oraz może minimalnie zmieniać timing. Obrazu
diagnostycznego nie wolno utożsamiać bitowo ani czasowo ze standardowym `l0`.

Diagnostyczny snapshot przez autoryzowane
`GET /api/hardware?ttsDiagnostics=1` udostępnia per-run m.in. `cleanups`,
`watchdogTimeouts`, `naturalEofs`, `transportErrors`, terminal reason i state.
Totals obejmują `runs`, `watchdogTimeouts`, `naturalEofs`, `cleanups` i
`replacements`; nie ma osobnego total dla `transportErrors`. Per-run zeruje
się przy kolejnym starcie, a cała diagnostyka RAM przy restarcie. Standardowy
L0 nie udostępnia tych liczników ani równoważnego snapshotu WebSocket.

## 5. Rozdzielenie dowodów A/B/C/D

| Scenariusz | Dowód fizyczny | Dowód licznikowy na urządzeniu | Dowód offline | Wynik zachowany |
|---|---|---|---|---|
| A — HTTP 503, RADIO STOP | Jeden GET/503, UART/WS STOP, brak trwałego autoplay i stabilny L0 | Brak odczytu wewnętrznego cleanup | Connect failure, terminal/finish, brak podwójnego cleanup i brak późnego restore | L0 FUNCTIONAL HIL PASS |
| B — stalled body, RADIO STOP | Watchdog około 10,06 s, autonomiczny STOP, brak autoplay | Brak odczytu wewnętrznego cleanup | Timeout postępu, jeden cleanup, late callback harmless | L0 FUNCTIONAL HIL PASS |
| C — stalled body, RADIO PLAY | Watchdog około 10,41 s, autonomiczny PLAY około 10,80 s, ta sama stacja | Brak odczytu wewnętrznego cleanup | Timeout, aktualny intent, jeden restore i brak ponowienia | L0 FUNCTIONAL HIL PASS |
| D — wolny stale postępujący transfer | Pełne `3648044/3648044 B`, stabilne READY 231/231 | `naturalEofs +1`, `cleanups +1`, watchdog/error +0, `natural_eof`, `cleaned` | Postęp odnawia okno, pełne body może się drenować do EOF | L0 PHYSICAL/COUNTED HIL PASS |

Wewnętrzny licznik cleanup A/B/C pozostaje **UNVERIFIED**, lecz nie cofa
obserwowalnych wyników funkcjonalnych. Raport D nie przyznaje fizycznego PASS
jakości audio ani braku słyszalnych underrunów. Wyniki L0 nie są fizycznymi
dowodami S0.

## 6. Decyzja o niepowtarzaniu A/B/C na L0

Kontrakt Stable wymaga jednorazowego zachowania cleanup/restore po końcu,
błędzie lub timeout, a nie obecności konkretnego licznika diagnostycznego.
A/B/C wykazały wymagane zachowanie fizyczne przez niezależne HTTP, UART i WS;
D wykazał bezpośrednio licznikową jednorazowość ścieżki natural EOF, a testy
native i sanitizery obejmują pozostałe zakończenia, duplikaty i późne
callbacki. Ponowienie A/B/C na obrazie diagnostycznym dałoby mocniejszą
telemetrię, ale wymagałoby innego binarium i ryzykownego OTA bez rozszerzenia
wymaganego kontraktu. Bez dowodu regresji nie ma uzasadnienia dla takiego
powtórzenia.

## 7. Ryzyko późniejszego diagnostycznego OTA

Ryzyko jest nieakceptowalne przy nieobecnym operatorze: OTA zmieniłoby aktywny
obraz i `bootId`, obraz diagnostyczny ma inny timing i footprint, a sam status
rollback nie gwarantuje odzyskania zdalnego dostępu. Ewentualne późniejsze
wdrożenie wymaga obecnego operatora, ciągłego logu native USB, zweryfikowanego
obrazu powrotnego, potwierdzonego rollbacku i jednoznacznego planu STOP.
Diagnostyczne A/B/C są opcjonalnym wzmocnieniem dowodu, nie warunkiem zamknięcia
już zaliczonych funkcjonalnych A/B/C L0.

## 8. Minimalna fizyczna macierz S0

S0 jest osobnym obowiązkowym profilem Stable: ESP32-S3 Zero, lokalny
MAX98357, OLED i EC11, VoxOneBT po UART1 RX44/TX43 oraz PCM I2S1
BCLK11/WS12/DATA13; log aplikacji musi używać native USB CDC. VU oraz
MAIN→VoxOneBT update są poza tą macierzą.

| Próba | Baseline i zakończenie | Minimalny dowód PASS | Pokrycie istniejące |
|---|---|---|---|
| S0-TTS-1 | RADIO PLAY → TTS z natural EOF | Temporary audio fizycznie przejmuje I2S0; po EOF wraca ta sama stacja i PLAY intent; jeden obserwowalny cleanup/restore; poprawny OLED i lokalne sterowanie | Logika offline PASS; odpowiednik L0 PASS; S0 PHYSICAL PENDING |
| S0-TTS-2 | RADIO STOP → TTS z kontrolowanym HTTP error | Po błędzie RADIO pozostaje STOP bez autoplay i bez późnego PLAY; I2S0 oraz UI wracają do stanu RADIO STOP | Logika offline i L0 A PASS; S0 PHYSICAL PENDING |
| S0-TTS-3 | BT PLAY → TTS z natural EOF | TTS przejmuje wyjście bez zmiany źródła bazowego; po EOF I2S1→I2S0 wraca do BT PLAY bez syntetycznej komendy transportu; OLED/EC11 pozostają sprawne | Logika offline oraz L0 BT PLAY PASS w TODO; S0 PHYSICAL PENDING |
| S0-TTS-4 | BT PAUSED → TTS z kontrolowanym timeoutem postępu | Po timeout pozostaje BT PAUSED, bez AVRCP RESUME/autoplay; jeden cleanup, brak późnego przejścia; routing/UI/sterowanie pozostają sprawne | Logika offline oraz L0 BT PAUSED PASS w TODO; S0 PHYSICAL PENDING |

Istniejące pokrycie testowe jest jawne, lecz wyłącznie programowe:
`temporary_audio_native` obejmuje RADIO PLAY/STOP, BT PLAY/PAUSED/STOPPED,
connect failure, transport error, timeout, natural EOF, duplikat terminalny i
late/stale callback. `source_manager_native`, `source_stop_native`,
`source_connect_restore_native`, `radio_source_intent_native`,
`bt_source_switch_native` i `bt_loss_audio_safety_native` sprawdzają wybór,
intent, restore i utratę BT. `bt_audio_input_native`, `bt_audio_route_native`,
`ui_input_events_native`, `local_input_activation_native` oraz testy hardware
descriptor pokrywają kontrakt I2S/UI/capability, ale nie fizyczne okablowanie,
OLED, EC11, MAX98357 ani PCM I2S1 konkretnego S0.

Te cztery próby obejmują wymagane RADIO PLAY/STOP, BT PLAY/PAUSED, HTTP error,
timeout i natural EOF. Brak podwójnego cleanupu ocenia się przez jeden
terminalny transition/restore oraz pasywny postcheck co najmniej przez kolejne
okno inactivity timeout; dokładna jednorazowość wewnętrzna zachowuje osobny
dowód offline. Nie jest potrzebny piąty wariant ani powtarzanie D na S0 bez
nowej przesłanki.

Po całej macierzy wystarczy jeden kontrolowany smoke lokalnego UI: zgodny ekran
źródła/playback oraz osobno zatwierdzona, odwracalna para wejść EC11, z
przywróceniem baseline. Fizyczny odsłuch albo pomiar analogowy/I2S jest
konieczny do kwalifikacji routingu; odczyt REST/WS nie zastępuje tego dowodu.

## 9. Warunki rozpoczęcia S0 HIL

1. S0 jest fizycznie podłączony, operator znajduje się przy urządzeniu i
   potwierdza możliwość odsłuchu albo pomiaru I2S/wyjścia analogowego.
2. Istnieje czysty, dokładnie zidentyfikowany kandydat S0 oraz zweryfikowany
   obraz powrotny; deployment i rollback mają osobną zgodę.
3. Native USB CDC zapisuje ciągły UART bez używania GPIO43/44, a READY, OTA,
   `bootId`, heap/PSRAM i stan VoxOneBT są monitorowane.
4. VoxOneBT ma potwierdzone UART protocol 2, BT online oraz stabilne PCM I2S1;
   źródła RADIO i BT mają świeży baseline stacji, playback, volume, MUTE i
   balance.
5. Jednorazowe fixture natural EOF, HTTP error i stall są ograniczone czasowo,
   mają dokładnie jeden request i nie wymagają trwałej zmiany konfiguracji.
6. Produkcyjny, autoryzowany ingress TTS jest dostępny; każda mutacja i każde
   przywrócenie baseline mają jawnie zatwierdzony zakres. Warunek STOP obejmuje
   utratę READY, zmianę `bootId`, panic/WDT, nieoczekiwane źródło/playback albo
   utratę logu.

## 10. Granice dokumentacyjne

Repozytorium zawiera raport zamknięcia A/B/C oraz raport zamknięcia D.
Niektóre starsze raporty wymienione w `docs/TODO.md` nie występują obecnie w
`reports/`; ich wyniki są zachowane w kanonicznym zapisie TODO, lecz pełne
treści nie były dostępne do ponownego niezależnego audytu. Nie jest to dowód
regresji i nie zmienia historycznych PASS.

Nie zmieniono firmware, konfiguracji, narzędzi ROM Recovery ani urządzeń. Bez
commit, tag i push.
