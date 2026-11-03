# VOXONE-L0-BT-PAUSE-RESUME-FINAL-1

Data: 2026-11-02, 14:20–14:23 CET. Wynik sesji: **FAIL proceduralny / STOP
po kroku 7**. Nie wykonano ponowienia, drugiego początkowego PLAY, OTA,
flashowania, restartu ani zmian SECURITY.

## Stan początkowy

- MAIN L0: `0.8.0-dev`, build `4ef1e53`, `app1 VALID`, READY,
  WWW READY/verified/compatible, `bootId=e7582463`.
- VoxOneBT: online, firmware `0.6.7-dev` z obrazu `d76b139`, PROTO 2.
- Raspberry: peer `VoxOneBT-EFF35A` / `30:76:F5:EF:F3:5A`,
  `Paired=yes`, `Bonded=yes`, `Connected=no`; usługi Bluetooth, BlueALSA i
  bluealsa-aplay aktywne.
- Audio: stacja #3 `Italo4you`, RADIO/PLAY/playing, volume 7 / volume100 12,
  MUTE OFF, balance 0.
- Pasywny UART L0: FT232R
  `usb-FTDI_FT232R_USB_UART_A7039N0Z-if00-port0`, 115200 8N1, RX-only.

## Kontrakt PAUSE

Utrzymanie ręcznego PAUSE przez disconnect/reconnect jest zgodne z kodem
wdrożonego VoxOneBT `d76b139`, a nie samo w sobie defektem. Metoda
`A2dpAudioGate::onConnection(false)` zeruje stan A2DP, znajomość sample rate i
`resumeAfterSuspend_`, ale nie zeruje `pausedByUser_`. Flaga jest zerowana
dopiero przez `onLocalPlay()` albo przez świeży STARTED po rzeczywistym remote
suspend. `effectivePlayback()` zwraca PAUSED, gdy `pausedByUser_` jest ustawione.

## Wyniki jednej sesji

| Próba | Stan / RX / TX | Wynik |
|---|---|---|
| Początkowy RADIO | RADIO/PLAY, peer disconnected; `bootId=e7582463` | PASS |
| CONNECT bez PCM | A2DP Source, SBC, S16_LE stereo 48 kHz; MAIN `BT/STOP/stopped` | PASS — brak autoplay |
| Start syntetycznego WAV | 60 s PCM16 stereo 48 kHz, SHA-256 `70cecf0641087896c75f126235f3ab1b32247c7fef3cd8a91bc6aa409a6fc6e1`; stan przeszedł do `BT/PAUZA/paused` | PASS — potwierdzenie zachowanej intencji PAUSE |
| Jawne PLAY | Dokładnie jedna komenda `webtransport=toggle`; readback `BT/PLAY/playing` po około 579 ms | PASS dla komendy i readbacku |
| RX/TX po PLAY | Jedyny pobrany raport: `rx=0 B/s`, `tx=0 B/s`, `bytes=0`, `txBytes=0`, `errors=writeErrors=shortWrites=0`; tuż potem UART ustawił `route=BT rate=48000` | **FAIL / TX NIEPOTWIERDZONY** |
| PAUSE podczas dodatniego PCM | Nie wykonano po STOP | NOT RUN |
| Ustabilizowane okno PAUSE | Nie wykonano | NOT RUN |
| PLAY po PAUSE i powrót TX | Nie wykonano | NOT RUN |
| Disconnect/reconnect podczas PLAY | Nie wykonano | NOT RUN |
| Source Manager: wymagany fallback i ręczne RADIO → BT → RADIO | Nie wykonano w wymaganym przebiegu po dodatnim TX | NOT RUN |
| Cleanup Source Manager | Rozłączenie peer-a spowodowało `active=RADIO reason=bt-disconnect`, `route=RADIO rate=44100` | PASS wyłącznie cleanup, nie zastępuje wymaganej próby |
| Końcowy RESTORE | #3 `Italo4you`, RADIO/PLAY, volume 7/12, MUTE OFF, balance 0, peer disconnected | PASS |

Raport zerowy został wygenerowany o 14:23:00.592, tylko 158 ms po wysłaniu
PLAY i przed `route=BT rate=48000` z 14:23:00.624 oraz przed pełnym readbackiem
PLAY o 14:23:01.013. Runner przyjął pierwszy raport z offsetu sprzed startu
PCM jako rozstrzygający i zakończył transmisję o 14:23:02.328. Zatem dowód
nie pozwala odróżnić rzeczywistego braku wznowienia TX od przejściowego raportu
sprzed aktywacji routingu. Nie przypisuje się tego jako potwierdzonego defektu
firmware. Kryterium RX/TX > 0 pozostaje jednak niespełnione, więc zgodnie z
warunkiem STOP całej sesji nie ponowiono.

## Stabilność i stan końcowy

- Logger zapisał 2846 B w 42/42 niepustych odczytach, bez utraconych bajtów.
  Rekonstrukcja 42 chunków jest bitowo identyczna z `uart.raw`. Status
  `INTERRUPTED` oznacza kontrolowane zatrzymanie loggera po STOP, nie błąd
  integralności.
- UART nie zawiera panic, assert, WDT, Guru Meditation, stack canary ani
  korupcji heap. `bootId=e7582463` nie zmienił się.
- Końcowo L0 pozostaje READY, `app1 VALID`, WWW READY/verified; VoxOneBT jest
  online jako `0.6.7-dev` / PROTO 2, RADIO gra, a Raspberry jest rozłączone z
  zachowanym Pair/Bond.
- `git diff --check` ma PASS. `docs/TODO.md` nie zmieniono: sesja nie dodała
  nowego pełnego PASS dla otwartych kroków lifecycle ani Source Manager.

## Werdykt

- PLAY/PAUSE/PLAY: **FAIL / PARTIAL** — zachowanie intencji PAUSE oraz jawny
  readback PLAY mają PASS, lecz dodatnie RX/TX po PLAY nie zostały potwierdzone;
  dalsze PAUSE/PLAY nie zostało wykonane po STOP.
- Disconnect/reconnect: **NOT RUN** w wymaganym wariancie podczas dodatniego
  PLAY.
- Source Manager: **NOT RUN** w wymaganym przebiegu; cleanup do RADIO był
  poprawny.
- Restore: **PASS**.
- Wynik całej sesji: **FAIL**. Bramki `RC1-BT-LIFECYCLE` i
  `RC1-SOURCE-MANAGER` pozostają OPEN/PARTIAL; wcześniejsze PASS pozostają
  zachowane.

Dowody:
`/home/piotrek/voxone-hil/VOXONE-L0-BT-PAUSE-RESUME-FINAL-1-20261102T142022+0100/`.
Najważniejsze SHA-256:

- źródłowy WAV 10 s: `d0b913799f15bd0f30715ba975c6b1b50b405c5871cf7afa39a6fdbf763a0d41`;
- WAV 60 s: `70cecf0641087896c75f126235f3ab1b32247c7fef3cd8a91bc6aa409a6fc6e1`;
- UART raw: `728b5595c74f457b6fed79353beb74d0b43bee2274422e0cb30ae2711f776eb6`;
- manifest UART: `90af2249fb16d86d416a65dbacdb47ac8b5a25dbd99213912a939cc03653ec71`;
- `events.jsonl`: `4b9380f14328ab382b58e8d546432c3fc83a9622c48f9b51e604bf030d190cd8`;
- `operations.jsonl`: `6af5c87985e6177073e330fc3b365b1f174e5af82f483e0d82fd27c1d5376499`.

Nie wykonano commit ani push.
