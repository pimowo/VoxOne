# VOXONE-RC1-L0-SOURCE-MANAGER-HIL-1

Data: 2026-11-03, 21:04–21:08 CET. Wynik: **PREFLIGHT PHYSICAL PASS /
OGRANICZONY MONITORING PASS / RC1-SOURCE-MANAGER PARTIAL**.

Nie wykonano OTA, flashowania, restartu, resetu VoxOneBT, odłączania
zasilania ani przewodów, zmian konfiguracji trwałej, TTS, MQTT/HA ani testu
przeciążeniowego. Nie powtarzano zaliczonych normalnych cykli A2DP.

## Audyt kontraktu i brakujący zakres

Kontrakt `docs/STABLE_CONTRACT.md` wymaga odrębnego traktowania:

- utraty połączenia peer-a A2DP;
- utraty runtime/heartbeat samego modułu VoxOneBT.

Pierwszy zakres ma już fizyczne dowody na L0 `4ef1e53`: kontrolowany peer
Raspberry/BlueALSA podczas PLAY, fallback RADIO PLAY, reconnect bez autoplay,
ręczny priorytet RADIO i terminalną stabilność normalnego cyklu. Zachowane
dowody to przede wszystkim `VOXONE-L0-BT-LIFECYCLE-HIL-FIX-1` oraz 13 pełnych
cykli `VOXONE-L0-BT-STRESS-V2-FINAL-1`. Nie są one dowodem zaniku modułu.

Utrata runtime ma historyczny funkcjonalny PASS na `acdaeb5`: dwa ręczne
resety VoxOneBT potwierdziły osobno RADIO PLAY i RADIO STOP. Tamten przebieg
zakończył się jednak korupcją kolejki WebSocket i zmianą `bootId` podczas
końcowego restore. Obecny firmware zawiera późniejsze poprawki WebSocket, ale
nie istnieje produkcyjna komenda ani zatwierdzony izolator pozwalający
zatrzymać wyłącznie heartbeat/UART bez resetu modułu, odłączenia przewodu
albo zmiany firmware. Zgodnie z zasadami zadania nie wykonano improwizowanego
fault injection ani resetu VoxOneBT.

## Preflight

| Element | Wynik | Dowód |
|---|---|---|
| Repozytorium | PASS | HEAD `81f984e459ab`; zachowano lokalne zmiany `docs/TODO.md` i nieśledzony raport P1. |
| MAIN L0 | PASS | `0.8.0-dev`, build `4ef1e53`, profil L0. |
| READY / boot | PASS | `ready=true`, stały `bootId=3cb6512e`. |
| OTA / rollback | PASS | `app1/app1`, `VALID`, `pending=false`, `rollbackAvailable=true`, `firmwareUpdateReady=true`. |
| WWW | PASS | READY, verified, compatible; assetSet `1d7762d8eac4570ba7bee2954ac86abb3d32af61842b681989a9219275915c61`; GET `/` po jednym przekierowaniu HTTP 200. |
| VoxOneBT | PASS | `0.6.7-dev`, online, protocol 2, capability A2DP/AVRCP/ABSVOL/I2S/VU/diag/update; port USB tylko zinwentaryzowano, nie otwierano. |
| Raspberry BT | PASS | BlueZ, BlueALSA i `bluealsa-aplay` active; `VoxOneBT-EFF35A` `30:76:F5:EF:F3:5A` paired/bonded/connected; endpoint A2DP Source obecny. |
| UART MAIN | PASS | FT232R `A7039N0Z`, pasywny RX-only; port by-id zgodny i wolny przed otwarciem loggera. |
| Zasilanie Raspberry | PASS z historią | Bieżące bity throttlingu wyzerowane, sticky `0x50000`. Dwa historyczne incydenty undervoltage 2026-11-02 o 20:15 i 23:07 zostały znormalizowane; od baseline'u 2026-11-03 21:04 nie pojawił się nowy wpis. |

Stan wejściowy: stacja #4 `Złote Przeboje Wrocław`, RADIO/PLAY/playing,
volume 5 (`volume100=10`), MUTE OFF, balance 0. Peer Raspberry był połączony,
ale aktywne RADIO zachowywało ręczny priorytet.

## Wyniki scenariuszy

| Scenariusz | Status | Zakres dowodu |
|---|---|---|
| Utrata połączenia peer-a BT podczas PLAY | **PHYSICAL PASS** | Zachowany kontrolowany dowód L0 `4ef1e53`: disconnect peer-a Raspberry podczas potwierdzonego PCM PLAY wrócił do RADIO/PLAY i tej samej stacji. To nie jest utrata modułu. |
| Reconnect peer-a bez niezamierzonego autoplay | **PHYSICAL PASS** | Zachowany dowód L0 `4ef1e53`: reconnect bez PCM dał BT/STOP; dodatkowo wiele cykli stress zachowało ten kontrakt. |
| Kontrolowana utrata heartbeat/runtime VoxOneBT i powrót | **BLOCKED** | Brak bezpiecznego, nieinwazyjnego mechanizmu. Historyczne resety modułu dały funkcjonalny PASS, ale terminalny FAIL; nie powtórzono ich pod obecnym zakazem resetu. |
| RADIO PLAY przy utracie peer-a | **PHYSICAL PASS** | Zachowany fallback do RADIO/PLAY, ta sama stacja, bez dodatkowej komendy PLAY. |
| RADIO STOP przy utracie peer-a | **PHYSICAL PASS** | Zachowany osobny L0 HIL: RADIO/STOP pozostał STOP przez 15 s/24 ramki bez autoplay. |
| RADIO PLAY/STOP przy utracie modułu | **PENDING / BLOCKED** | Historyczny funkcjonalny PASS obu intencji nie ma terminalnej stabilności. Bez bezpiecznego fault injection nie przyznano bieżącego PASS. |
| Ręczny wybór RADIO i priorytet wobec zdarzeń BT | **PHYSICAL PASS** | Zachowane dowody manualnego `RADIO -> BT -> RADIO` oraz RADIO utrzymanego przy BT connected+playing. Bieżący preflight także zastano w RADIO/PLAY przy `btConnected=true`, bez wykonywania nowej mutacji. |
| Stabilność po powrocie peer-a i brak opóźnionej komendy PLAY | **PHYSICAL PASS** | Zachowany normalny cykl L0 ma stały bootId i restore; bieżący monitoring nie wykazał opóźnionej zmiany stanu. |
| Stabilność po powrocie samego modułu | **PENDING / BLOCKED** | Nie może być wywnioskowana z reconnectu A2DP; wymaga osobnego bezpiecznego mechanizmu zaniku heartbeat/runtime. |

Określenie peer-a w PASS odnosi się do kontrolowanego źródła
Raspberry/BlueALSA użytego przez stanowisko. Nie rozszerza automatycznie
dowodu na pełną macierz konkretnych telefonów ani LG TV.

## Monitoring i stan końcowy

- 13/13 próbek przez 120 s: READY, `bootId=3cb6512e`, app1 VALID, WWW READY,
  VoxOneBT online/protocol 2;
- `freeHeap` 103520–103552 B, stałe `minimumFreeHeap=87752 B`, stałe
  `psramFree=7805371 B`;
- pasywny UART 180,071 s: PASS integralności, 10274 B, 114/114 niepustych
  odczytów, zero niezapisanych bajtów, stały bootId;
- zero panic/assert/WDT/stack canary/heap corruption; wszystkie raporty audio
  miały `errors=0`, `writeErrors=0`, `shortWrites=0`. Rosnący licznik
  `timeouts` odpowiadał brakowi PCM przy połączonym peer-ze i aktywnym RADIO,
  nie błędowi UART;
- zero nowych incydentów undervoltage po baseline;
- stan końcowy jest zgodny z wejściowym: stacja #4, RADIO/PLAY/playing,
  volume 5 / 10%, MUTE OFF, balance 0, peer nadal connected;
- końcowo L0 pozostaje READY, `app1 VALID`, rollback dostępny, WWW READY,
  VoxOneBT online/protocol 2, bez restartu i bez zmiany konfiguracji.

`RC1-SOURCE-MANAGER` pozostaje **OPEN / PARTIAL**. Nie przyznano pełnego
PASS, ponieważ utrata samego modułu/heartbeat i terminalna stabilność po
jego powrocie pozostają nieweryfikowane na bieżącym obrazie.

Dowody:
`/home/piotrek/voxone-hil/RC1-L0-SOURCE-MANAGER-HIL-1-20261103T2110+0100/`.
Najważniejsze SHA-256:

- `SUMMARY.json`: `d328edbd347cd2a9773f360881ddca1cbd0b66e8551417fa76d2d4363437d54b`;
- monitoring `SHA256SUMS`: `633a984f795ef5a1f1d72688b7f9c3fac57c30a94e3b02418e7d64841b53f5b3`;
- manifest UART: `668d039c900102e79412a8772fd209e0184ba85b072ec81cec52266ec137acfd`;
- UART raw: `3a7d0834cc39a40270a992c8f8603fd6bd05d3767e1173cee77cff71ae61457d`;
- final snapshot: `8c42c001a282672699c36293d61a480fb36ca48a3f86117d34e66e8c26ac3520`.
