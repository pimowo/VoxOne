# VOXONE-L0-BT-LIFECYCLE-HIL-FIX-1

Data: 2026-11-02, 16:18–16:22 CET. Wynik: **PASS** dla poprawionego
harnessu oraz jednej zbiorczej sesji LIVE. Firmware nie zmieniano; nie
wykonywano OTA, flashowania, restartów, zmian SECURITY, commit ani push.

## Poprawka harnessu i gate offline

Harness rozpoczyna pomiar dopiero po zgodnym readbacku transportu oraz nowym
markerze routingu UART. Granica pomiaru jest ustawiana za markerem, a pierwszy
raport po tej granicy jest zawsze odrzucany jako potencjalnie przejściowy.
PASS wymaga dwóch kolejnych późniejszych raportów:

- PLAY: `rate=48000`, dodatnie RX/TX, brak błędów oraz wzrost obu liczników
  skumulowanych `bytes` i `txBytes`;
- PAUSE: `tx=0 B/s`, brak błędów i identyczne `txBytes` w obu oknach;
- timeout bez dowodu klasyfikuje wynik jako UNVERIFIED, natomiast utrzymująca
  się sprzeczność transportu z kontraktem jako FAIL.

`offline_test.py` ma PASS dla odrzucenia próbki przejściowej, wymogu dwóch
sąsiednich pełnych okien, braku łączenia próbek przez niepoprawne okno,
wzrostu obu liczników PLAY, stabilnego PAUSE oraz odrzucania raportów z
błędami. Oba pliki przeszły również `py_compile`.

## Stan początkowy

- MAIN L0: `0.8.0-dev`, build `4ef1e53`, `app1 VALID`, READY,
  WWW READY/verified/compatible, `bootId=e7582463`.
- VoxOneBT: firmware `0.6.7-dev` z obrazu `d76b139`, online, PROTO 2.
- Raspberry: `VoxOneBT-EFF35A` / `30:76:F5:EF:F3:5A`, Paired/Bonded,
  Connected=no; BlueALSA A2DP Source/SBC/S16_LE/stereo/48 kHz.
- Audio: #3 `Italo4you`, RADIO/PLAY, volume 7 / volume100 12, MUTE OFF,
  balance 0.
- UART L0: pasywny RX-only FT232R
  `usb-FTDI_FT232R_USB_UART_A7039N0Z-if00-port0`. Otwarcie nie zmieniło
  `bootId`.

## PLAY / PAUSE / PLAY

WAV miał 60 s, PCM16 stereo 48 kHz i SHA-256
`70cecf0641087896c75f126235f3ab1b32247c7fef3cd8a91bc6aa409a6fc6e1`.
Sygnał pozostawał aktywny do kontrolowanego disconnectu podczas PLAY.

| Faza | Stan i routing | Odrzucone okno | Pełne okno 1 | Pełne okno 2 | Wynik |
|---|---|---|---|---|---|
| PLAY | `BT/PLAYING`; route 48 kHz o 16:21:01.492 | 16:21:06.911: RX/TX 191897/191897 B/s | 16:21:11.917: RX/TX 192000/191795; `bytes=1998848`, `txBytes=1996800` | 16:21:16.923: RX/TX 192115/192319; `bytes=2960384`, `txBytes=2959360` | PASS |
| PAUSE | `BT/PAUZA`; route 0 o 16:21:18.951 | 16:21:21.924: przejściowe RX/TX 77997/77997 B/s | 16:21:26.941: RX/TX 0/0; `bytes=3350528`, `txBytes=3349504` | 16:21:31.935: RX/TX 0/0; `bytes=3350528`, `txBytes=3349504` | PASS |
| PLAY po PAUSE | `BT/PLAYING`; route 48 kHz o 16:21:33.396 | 16:21:36.952: RX/TX 135661/135252 B/s | 16:21:41.969: RX/TX 191936/191936; `bytes=4993024`, `txBytes=4989952` | 16:21:46.972: RX/TX 191987/191987; `bytes=5953536`, `txBytes=5950464` | PASS |

W każdym użytym oknie `errors=0`, `writeErrors=0` i `shortWrites=0`.
Początkowa transmisja automatycznie weszła w PLAYING, więc nie wysyłano
zbędnej początkowej komendy PLAY (`initialExplicitPlayCount=0`). Ręczne PAUSE
i późniejsze PLAY miały zgodne readbacki.

## Lifecycle i Source Manager

| Próba | Wynik | Dowód |
|---|---|---|
| CONNECT bez PCM | PASS | `BT/STOP/stopped`; brak autoplay. |
| Disconnect podczas potwierdzonego PLAY | PASS | Peer `Connected=no`; `active=RADIO reason=bt-disconnect`, RADIO/PLAY i ta sama stacja. |
| Reconnect bez PCM | PASS | `BT/STOP/stopped`; brak autoplay. |
| Ręczne RADIO → BT → RADIO | PASS | Każda komenda miała zgodny readback; UART: `active=RADIO reason=manual`, następnie BT i ponownie RADIO. Nie użyto radiowego `start=1` do BT. |
| Restore | PASS | #3 `Italo4you`, RADIO/PLAY, volume 7/12, MUTE OFF, balance 0, peer disconnected; Pair/Bond zachowane. |

## Stabilność, integralność i zakres

- Sesja sterująca trwała 71,860 s i zakończyła się bez retry.
- `bootId=e7582463` nie zmienił się; końcowo READY, `app1 VALID`, WWW
  READY/verified, VoxOneBT online / PROTO 2.
- UART: 11337 B, 204/204 niepuste chunki, zero niezapisanych bajtów;
  rekonstrukcja chunków jest bitowo identyczna z `uart.raw`. Status loggera
  `INTERRUPTED` wynika wyłącznie z kontrolowanego zatrzymania po sesji.
- Brak panic, assert, WDT, Guru Meditation, stack canary i korupcji heap.
- `git diff --check` ma PASS.
- PASS obejmuje normalny cykl L0 opisany powyżej. Szersze bramki pozostają
  PARTIAL: niewykonane są m.in. wielokrotne cykle, volume/metadata, awaryjna
  utrata runtime/heartbeat oraz docelowy profil S0.

Dowody:
`/home/piotrek/voxone-hil/VOXONE-L0-BT-LIFECYCLE-HIL-FIX-1-20261102T161845+0100/`.
Najważniejsze SHA-256:

- harness: `5cbb32e7bba19637b9db3c0d25412f8342ccda270c70150515d70f1ec28ca642`;
- test offline: `79888f09bdf5dd672a6c4442c7f730340f2b0097a81c18186f4cc1909018ae8d`;
- WAV 60 s: `70cecf0641087896c75f126235f3ab1b32247c7fef3cd8a91bc6aa409a6fc6e1`;
- UART raw: `2f4f4d062cf4557d113745f54feae5844978d786e974c4aaef27d13445e915a3`;
- `RESULT.json`: `9cae327425a2daa5a94a5ef1fb233efa31bc945ea851b79591c595cdb86814ba`;
- `events.jsonl`: `23686264ec3b3a8433649479047226f9503fe187e66fd83fabdbe7e85b215fcd`;
- manifest UART: `ea711fcde6b4fa5bcc8f042bed91fcf6268d2b56c7c6ed64f76b182416af376e`.
