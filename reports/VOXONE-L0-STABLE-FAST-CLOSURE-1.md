# VOXONE-L0-STABLE-FAST-CLOSURE-1

Data: 2026-10-31. Zakres: decyzja o kandydacie L0 RC bez zmiany firmware,
urządzeń, brokera ani zamrożonego kontraktu. Źródłem otwartych wymagań jest
`docs/TODO.md`; raport grupuje je dla decyzji, nie tworzy drugiego backlogu.

## Werdykt

**L0 RC NOT READY. Stable 1.0.0 NOT READY.** Dwa profile S0 i L0 są obowiązkowe;
S0+VoxOneBT nie jest jeszcze capability bieżącego BIN. L0 nie ma końcowego
fizycznego HIL dokładnego czystego kandydata, a wymagane P1 `_lagging` i
NetServer close 1013 oraz TTS A/B/C na docelowym kandydacie nie mają
kwalifikujących dowodów. Nie przeniesiono żadnego wymagania release-gating do
NON-BLOCKING.

## Licznik formalnych bramek

Jednostką liczenia jest **12 kanonicznych bramek RC1** z TODO i
`docs/STABLE_CONTRACT.md`, nie każdy historyczny przebieg ani podtest.
`BLOCKER` oznacza tu potwierdzony, *nienaprawiony w bieżącym kodzie* P0/P1;
`BLOCKED` oznacza brak bezpiecznej lub dostępnej drogi do pełnego HIL.

| Klasa | Liczba | Bramki |
|---|---:|---|
| DONE | 2 | `RC1-RADIO-ENDURANCE`, `RC1-LR-VU` — zakres zamknięty w TODO; osobny BALANCE endpoint nie jest tym samym testem. |
| BLOCKER | 0 | W aktualnym `VoxOne` HEAD nie ustalono otwartego P0/P1 z nienaprawioną ścieżką kodu. Otwarte P1 mają poprawkę software, ale brak wymaganej kwalifikacji fizycznej; to **nie** oznacza zamknięcia `RC1-NO-P0-P1`. |
| MISSING EVIDENCE | 4 | `RC1-PROFILE-L0`, `RC1-BT-LIFECYCLE`, `RC1-SOURCE-MANAGER`, `RC1-FROZEN-RELEASE`. |
| BLOCKED | 6 | `RC1-PROFILE-S0` (pinout/BT), `RC1-TTS-RESTORE` (fixture A/B/C), `RC1-UPDATE`, `RC1-RECOVERY`, `RC1-CONFIG-V7` (bezpieczna fizyczna asekuracja dla destrukcyjnych ścieżek), `RC1-NO-P0-P1` (brak bezpiecznego, deterministycznego harnessu `_lagging`/1013). |
| NON-BLOCKING | 0 | Żadna z 12 bramek nie została zdegradowana. Już zatwierdzone opcjonalne warianty i POST-STABLE są poza licznikiem. |

Licznik pozostaje **10/12 OPEN, 2/12 CLOSED**. Kategoria BLOCKED jest
operacyjna: nie zwalnia z bramki. Przeniesienie któregokolwiek wymagania do
NON-BLOCKING wymaga odrębnego uzasadnienia i decyzji użytkownika.

## Klasyfikacja otwartych prac L0 i zachowanych PASS

Poniższe zakresy są podzadaniami bramek i **nie są ponownie doliczane** do
licznika 12. Kategorie opisują brakujący fragment, a `DONE` wyłącznie
udokumentowany zakres PASS.

| Zakres z TODO | Klasa | Dowód lub rzeczywisty brak |
|---|---|---|
| RADIO endurance, transport i logiczny STOP/PLAY | DONE | Terminalny 14 400,922 s, 240/240 READY/UART, stacja i bootId stałe; osobne STOP→PLAY ma PASS. |
| HTTP/chunked recovery i timeout transportu RADIO — pełny HIL | MISSING EVIDENCE | `radio_transport_native --strict` ma 16/16, a native `radio_hls_recovery`, `radio_reconnect` i `return_player_timeout` przechodzą; to zachowany software PASS, nie fizyczna macierz realnych streamów, Wi-Fi loss i dead stream. |
| Odsłuch RADIO i reakcja LCD/VU na dokładnym finalnym obrazie | MISSING EVIDENCE | Logika i codzienna obserwacja nie zastępują końcowej oceny fizycznej. |
| Pasywny `L0_LOG_RX` | DONE | RX-only odbierał UART; późniejsze sesje 240/300 s i SHA zachowują integralność. Pomiary napięć i kwalifikacja elektryczna pozostają MISSING EVIDENCE. |
| Inwentaryzacja USB rozdzielona DEV/bridge | MISSING EVIDENCE | Narzędzie 4/5 albo 1/5 nie jest jeszcze pełną walidacją rejestru; nie usuwać wpisu jako „tylko tooling”. |
| Analogowe L/R i odpowiadające paski VU — bazowa bramka | DONE | Kontrolowany BT WAV 19 s z jednoznaczną obserwacją użytkownika zamknął `RC1-LR-VU`; późniejsza niejednoznaczna próba BALANCE tego PASS nie cofa ani nie rozszerza. |
| `BALANCE-ENDPOINT-1` na L0 | BLOCKED | Odczyty −16/−15/0/+15/+16 i restore 0 istnieją, lecz audio/VU jest UNVERIFIED; brak wiarygodnej metody fizycznego rozróżnienia, nie PASS/FAIL. |
| BT A2DP/AVRCP normalne connect/disconnect/reconnect, volume/MUTE cross-core | DONE (częściowy zakres) | ASUS/LG/telefon dały udokumentowane funkcjonalne PASS; volume 20→15→20, MUTE PLAY/PAUSE/disconnect, reconnect/STOP bez autoplay i bez trzasków mają fizyczny PASS. |
| Cały BT lifecycle, VU podczas utraty/reconnect, końcowy SourceManager i BT-loss I2S0 | MISSING EVIDENCE | Etapowe PASS nie obejmują pełnej terminalnej stabilności i wszystkich wymaganych przejść na finalnym kandydacie. Historyczny crash przy restore nie został przeniesiony na nowy obraz jako potwierdzony defekt. |
| VoxOneBT `copyPeerName()` | BLOCKED | Jednobajtowy odczyt poza granicą ma potwierdzoną poprawkę software w **osobnym repozytorium**, lecz nie jest wdrożony; wymaga osobnego checkpointu, autoryzowanej aktualizacji modułu i ograniczonego HIL. Nie klasyfikować go bez decyzji jako P1 w bieżącym `VoxOne` HEAD. |
| TTS D, normalny EOF, przerwanie i BT-loss | DONE (częściowy zakres) | Scenariusz D ma fizyczny PASS 3 648 044/3 648 044 B, natural EOF +1, cleanup +1; poprzednie restore/interrupt mają własny PASS. |
| TTS A/B/C na docelowym obrazie, error/timeout | BLOCKED | Historyczny PASS na `fbbdfbc` pozostaje; oryginalnych fixture, manifestów i raportów na DEV-VM brak. Nie odtwarzać z domysłów ani zastępować HIL symulacją. |
| MAIN OTA dodatnie, rollback slotu, WWW READY/verified | DONE (częściowy zakres) | Poprzednie autoryzowane MAIN OTA i WWW mają fizyczny PASS; nie dowodzą negatywnych ścieżek ani finalnego HEAD. |
| SPIFFS/MAIN abort, rollback negatywny, legacy migration i BT sender failure | BLOCKED | Wymagają osobnych zatwierdzonych scenariuszy, kopii/rollbacku i fizycznej drogi odzyskania; nie wykonywać zdalnie w ramach zbiorczego verify. |
| Recovery, FIRST BOOT i Config v7/power-loss — destrukcyjne L0 HIL | BLOCKED | Implementacja i software baseline istnieją, lecz w tym zadaniu nie ma fizycznej asekuracji, backupu/restore i zatwierdzonego scenariusza. Nie przenosić wyników M0 na L0. |
| SECURITY, WWW i Config v7 — niedestrukcyjne L0 readbacki | MISSING EVIDENCE | Software i historyczne M0 PASS istnieją, ale pełny fizyczny L0 zakres pozostaje otwarty. |
| WebSocket lifetime 190,296 s, RST/reconnect, pełny GETINDEX, HTTP/WWW | DONE (częściowy zakres) | Jeden aktywny klient, close 1000, stały bootId, READY, heap i UART potwierdzone fizycznie. Nie powtarzać bez nowej przesłanki. |
| AsyncTCP `_lagging` próg 32 i osobny NetServer close 1013 | BLOCKED | Software/native/sanitizer PASS, ale fizycznie nie osiągnięto obu stanów; bez ograniczonego bezpiecznego harnessu i mierzalnych markerów nie zaliczać P1. `_lagging` zamyka TCP, nie jest synonimem 1013. |
| AsyncTCP OOM i Config runtime snapshot | MISSING EVIDENCE | OOM ma deterministyczny ASan/UBSan i zaakceptowany nieinwazyjny L0 lifecycle; pełny P1 pozostaje otwarty przez `_lagging`/1013. Snapshot Config ma software/TSan PASS, lecz osobny zakres fizyczny w TODO pozostaje. |
| MQTT HA volume i retained volume | DONE (częściowy zakres) | HA→L0 20→15→20, WS/readback oraz retained 51→38→51 potwierdzone; nie powtarzać. |
| MQTT retained `status` i `playlist` — pasywny odczyt | DONE (częściowy zakres) | Odczyt z nowej subskrypcji HA: QoS 0, Retain=true dla obu; późniejszy WS/UART zarejestrował zmianę metadata, ale nie ma odczytu HA po zmianie. |
| MQTT aktywny `status`/`playlist`, pełna HA parity | MISSING EVIDENCE | Aktywny `status` po zmianie metadata nadal UNVERIFIED; zwykłe metadata nie publikują `playlist`. |
| MQTT reconnect, command resubscribe, availability/LWT i post-restart | BLOCKED | Nie ma wspieranej izolowanej komendy MQTT-only reconnect; LWT nie jest skonfigurowany. Potrzebne osobne narzędzie/uzgodniony scenariusz i decyzja kontraktowa, nie restart brokera w tym zadaniu. |
| WWW kontrolki, SECURITY L0, DISPLAY/SCROLL/branding, kodeki/playlisty/Radio Directory, audio glitch/USB ground | MISSING EVIDENCE | Software i wybrane zdalne/eksploatacyjne PASS nie zastępują oceny bazowego L0 ani testów realnych formatów i fizycznego toru. Destrukcyjne mutacje playlisty/power-loss pozostają pod rygorem osobnej asekuracji. |
| Dodatkowe display/audio runtime, XSMT, native HA, DLNA/AUX/DSPmini; W0/M0/DIN0 fizyczny HIL | NON-BLOCKING (już zatwierdzone) | Są opcjonalne lub POST-STABLE według zamrożonego kontraktu; LG TV PAUSE status jest osobno zaakceptowanym ograniczeniem, nie PASS dla raportowania PAUSE. Nie jest to nowa reklasyfikacja. |

## Tożsamość kandydata i delta

- `main` HEAD: `bf1d147958b6c4f6b66c29bf0e3bc416063a8814` (2026-10-31). Początkowy worktree był czysty; lokalne zmiany tego zadania obejmują tylko `docs/TODO.md`, test native MQTT i niniejszy raport.
- Najpóźniej odczytany fizyczny L0: `0.8.0/dev`, `b4b74cf-dirty`, `app1 VALID`, `bootId=e95a3cbe`, WWW READY/verified. To **runtime build label**, nie SHA obrazu ani dowód, że odpowiada czystemu `b4b74cf`.
- Od `b4b74cf` do HEAD zmieniły się produkcyjnie `src/core/netserver.cpp` (status aktualizacji VoxOneBT z `confirmedVersion`), `web-src/voxone.html`, `.css`, `.js` i ich spakowane assety/manifest; zmieniły się też testy i dokumentacja. Po `91426c9` następne commity do HEAD są dokumentacyjne. Zestaw WWW obecnie odczytany z L0 ma `assetSet=1d7762d8eac4570ba7bee2954ac86abb3d32af61842b681989a9219275915c61`, zgodny z manifestem repozytorium; **MAIN** nadal zgłasza `b4b74cf-dirty`, więc nie ma dowodu na dokładną parę MAIN+WWW z HEAD.
- Brak potwierdzonego, nadal nienaprawionego P0/P1 w aktualnym kodzie `VoxOne`; istniejące wpisy P1 to software-fixed/physical-unverified albo ograniczony fizyczny PASS. Osobna, niewdrożona poprawka memory-safety VoxOneBT i niezamknięte P1 HIL pozostają realnymi bramkami. Nie utożsamiać „brak nowego defektu w kodzie” z „L0 RC READY”.

## Pięć najważniejszych blokad i najkrótsza droga

1. **Dokładny obraz RC:** zamrozić czysty MAIN+WWW z obecnego kodu, zapisać BIN/ELF/SPIFFS/SHA i wykonać końcowy, ograniczony profilowy HIL L0. Obecny L0 `b4b74cf-dirty` nie kwalifikuje HEAD.
2. **P1 WS:** przygotować jeden bounded harness, który osobno obserwuje rzeczywiste `_lagging` i 1013 bez floodingu/OOM; jeśli nie da się bezpiecznie, pozostawić BLOCKED i uzyskać decyzję release-gating, nie pozorny PASS.
3. **TTS A/B/C:** odzyskać oryginalne fixture/manifesty i wykonać jeden ograniczony retest na docelowym obrazie; nie powtarzać D.
4. **Fizyczny baseline audio/BT/MQTT:** wiarygodnie domknąć BALANCE endpoint oraz brakujące BT lifecycle/SourceManager/BT-loss na dokładnym kandydacie; dla MQTT pozyskać dowody aktywnego retained i bezpiecznego reconnect/availability albo zachować BLOCKED. Nie powtarzać zaliczonych normalnych cykli BT i volume 20→15→20.
5. **Ryzykowne bramki update/recovery/persistence:** ustalić fizyczną drogę ratunkową, pełne kopie i osobne autoryzacje przed negatywnymi OTA/SPIFFS, FIRST BOOT/Recovery i power-loss; równolegle domknąć nieinwazyjne SECURITY/WWW/Config readbacki.

Automatycznie: verify native/sanitizers, kontrakty, WWW/firmware, obrazy wszystkich pięciu targetów, walidacja pełnych obrazów i `git diff --check`. Fizyczny użytkownik: ocena audio/LCD/VU, telefon/BT, zasilanie i recovery, izolowany logger, zgoda na każdy zapis/OTA oraz HA/broker tam, gdzie brak dostępu odczytowego. Do czasu spełnienia powyższych warunków nie tworzyć release commita ani wersji 1.0.0.

## Offline verification w tym zadaniu

- `pwsh -NoProfile -File tools/verify.ps1`: **RESULT: PASS**. Display branding,
  158 native, RADIO transport strict 16/16, ASan/UBSan, headless Web Update,
  diagnostyka HIL, SPIFFS, WWW 7/7, pełne obrazy L0/M0/S0/DIN0/W0,
  walidacja pełnych obrazów 8/8, L0 TX-only, powtarzalność WWW i
  `git diff --check` mają PASS. L0 MAIN 1 764 448 B (89,74% partycji),
  full.bin 16 777 216 B, SPIFFS 196 608 B.
- Roboczy build L0 zgłasza `bf1d147-dirty`, ponieważ bez commita w worktree
  leżą zmiany dokumentacji/testu. SHA-256: MAIN
  `6cac8eb0f9ab8a4929cc40c24960a58551696286f7363fc66b3081aef8854975`,
  ELF `fa78832e9b8ef70c2ece7857edeaf61732c24fc9560f20f52ac11f2e1ad06d90`,
  SPIFFS `b95578af4a8cf209c97d21918e8ac50d5e815e06f79f72c395cda0dfebc45ae2`,
  full.bin `06caf4de6da0769fd4a2676f6a6217f9c6c103dae753dd0599695842eb934411`.
  Artefakt jest **OFFLINE VERIFIED / NOT DEPLOYED / NOT RELEASE RC**.
- Po fazie native pełnego runnera uzupełniono wyłącznie
  `test/mqtt_contract_native.cpp`: dla `sessionPresent=false/true` sprawdza
  `command` QoS 2 oraz trzy publikacje QoS 0 retained przy `onMqttConnect`.
  Nową wersję testu osobno skompilowano i uruchomiono z PASS zwykłym oraz
  ASan/UBSan. Nie przypisywać nowych asercji do wcześniejszych 158 testów
  native pełnego runnera.
- Walidacja dokumentacji w tym zakresie: zgodność liczby/stanu 12 bramek
  między TODO a kontraktem, zachowanie historycznych PASS i brak zmiany
  release-gating; format zmian ma `git diff --check` PASS. Nie uruchamiano
  odrębnego narzędzia do skanowania wszystkich linków Markdown.
- Nie wykonano HIL ani żadnej operacji na L0/S0/M0/DIN0, HA lub brokerze.
  Firmware i konfiguracja urządzeń pozostały nietknięte; bez OTA, flash,
  restartu, commit i push.
