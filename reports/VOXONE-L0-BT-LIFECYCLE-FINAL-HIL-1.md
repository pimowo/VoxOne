# VOXONE-L0-BT-LIFECYCLE-FINAL-HIL-1

Data: 2026-11-02, 14:12–14:15 CET. Wynik sesji: **FAIL — STOP po
rzeczywistym błędzie w kroku 6**. Nie wykonywano ponowienia, OTA, flash,
restartu, btmon ani dalszych scenariuszy po STOP.

## Stan wejściowy i zabezpieczenia

- MAIN L0: `0.8.0-dev` / build `4ef1e53`, `app1 VALID`, READY,
  WWW READY/verified/compatible, `bootId=e7582463`.
- VoxOneBT: online, firmware `0.6.7-dev` z wdrożonego obrazu `d76b139`,
  PROTO 2. Raspberry widziało `VoxOneBT-EFF35A` pod
  `30:76:F5:EF:F3:5A` jako `Paired=yes`, `Bonded=yes`, `Connected=no`.
- Stan audio: stacja #3 `Italo4you`, RADIO/PLAY/playing, volume 7,
  volume100 12, MUTE OFF, balance 0. Początkowa metadata była bieżącą
  metadata radia i nie jest trwałym elementem restore.
- BlueALSA, Bluetooth i bluealsa-aplay były aktywne. Endpoint po połączeniu
  potwierdził A2DP Source, SBC, S16_LE stereo i 48 kHz.
- Pasywny logger L0 został uzbrojony na FT232R
  `usb-FTDI_FT232R_USB_UART_A7039N0Z-if00-port0` przed pierwszą zmianą stanu.

## Wyniki kroków

| Krok | Wynik | Dowód |
|---|---|---|
| Zapis stanu początkowego | PASS | Pełny READY i świeży WS GETINDEX/GETSYSTEM/GETWEBSTATUS. |
| Pasywny UART | PASS | 3676 B, 66/66 chunków, raw identyczny z rekonstrukcją, zero niezapisanych bajtów. Manifest `INTERRUPTED` wyłącznie przez kontrolowane zakończenie po STOP. |
| Connect Raspberry → VoxOneBT | PASS | BlueZ `Connected=yes`; MAIN automatycznie wybrał BT. |
| Brak autoplay bez PCM | PASS | Po 2 s `BT/STOP/stopped`, sampleRate 48000, brak TX. Ten zakres był już wcześniej zaliczony i nie jest nowym zamknięciem TODO. |
| Start syntetycznego PCM | PASS techniczny | BlueALSA otworzyła właściwy endpoint i rozpoczęła pętlę I/O. Użyto 60 s PCM16 stereo 48 kHz, SHA-256 `70cecf0641087896c75f126235f3ab1b32247c7fef3cd8a91bc6aa409a6fc6e1`. |
| BT/PLAYING i TX > 0 | **FAIL** | Przez pełne ograniczone okno WS pozostał `BT/PAUZA/paused`. UART uruchomił I2S1 przy 48 kHz i ustawił `route=BT rate=0`, po czym dwa kolejne raporty miały `rx=0 B/s`, `tx=0 B/s`, `bytes=0`, `txBytes=0`, bez błędów I2S. |
| PAUSE i dwa ustabilizowane odczyty | NOT RUN | STOP nastąpił przed wysłaniem nowego PAUSE. |
| PLAY i ponowny TX | NOT RUN | STOP. |
| Disconnect podczas PLAY | NOT RUN | STOP przed PLAY. |
| Fallback RADIO po PLAY | NOT RUN w wymaganym wariancie | Cleanup po stanie PAUSE/zero PCM wrócił do RADIO, ale nie kwalifikuje scenariusza disconnect-during-PLAY. |
| Reconnect i wybór źródła | NOT RUN | STOP. |
| Ręczne RADIO → BT → RADIO | NOT RUN | STOP. |
| Restore | PASS | Własny proces `aplay` zatrzymano, peer rozłączono, przywrócono #3 `Italo4you`, RADIO/PLAY, volume 7/12, MUTE OFF i balance 0. |

## Charakter błędu i stabilność

W poprzedniej zatwierdzonej sesji ostatnią komendą transportową BT był PAUSE,
po czym wykonano disconnect i restore RADIO. Bieżący reconnect bez PCM
prawidłowo nie uruchomił autoplay, lecz późniejsze rozpoczęcie nowej sesji PCM
pozostało w `PAUZA/paused`. Obserwacja jest zgodna z zachowaniem lokalnej
intencji PAUSE przez disconnect/reconnect. W tej sesji nie wysłano PLAY w celu
obejścia stanu, ponieważ krok 6 wymagał autonomicznego `BT/PLAYING`, a
procedura nakazywała STOP przy rzeczywistym błędzie.

Nie było utraty READY, zmiany `bootId`, restartu, panic, assert, WDT, stack
canary ani korupcji heap. Końcowo L0 nadal działa na `4ef1e53`, `app1 VALID`,
VoxOneBT jest online PROTO 2, RADIO gra, a Raspberry pozostaje rozłączone przy
zachowanym Pair/Bond.

## Werdykt bramek

- **RC1-BT-LIFECYCLE: FAIL dla tej finalnej sesji; bramka pozostaje OPEN.**
  Nowa sesja ujawniła blokujący stan PAUSE przechodzący na następną sesję PCM;
  wymagane PAUSE→PLAY, disconnect podczas PLAY i reconnect nie mogły zostać
  zakończone.
- **RC1-SOURCE-MANAGER: PARTIAL; bramka pozostaje OPEN.** Automatyczny wybór
  BT po connect oraz bezpieczny powrót RADIO w cleanup były poprawne, ale
  wymagane disconnect-during-PLAY, reconnect i ręczne RADIO→BT→RADIO nie
  zostały wykonane po STOP. Nie zaobserwowano nowej awarii samego Source
  Managera.

Dowody i sumy:
`/home/piotrek/voxone-hil/VOXONE-L0-BT-LIFECYCLE-FINAL-HIL-1-20261102T141220+0100/`.
`docs/TODO.md` nie został zmieniony, ponieważ sesja nie dodała nowego PASS
poza zakresem już zachowanych wyników. Nie wykonano commit ani push.
