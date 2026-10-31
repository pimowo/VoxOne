# VOXONE-L0-VU-LR-PHYSICAL-HIL-1

Data: 2026-10-31
Profil: L0
Firmware: `0.8.0-dev` / `b4b74cf-dirty`
Stan końcowy testu: **AUDIO/VU UNVERIFIED**

Ten wynik nie otrzymuje statusu PASS ani FAIL. Użytkownik nie był w stanie
wiarygodnie rozróżnić czterech wymaganych obserwacji fizycznych, dlatego nie
ma podstaw do oceny audio ani pasków VU.

## Potwierdzone fakty maszynowe

- Kontroler endpointu wykonał odczyty zwrotne sekwencji `balance=0, -16,
  -15, 0, +15, +16`, po czym potwierdził końcowe `balance=0`.
- Dalszy obserwator był read-only: `mutatingCommands=0`.
- Połączenie L0 z plikiem WAV trwało 117,979 s i zamknęło się bez komendy
  STOP. Zarejestrowano `WAV_HTTP_CLOSED`; materiał osiągnął naturalny koniec.
- Po WAV wróciło `Italo4you` #3 w stanie `RADIO/PLAY/playing`, MP3 256 kb/s.
- Końcowy snapshot potwierdził `balance=0`, MUTE OFF, `READY=true`, WWW READY
  oraz niezmieniony `bootId=e95a3cbe`.
- Obserwator zapisał 308 ramek JSON i 52 próbki READY; żadna próbka nie miała
  `ready=false`, a wszystkie zawierały ten sam `bootId`.
- Pasywny `L0_LOG_RX` zapisał 360,109 s / 2376 B surowego UART. Pełne
  wyszukanie markerów panic, Guru Meditation, assert/abort, watchdog/WDT,
  restart/reset, heap corruption, error/failure/timeout, short write,
  decoder/underflow, exception i stack overflow zwróciło zero trafień.
  SHA-256 `uart.raw`:
  `9d421dd2bfe90d63d39c2d62f5cdcaf93576b187534b9e1ac0c80be2be17456f`.

## Ograniczenie wyniku

Odczyt endpointu, ciągłość READY i UART nie dowodzą tego, który kanał był
słyszalny ani który pasek VU reagował. Fizyczny zakres AUDIO/VU pozostaje
UNVERIFIED i zadanie nie jest zamknięte. Nie wykonano flashowania, OTA,
restartu, zmiany firmware ani zmiany okablowania.

## Dowody

- `/home/piotrek/voxone-hil/logs/l0/l0-20261031T173551Z-RHiySpGh/balance-hil-run2.json`
- `/home/piotrek/voxone-hil/logs/l0/vu-lr-20261031T175200Z/ws-observer.json`
- `/home/piotrek/voxone-hil/logs/l0/vu-lr-20261031T175200Z/ws-observer.log`
- `/home/piotrek/voxone-hil/logs/l0/l0-20261031T175200Z-tUpOkda8/manifest.json`
- `/home/piotrek/voxone-hil/logs/l0/l0-20261031T175200Z-tUpOkda8/uart.raw`
