# VOXONE-RC1-L0-TTS-RESTORE-HIL-1

Data: 2026-10-25 (Europe/Warsaw)

Werdykt: **TTS_RESTORE_HIL_BLOCKED**.

## Kryterium i dostępny interfejs

Kanoniczny TODO wymaga fizycznego potwierdzenia restore RADIO PLAY/STOP,
braku autoplay po TTS, ochrony przed spóźnionym RESUME oraz wyjątku utraty BT
podczas aktywnego komunikatu. Istniejące raporty
`VOXONE-STABLE-RADIO-FALLBACK-CONTRACT-1` i
`VOXONE-STABLE-BT-LOSS-AUDIO-SAFETY-1` potwierdzają te warunki wyłącznie
programowo.

Aktualny firmware ma jeden produkcyjny punkt wejścia temporary audio:
wiadomość dłuższa niż 19 B na topicu MQTT `<effectiveRoot>/command` jest
przekazywana jako URL do `Player::requestTemporaryUrl()`. Nie istnieje osobny
endpoint HTTP/WS TTS/announce ani autoryzowana komenda WWW uruchamiająca tę
samą ścieżkę.

Read-only `GET /api/mqtt` na L0 zwrócił:

- `enabled=false`;
- pusty `host` i `rootTopic`;
- `passwordSet=false`;
- automatyczny `effectiveRoot=voxone-8CBD1C`.

ASUS DEV nie ma aktywnego lokalnego brokera MQTT ani narzędzia
`mosquitto_pub`. Włączenie MQTT i wskazanie brokera wymagałoby trwałej zmiany
konfiguracji urządzenia, wyraźnie zabronionej w tym zadaniu. Bez aktywnego
brokera publikacja na sam wyliczony topic nie dociera do L0.

## Zakres wykonany

Wykonano tylko odczytowy preflight. L0 pozostał READY z
`bootId=8a416d6c`, firmware `ebb3c06-dirty`, `app0 VALID`. Nie wysłano TTS,
STOP, PLAY ani RESUME; nie otwarto UART, ponieważ nie istniał prawdziwy
scenariusz do zsynchronizowania. Nie zmieniono MQTT, Wi-Fi, SECURITY ani
innej konfiguracji trwałej.

Nie wolno zastępować tego wyniku bezpośrednim wywołaniem funkcji firmware,
testem native albo zwykłym streamem A2DP: nie byłby to fizyczny test
produkcyjnego wejścia TTS.

## Minimalny następny krok

Udostępnić istniejący, autoryzowany broker MQTT i skonfigurowany już na L0
topic `command` albo osobno zatwierdzić kontrolowaną konfigurację MQTT wraz z
rollbackiem. Następnie jednym HIL sprawdzić RADIO PLAY → TTS → restore oraz
RADIO STOP → TTS → brak autoplay/spóźnionego RESUME, z równoległym UART i WS.

Nie zamknięto bramki TTS RADIO/BT restore. Licznik pozostaje **11/13
otwartych**. Wcześniejsze przejście z 12 do 11 nastąpiło po zamknięciu bramki
**brak otwartych błędów P0/P1** przez `M0 BAD_TAIL_HIL_PASS`; nie jest wynikiem
bieżącego preflightu TTS.
