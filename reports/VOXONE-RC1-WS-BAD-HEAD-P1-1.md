# VOXONE-RC1-WS-BAD-HEAD-P1-1

Werdykt: **FIX_READY — HIL PENDING**

## Dowody i stos

Incydent pochodzi z L0 `0.8.0-dev` / `ebb3c06-dirty`, `app0 VALID`.
Oryginalnych danych nie zmieniono. Dokładny ELF ma SHA-256
`0387309d5b785aac87573e3c9b2b9e7bbf2f5296f2245e4d2c3cf89da7dff500`
i zgadza się z prefiksem zapisanym przez panic. Surowy UART
`uart-current-2/uart.raw` ma SHA-256
`39421fac4ffc0b3e9f4efa2696151fab5e1c7b9c9745606b4aac7275de3cc86f`.

Symbolizacja całego stosu:

1. `panic_abort` → `esp_system_abort` → `__assert_func`;
2. `multi_heap_free` (`multi_heap_poisoning.c:259`) → `heap_caps_free` →
   `free`;
3. `AsyncWebSocketBasicMessage::~AsyncWebSocketBasicMessage()`
   (`AsyncWebSocket.cpp:317-318`);
4. callback usuwający wiadomość → `LinkedList::remove()`
   (`StringArray.h:128`) → `_runQueue()` (`AsyncWebSocket.cpp:553`);
5. `_onAck()` (`AsyncWebSocket.cpp:536`) → callback ACK →
   `AsyncClient::_sent()` (`AsyncTCP.cpp:936-941`) → `_s_sent()` →
   `_async_service_task()`.

UART zawiera pełne `CORRUPT HEAP: Bad head`, assert, backtrace, identyfikator
ELF i automatyczny boot `rst:0xc`. Logger nie zdążył utworzyć terminalnego
manifestu i zapis ma powtarzające się fragmenty, więc nie jest ogólnym UART
PASS, ale sam panic jest kompletny i potwierdzony zerwaniem WS oraz zmianą
`bootId` `db90a297` → `5f85372b`.

Bezpośrednio przed awarią zakończyły się poprawnie BT PAUSED, BT STOPPED i
BT-loss-during-TTS. Po ponad 30 s stabilnego RADIO/STOP ciągły klient wysłał
dokładnie jeden `start=1` o 14:11:43.376; ostatnia ramka WS (`meta=[łączenie]`)
przyszła o 14:11:43.699, potem połączenie zostało zresetowane i UART zapisał
panic. Nie wysłano czwartego TTS ani STOP. Przed panic 126 próbek `systemInfo`
nie pokazało trendu wyczerpywania pamięci: free heap 146852–162456 B,
minimum 137020–138728 B, PSRAM free 7793791–7828791 B. Nie jest to dowód
integralności każdego bloku heap, ale nie ma przesłanki wcześniejszej
degradacji zasobów.

## Potwierdzony defekt

Vendored `AsyncWebSocketClient` przechowywał wiadomości w niechronionym
`LinkedList`. `_queueMessage()` i wywołane przez niego `_runQueue()` mogą
pracować z tasku aplikacji podczas publikacji `textAll()`, natomiast `_onAck()`
i `_onPoll()` wywołują ten sam `_runQueue()` z tasku AsyncTCP. Dwa wywołania
mogły jednocześnie zobaczyć ten sam ukończony element czołowy, po czym oba
wykonać `remove()` i destruktor. `free(_data)` z drugiego przejęcia własności
wyjaśnia wykryty w dokładnie tym destruktorze `Bad head`; wartość zastępująca
canary ma postać wskaźnika heap. Dodatkowo broadcasty iterowały `_clients` bez
`_clientsLock`, więc disconnect mógł równolegle usunąć klienta.

To jest konkretny defekt synchronizacji i własności odpowiadający stosowi.
Zachowany log nie zawiera śladu schedulerowego obu równoczesnych wywołań,
dlatego nie deklarujemy, że historyczny przeplot został bezpośrednio
zaobserwowany ani że jest wspólny z dawnym M0 `Bad tail`. Utrzymywana
implementacja ESP32Async również chroni ACK, poll, enqueue, disconnect i
`_runQueue()` osobnym mutexem kolejki oraz wymaga wywołania `_runQueue()` pod
blokadą: <https://github.com/ESP32Async/ESPAsyncWebServer/blob/main/src/AsyncWebSocket.cpp>.

## Minimalna poprawka

- dodano per-client `AsyncWebLock _queueLock`;
- pod tą blokadą działają ACK, poll, enqueue, stan, cleanup/destruktor oraz
  wszystkie odczyty i modyfikacje kolejek;
- `_runQueue()` przyjmuje kontrakt „caller holds `_queueLock`” i nie działa po
  odłączeniu `_client`;
- `textAll()` i pozostałe broadcasty utrzymują `_clientsLock`, aby klient nie
  został usunięty podczas iteracji;
- disconnect zeruje `_client` i stan pod blokadą kolejki, a dopiero potem usuwa
  klienta z listy;
- ACK/poll/timeout zwalniają guard przed `AsyncClient::close()`, ponieważ jego
  callback disconnect jest synchroniczny i może usunąć bieżący obiekt.

Nie zmieniono istniejącej poprawki lifecycle AsyncTCP, transportu, TTS,
SourceManager ani funkcji produktu.

## Regresja

Nowy `websocket_queue_lifecycle_native` sprawdza kontrakt blokad w kodzie
produkcyjnym, deterministycznie modeluje dawny podwójny transfer własności i
wykonuje 20000 współbieżnych rund enqueue/ACK zakończonych disconnectem.
Każda wiadomość ma dokładnie jeden destruktor.

PASS:

- `websocket_queue_lifecycle_native`: zwykły oraz ASan/UBSan;
- `websocket_command_native`: ASan/UBSan;
- SECURITY: `security_http_adapter_native` dla W0/M0/L0 oraz
  `security_session_native`, `security_runtime_native`: ASan/UBSan;
- SourceManager: `source_manager_native`, `source_connect_restore_native`,
  `source_stop_native`, `bt_source_switch_native`, `temporary_audio_native`,
  `radio_fallback_contract_native`: ASan/UBSan;
- produkcyjne buildy: W0 1650064 B, M0 1639824 B, L0 1718496 B;
- `git diff --check`.

Buildy mają profil zgodny z manifestem, produkcyjny SECURITY oraz identity
`ebb3c06-dirty`. Są walidacją kompilacji, nie nowym zestawem HIL ani zgodą na
OTA.

## Minimalny HIL i stan RC1

P1 pozostaje otwarty. Po osobno zatwierdzonym OTA poprawionego L0 należy:

1. uzbroić świeży, ciągły UART i potwierdzić wyjściowy `bootId`/VALID/READY;
2. z aktywnym ciągłym WS wykonać raz dokładny RADIO STOP → pojedynczy
   `start=1`;
3. wykonać ograniczony connect/status/disconnect/reconnect, aby przeciąć
   ścieżki enqueue, ACK i cleanup bez floodingu;
4. obserwować UART i heap co najmniej 180 s; wymagane są stały `bootId`,
   poprawny RADIO/WWW/WS i brak panic/assert/watchdog/korupcji heap.

Zaliczone scenariusze TTS pozostają zaliczone. Nowy P1 ponownie otwiera bramkę
„brak otwartych P0/P1”, dlatego aktualny licznik to **12/13 otwartych bramek
RC1**, a nie 11/13.
