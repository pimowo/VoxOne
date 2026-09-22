# BLUETOOTH ARCHITECTURE

## Current migration status

MAIN `App` no longer starts local Bluetooth. Optional VoxOneBT owns the
Classic controller, Bluedroid, A2DP/AVRCP and pairing. MAIN waits up to
1500 ms for protocol-v1 `READY`, marks an absent module Unavailable without
blocking RADIO or boot, and accepts a late `READY`. UART work per loop is
bounded; unknown and overlong frames are discarded. MAIN I2S RX/PCM routing
is not active, so BT audio remains unavailable even when UART is Ready.
The old DESK lifecycle A/B/C passages below are historical. Their MAIN
source files and A2DP dependency have been removed; VoxOneBT keeps its own
A2DP library. The split has not been tested on hardware.

Poniższy opis lokalnego `BluetoothService` jest historyczny; MAIN nie
kompiluje już tej klasy. Osobny projekt VoxOneBT jest zaimplementowany,
ale bez testu na drugim ESP32.
Źródło prawdy: [MASTER_SPEC](MASTER_SPEC.md).

## Historyczny DESK — lokalny BT i otwarte ryzyko

A2DP Sink, AVRCP, metadane, initial volume sync i dwukierunkowa regulacja
zostały potwierdzone sprzętowo. Bonding pozostaje po zatrzymaniu profilu.
Lokalny gain PCM RADIO/BT 0..100 działa; AVRCP przelicza tylko skalę na
granicy protokołu.

| Próba lifecycle przy BT -> RADIO | Wynik |
|---|---|
| A: stop A2DP/AVRCP, Bluedroid i controller nadal aktywne | odrzucony: za mało RAM dla alokacji Helix; powrót profilu przez resume także zawiódł |
| B: stop A2DP/AVRCP, deinit Bluedroid, controller retained | historyczny test: RADIO grało, BT po ponownym starcie profilu działał, bonding zachowany; telefon mógł połączyć się sam |
| C: stop profilu, Bluedroid i controller | RAM dla RADIO wystarcza, ale powrót BT/reconnect jest problematyczny |

Po wariancie B wymuszone outbound `connect_to(last BDA)` kończyło się
`Connecting -> Disconnected`; połączenie inicjowane później przez telefon
działa bez ponownego parowania. Nie utożsamiać tego z niezawodnym
automatycznym powrotem BT. Assert `host_recv_pkt_cb hci_hal_h4.c:580 (0)`
oznacza nieudaną alokację bufora przychodzącego HCI i jest **nierozwiązany**;
wystąpił również podczas pierwszej sesji po boot, więc nie przypisywać go
wyłącznie restartowi Bluedroid. Historyczna diagnostyka HCI została usunięta
z MAIN; ewentualny test HCI należy wykonać na osobnym VoxOneBT.

## VoxOneBT — osobny moduł

Klasyczny ESP32U przejmuje tylko Bluetooth Classic A2DP Sink, AVRCP,
metadane, transport telefonu i PCM. Nie prowadzi Wi-Fi, WWW, radia,
MQTT/HA ani wyświetlacza. Main pozostaje właścicielem source intent,
PLAY_MEDIA, głośności końcowej i jedynego DAC.

```text
telefon --A2DP/AVRCP--> VoxOneBT --I2S PCM--> Main --AudioRouter/I2S TX--> DAC
                              <-- UART control/status -->
```

MAIN używa wyłącznie adaptera UART `RemoteBluetoothBackend`. Core nie
inicjalizuje lokalnego Bluetooth Classic. DIN/SALON z BT będą wymagały
osobnego klasycznego ESP32U. Firmware VoxOneBT i parser UART istnieją;
I2S RX/PCM router po stronie MAIN nadal nie są podłączone.

## Historyczny szkic UART v1

Prosty protokół tekstowy UTF-8, jedna ramka zakończona LF; CR przed LF
ignorowany. Komendy Main -> BT: `PLAY`, `PAUSE`, `NEXT`, `PREV`,
`GET_STATUS`, `SET_VOLUME n` (`n` w skali logicznej 0..100).
Zdarzenia BT -> Main: `CONNECTED`, `DISCONNECTED`, `PLAYING`, `PAUSED`,
`ARTIST text`, `TITLE text`, `ALBUM text`, `SAMPLE_RATE hz`, `VOLUME n`.
Po `GET_STATUS` moduł powinien odesłać pełny aktualny snapshot zdarzeń;
po starcie przewidziany jest handshake wersji protokołu przed sterowaniem.

Tekst metadanych może zawierać znaki końca linii i separatora, więc przed
implementacją trzeba ustalić escaping albo długość pola, limit ramki i
obsługę overflow. Trzeba też ustalić ACK/błędy, sekwencję zdarzeń po
reconnect, baudrate, piny, master clock I2S i timeout UART. Żaden pin ani
tempo łącza nie jest tutaj deklaracją gotowego hardware.
`SET_VOLUME` służy synchronizacji AVRCP/telefonu; główny gain PCM stosuje
Main, bez podwójnego tłumienia w module BT.

Podczas przyszłego PLAY_MEDIA telefon powinien pozostać połączony:
Main żąda PAUSE, odtwarza media, potem żąda PLAY, jeśli BT wcześniej grał.
Utrata UART/I2S RX nie może zatrzymać Main ani pozostawić dzierżawy audio
zablokowanej.
## VoxOneBT implementation checkpoint

The parallel firmware now lives in `VoxOneBT/` (separate PlatformIO project).
It starts A2DP Sink/AVRCP once and keeps the stack running regardless of
Main's source intent. PCM is I2S TX master to a future Main I2S RX slave;
UART v1 carries commands and metadata. The exact wire contract and limits
are in [VOXONE_BT_PROTOCOL](VOXONE_BT_PROTOCOL.md). There is no Wi-Fi,
WebService, radio, display, MQTT, HA, StationStore or NTP in this firmware.

MAIN has `BtLink` and `RemoteBluetoothBackend` only. It starts the UART
control link when the external-BT feature is enabled; absent module reaches
Unavailable without blocking RADIO or WWW. BT source selection is rejected
until the future I2S RX/router stage is complete.

Separating MCU memory removes Helix/A2DP RAM competition on Main and avoids
Bluetooth teardown on Main source switches. It does **not** prove the HCI
assert is fixed: that assert also occurred during the first BT session.
VoxOneBT needs its own long-running HCI/heap stress test before migration.
