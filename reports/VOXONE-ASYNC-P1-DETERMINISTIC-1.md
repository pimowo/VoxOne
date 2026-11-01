# VOXONE-ASYNC-P1-DETERMINISTIC-1

Data: 2026-11-01. Weryfikacja wyłącznie offline na `main`, bazowy HEAD
`753a25857528149627c9c706ad1c5932e5c4a98a`. Fizycznego L0 `bf1d147`
nie kontaktowano, nie aktualizowano ani nie restartowano. Raport jest dowodem
software, nie kwalifikacją fizyczną kandydata ani nową listą zadań; statusy
release pozostają w `docs/TODO.md`.

## Wynik

**SOFTWARE PASS** dla dwóch odrębnych ścieżek. **PHYSICAL HIL BLOCKED /
UNVERIFIED** dla rzeczywistego `_lagging` i kodu 1013 na L0. Nie znaleziono
nowego defektu wymagającego naprawy produkcyjnego firmware. Nie zmieniono
`src/`, kontraktu WS, endpointów, konfiguracji ani profili. Pełny
`tools/verify.ps1` po zmianach testowych zakończył się `RESULT: PASS`:
158/158 testów native, sanitizery i wymagane buildy profili przeszły.

## Dokładne warunki i pokrycie

| Ścieżka | Warunek produkcyjny | Deterministyczny dowód hostowy |
|---|---|---|
| AsyncWebSocket `_lagging` | Na ESP32 domyślny `WS_MAX_QUEUED_MESSAGES=128`, lecz `_queueMessage()` stosuje `highWater=min(limit,32)`. Po 32 oczekujących wiadomościach następna próba ustawia `_lagging` dokładnie raz i zwalnia odrzuconą wiadomość. Następny `_onPoll()` wykonuje `AsyncClient::close(true)` po zwolnieniu `_queueLock`; disconnect sprząta kolejkę. To zamknięcie TCP, bez wymogu ramki 1013. | `websocket_queue_lifecycle_native` wymusza 32 przyjęte i dwie odrzucone wiadomości, jeden raport lagging, jeden wymuszony close, brak drugiego close, bilans utworzeń/zniszczeń oraz ścieżkę timeout. Kontrola źródła wiąże model z wyrażeniem progu i miejscem `close(true)`. |
| AsyncTCP cleanup przy 32/32 | Osobna kolejka zdarzeń `XQUEUE_SIZE=32`; jej jedyny konsument nie może blokować się na własnym pełnym queue podczas `CLEAR`. | `asynctcp_lifecycle_native` zapełnia 32/32, usuwa zdarzenia zamykanego klienta bezpośrednio, uzupełnia i przetwarza zdarzenia zdrowego klienta w zachowanej kolejności, sprawdza sloty, `pbuf` i jednokrotne zwolnienie. Nowa kontrola źródła wiąże model z `XQUEUE_SIZE=32`. |
| NetServer 1013 | `kNetserverQueueLength=20`; `requestOnChange()` używa `xQueueSend(...,0)`. Po zajęciu 20 miejsc nowe, niekoalescowane żądanie klienta jest odrzucane, rezerwacje są zwalniane i planowane jest odrzucenie tego klienta. `processOverloadRejection()` poza callbackiem wywołuje `websocket.close(clientId,1013,"Server busy; reconnect")`. Broadcast jest odraczany zamiast zamykania klienta. | `netserver_backpressure_native` zapełnia 20 miejsc, odrzuca dwa żądania klienta 41 i potwierdza dokładnie jedno zamknięcie 1013 z właściwym powodem. W tym samym pełnym queue żądanie klienta 42 zostaje później obsłużone i nie jest odrzucone. Istniejące próby obejmują koalescencję GETINDEX, częściowy snapshot, disconnect, retry MQTT i brak czekania konsumenta na własną kolejkę. Nowa kontrola źródła wiąże model ze stałą 20. |

Istniejące testy wykorzystano ponownie; nie dodano drugiego harnessu ani
testowej ścieżki w firmware. Test NetServer kompiluje rzeczywisty
`netserver_backpressure.h`, natomiast AsyncWebSocket i pełny NetServer są
modelowane z kontrolami kontraktu tekstu produkcyjnego. ASan/UBSan/LSan
sprawdzają modele hostowe, nie dowodzą braku wszystkich wyścigów lub awarii
na ESP32. Brak timeoutu, błędu sanitizerów, niezamkniętych zasobów modelu,
double-free lub use-after-free w wykonanych scenariuszach; nie jest to
twierdzenie o wszystkich możliwych interleavingach MCU.

## Wykonane kontrole

- Trzy zmienione testy skompilowane `g++ -std=c++11 -O2 -Wall -Wextra
  -Werror -Wno-unused-parameter -pthread` i uruchomione: **3/3 PASS**.
- Te same trzy testy skompilowane z `-fsanitize=address,undefined
  -fno-omit-frame-pointer`, uruchomione z `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`,
  `UBSAN_OPTIONS=halt_on_error=1` i limitem 20 s na test: **3/3 PASS**.
- Pełny `tools/verify.ps1`: **RESULT: PASS**. Native **158/158 PASS**,
  RADIO transport strict **16/16 PASS**, Native sanitizers **PASS** (w tym
  trzy zmienione testy P1), Web Update headless i diagnostyka mock HIL
  **PASS**. SPIFFS, zgodność i powtarzalność WWW, pełne obrazy L0/M0/S0/DIN0/W0,
  regresja walidacji obrazów, build L0 TX-only oraz `git diff --check`
  **PASS**. Diagnostyka HIL używała wyłącznie mock UART; nie kontaktowano L0.

## Granica dla L0 RC

SOFTWARE PASS nie zamyka `ASYNCTCP-SELF-DEADLOCK-1`, szerszego
`ASYNCTCP-EVENT-ALLOCATION-OOM-1` ani `RC1-NO-P0-P1`. Nadal brak bezpiecznego,
fizycznego dowodu, że rzeczywisty opóźniony klient L0 osiąga `_lagging`, że
UART rejestruje odpowiadający komunikat i że klient zdrowy oraz HTTP/WS/MQTT,
READY, bootId i heap pozostają stabilne bez panic/WDT. Osobno brak zachowanej
ramki close 1013 przy rzeczywistym przeciążeniu NetServer. Nie utożsamiać
wewnętrznego TCP close `_lagging` z zamknięciem WebSocket 1013. Nie powtarzać
ryzykownego floodingu; fizyczny HIL wymaga osobno zatwierdzonego, ograniczonego
harnessu i pasywnego UART. Rekomendacja: zachować L0 RC **NOT READY** według
bieżących bramek, bez zmiany statusu P1 na podstawie niniejszego raportu.
