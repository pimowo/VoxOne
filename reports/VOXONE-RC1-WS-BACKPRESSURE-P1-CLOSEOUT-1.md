# VOXONE — RC1-WS-BACKPRESSURE-P1-CLOSEOUT-1

Data closeoutu: 2026-11-03, Europe/Warsaw. Repozytorium: `main`, HEAD
`81f984e459ab590cc2765651eaa986b1398747aa`. Fizyczny L0:
`192.168.1.56`, runtime build `4ef1e53`, profil L0, `app1 VALID`.
Closeout nie zmienia firmware, konfiguracji ani wersji, nie wykonuje OTA,
flashowania, restartu, commita ani push. Istniejący lokalny diff
`docs/TODO.md` dotyczący VoxOneBT został zachowany.

## Werdykt

- AsyncWebSocket `_lagging`: **PHYSICAL PASS**.
- NetServer overload WebSocket close 1013: **PHYSICAL PASS**.
- `ASYNCTCP-SELF-DEADLOCK-1 / NETSERVER-BACKPRESSURE-1`: **CLOSED**.
- `ASYNCTCP-EVENT-ALLOCATION-OOM-1`: **CLOSED** według wcześniej
  zatwierdzonego kryterium, które nie wymaga fizycznego wymuszania OOM.
- Poprawka produkcyjna dla tych dwóch ścieżek nie jest potrzebna.

Dowód pochodzi z jednorazowego przebiegu
`/home/piotrek/voxone-hil/RC1-WS-BACKPRESSURE-BOUNDED-HIL-1-20261103T192346+0100`.
Nie powtarzano zamkniętych testów heap corruption ani client lifetime.

## Preflight

L0 raportował `READY=true`, stały `bootId=3cb6512e`, `runningSlot=app1`,
`bootSlot=app1`, `imageState=valid`, `pending=false` oraz WWW READY i verified.
Pi `voxone-bridge` miał stały boot ID
`b6ce50f2-c961-4c08-88a5-6c54392b0462`. Bazowe
`get_throttled=0x50000` zachowuje dwa historyczne incydenty undervoltage od
startu Pi; aktywne bity były zerowe i od początku okna HIL nie pojawił się
nowy wpis undervoltage/throttling.

Pasywny `L0_LOG_RX` działał 115200 8N1 w trybie RX-only. Preflight WS zwrócił
dokładnie 10/10 zwalidowanych ramek i close 1000. Początkowo free heap wynosił
146504 B, minimum 131128 B, a PSRAM 7828731/8385655 B. Stan funkcjonalny:
RADIO/STOP, Italo4you, volume100=10, MUTE off.

## `_lagging`: korelacja klienta i UART

Próba trwała od `19:25:44.455` do `19:25:51.011`. Jeden klient z żądanym
`SO_RCVBUF=1024` wykonał pięć paczek `getindex`, `getsystem`, `getwebstatus`,
czyli dokładnie 15 komend bez retry. Odebrał 8 ramek / 587 B, po czym Linux
`recv()` zwrócił `ECONNRESET` (`Errno 104`). UART zapisał dokładnie jeden
marker `WS client #8 lagging; disconnecting` o `19:25:48.002`, wewnątrz tego
samego okna.

RST jest zgodny z kontraktem. `_lagging` nie wywołuje ramki WebSocket 1013;
następny `_onPoll()` wykonuje wymuszone `AsyncClient::close(true)` po
zwolnieniu blokady kolejki. Z punktu widzenia klienta takie zamknięcie TCP
może zakończyć się EOF albo resetem połączenia. Oba wyniki dowodzą zamknięcia
transportu, jeśli są skorelowane z markerem `_lagging`.

`lagging_probe.py` błędnie zapisał `FAIL / TRANSPORT_ERROR`, ponieważ jego
obsługa wyjątku uznaje za kandydata wyłącznie `TRANSPORT_EOF`.
`WebSocket._take()` zamienia natomiast każdy `OSError`, w tym linuksowy
`ConnectionResetError`, na `HarnessFailure("TRANSPORT_ERROR")`. Jest to luka
klasyfikatora testu; marker UART, TCP RST i pełne recovery wykluczają błąd
kontraktu firmware w tym przebiegu.

Po ponad 10 s nowy klient ponownie uzyskał 10/10 ramek i close 1000. Free heap
wynosił 146492 B, minimum pozostało 131128 B, a PSRAM nie zmieniła się.

## NetServer overload 1013

Próba trwała od `19:26:24.172` do `19:26:30.542`. Dokładnie sześciu klientów
po barierze wysłało po jednym zestawie `getrssi`, `getsystem`, `getwebstatus`,
`getscreen`: łącznie dokładnie 24 read-only komendy, bez GETINDEX i bez retry.
Klient 2 odebrał rzeczywistą ramkę close 1013 z dokładnym reason
`Server busy; reconnect`. Pozostałych pięciu klientów zakończyło własny
cleanup close 1000.

UART zapisał dokładnie jeden marker
`netserver queue overload: 1 request(s), last 17`, podzielony między chunki
`19:26:24.543` i `19:26:24.558`, około 0,37 s po rozpoczęciu próby. Jest to
osobna ścieżka globalnej kolejki NetServer; wynik nie jest przypisywany
per-client `_lagging`.

Po wymaganej przerwie recovery ponownie dało 10/10 i close 1000. Free heap
wynosił 146280 B, minimum 131128 B, a PSRAM była niezmieniona.

## Stabilność i stan końcowy

Nadzorca zapisał 283/283 zdrowe próbki w 296,612 s, zero STOP, z maksymalną
przerwą 1,689 s. Ostatnia próbka nastąpiła 231,852 s po końcu overload; UART
działał do 233,293 s po końcu overload. Końcowy WS po 213,938 s zwrócił
10/10 i close 1000.

Końcowy free heap wrócił dokładnie do 146504 B; minimum pozostało 131128 B,
a PSRAM 7828731/8385655 B. Stan funkcjonalny pozostał RADIO/STOP, Italo4you,
volume100=10, MUTE off. `bootId=3cb6512e`, READY, app1 VALID, WWW, HTTP, ping
i broker TCP pozostały stabilne. UART nie zawiera panic, assert, watchdog/WDT,
Guru Meditation, stack canary, heap corruption ani resetu.

Manifest UART ma status `INTERRUPTED` wyłącznie dlatego, że własny logger
zatrzymano po zakończeniu wymaganego monitoringu. Zapisał 16857 B w 126
chunkach, zero niezapisanych bajtów i `bootIdChanged=false`. Złożenie danych
hex wszystkich chunków jest bitowo równe `uart.raw`.

## Granica dowodu MQTT

Monitoring potwierdzał przez całe okno niezmienione `enabled=true`, broker
`192.168.1.14:1883`, `passwordSet=true`, effective root `voxone-8CBD1C` oraz
osiągalność TCP brokera. Produkcyjny endpoint nie eksportuje wyniku
`mqttIsConnected()`. Jednorazowy anonimowy read-only CONNECT zakończył się
`CONNACK 0x05`, więc tester bez poświadczeń nie mógł odczytać live/retained.
Ten przebieg nie jest niezależnym dowodem MQTT retained/live i nie powinien
być tak cytowany. Ograniczenie obserwowalności nie zmienia bezpośrednich
dowodów `_lagging`, 1013, recovery i stabilności sieciowej.

## Integralność artefaktów

- `uart.raw`: SHA-256
  `92707580fb51745acf7ed5b817994075da4a1acb76acb683b713ffa3bf856908`;
- `uart-chunks.jsonl`: SHA-256
  `c53f797fdd6e048e3a71898a9a0a2b1389d93ea9798e37d11c367da26127a911`;
- `lagging-probe.json`: SHA-256
  `dd93fde13237d8fd20593a3fccfa01e525aa24b0ddb2f0d35371b5791b160ee1`;
- `overload-probe.json`: SHA-256
  `a2a90e4f947e0213c2f197caa230a712ad166aae27dcc01df72cefd943a861a3`;
- `guard.jsonl`: SHA-256
  `bbe447d4a4ef41cab9e6059daa9001cea716539f662e948a7c038bb4fe6ed15b`.

## Rozliczenie P0/P1

`ASYNCTCP-SELF-DEADLOCK-1 / NETSERVER-BACKPRESSURE-1` spełnia software i
physical criteria i jest zamknięty. `ASYNCTCP-EVENT-ALLOCATION-OOM-1` ma
deterministyczną iniekcję wszystkich wymaganych ścieżek offline pod
sanitizerami; niniejszy HIL dostarcza wymagany nieinwazyjny lifecycle bez
fizycznej iniekcji OOM, więc także jest zamknięty.

W kanonicznym TODO nie pozostaje otwarty P0. Jedynym rzeczywiście otwartym P1
jest `BALANCE-ENDPOINT-1`: L0 AUDIO/VU pozostaje UNVERIFIED, a S0 HIL PENDING.
Z tego powodu bramka `RC1-NO-P0-P1` nadal jest otwarta. Pozostałe zadania
Stable i release-gating zachowują własne dotychczasowe statusy.
