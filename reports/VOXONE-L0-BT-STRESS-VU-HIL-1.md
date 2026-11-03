# VOXONE-L0-BT-STRESS-VU-HIL-1

Data: 2026-11-02  
Wynik zbiorczy: **PARTIAL**  
L0: `4ef1e53`, firmware `0.8.0`, kanał `dev`, profil `L0`  
VoxOneBT: `0.6.7-dev`, obraz `d76b139`, protokół `2`  
Źródło: Raspberry Pi / BlueALSA A2DP Source, SBC, PCM S16_LE stereo 48 kHz

## Zakres i ograniczenia

Wykonano jedną ograniczoną sesję LIVE, bez OTA, flashowania, restartów,
zmian SECURITY/NVS/DSP, kasowania bondingu, commitów i push. Przed LIVE
test offline potwierdził plan 20 różnych czasów odtwarzania, limit 26 minut,
odrzucanie pierwszej próbki po zmianie routingu, sąsiednie pełne okna
PLAY/PAUSE oraz porównanie dwóch klientów WS.

WWW/WS nie eksportują bieżących surowych poziomów VU. Porównanie dwóch
klientów mogło więc objąć tylko source, transport, połączenie i stan modułu;
telemetrycznym dowodem VU jest `peak` z pasywnego UART MAIN. Ten test nie
przyznaje analogowego AUDIO PASS.

## Preflight

- L0: `READY=true`, `bootId=e7582463`, `app1`, OTA `valid`, WWW
  `READY/verified`.
- VoxOneBT: online, firmware `0.6.7-dev`, protokół `2`; peer
  `30:76:F5:EF:F3:5A` miał `Paired=yes`, `Bonded=yes`, `Connected=no`.
- Bridge: `bluetooth`, `bluealsa` i `bluealsa-aplay` były aktywne; nie było
  innego HIL ani procesu PCM.
- Pasywny UART: `/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A7039N0Z-if00-port0`
  wskazywał `/dev/ttyUSB0`. Otwarcie loggera nie zmieniło bootId.

## Wynik sesji

| Zakres | Wynik | Dowód |
|---|---|---|
| Reconnect endurance | **PARTIAL** | 4/20 cykli zakończone w całości. Każdy miał CONNECT bez autoplay, PLAY, PAUSE, PLAY, disconnect podczas PLAY, powrót do RADIO i brak pozostawionego połączenia. Cykl 5 osiągnął CONNECT, PLAY, PAUSE i zdrowe wznowienie PCM, lecz harness zatrzymał sesję przed disconnectem. |
| Volume/MUTE | **PARTIAL / NOT EXECUTED** | Bezpieczny test był zaplanowany na cykl 10; warunek STOP wystąpił wcześniej. Początkowe i końcowe `volume100=12`, MUTE OFF; poziomu nie zwiększono. |
| VU | **PARTIAL** | Cykle 1–4: w PLAY i PLAY po PAUSE dwa kolejne pełne okna miały `peak=1644` oraz wzrost RX/TX; w PAUSE dwa pełne okna miały `peak=0`, `tx=0` i stałe `txBytes`. Cykl 2 po reconnect bez PCM miał pełne okno `rx=tx=peak=0`. Dwa WS były zgodne stanowo, ale nie publikują raw VU. |
| Stabilność MAIN/VoxOneBT | **PARTIAL** | W obserwowanym przebiegu 329,573 s: 167 kontroli zdrowia, stały bootId, READY/OTA/WWW, BT online/proto 2, bez panic/assert/WDT. Pełne 20 cykli nie zostało wykonane. |
| Historyczny HCI allocation assert | **PARTIAL** | W całym bieżącym UART nie ma HCI allocation assert, panic, WDT, abort, brownout ani `module offline`, ale pokrycie zakończyło się po 4 pełnych cyklach. |
| Restore | **PASS** | Cleanup rozłączył peer i przywrócił `Italo4you` #3, RADIO/PLAY, `volume100=12`, MUTE OFF, balance 0. Końcowo READY, `bootId=e7582463`, OTA valid, WWW READY/verified, VoxOneBT online/proto 2. |

## Pełne okna i wykryta anomalia harnessu

W czterech ukończonych cyklach każda faza miała po dwa sąsiednie pełne
okna po bezwarunkowym odrzuceniu pierwszego raportu przejściowego:

- PLAY: około 191,8–192,3 kB/s RX i TX, rosnące `bytes`/`txBytes`,
  `peak=1644`;
- PAUSE: `rx=0`, `tx=0`, `peak=0`, niezmienne `txBytes`;
- PLAY po PAUSE: ponowny wzrost RX/TX i `peak=1644`;
- użyte okna miały `errors=writeErrors=shortWrites=0`.

W cyklu 5 pierwsze PLAY oraz PAUSE również spełniły te warunki. Po wznowieniu
PLAY pierwszy, odrzucony raport przejściowy miał `shortWrites=1`. Następne
trzy pełne raporty miały około 192 kB/s RX/TX, rosnące liczniki i `peak=1644`;
`errors=0`, `writeErrors=0`, a `shortWrites` pozostał `1` bez dalszego wzrostu.

Harness wymagał bezwzględnie `shortWrites==0`, mimo że jest to licznik
skumulowany. Dlatego pojedynczy wzrost z okna przejściowego uniemożliwił
kwalifikację wszystkich późniejszych okien i po ograniczonym timeout
spowodował STOP. Poprawna interpretacja dostępnego dowodu to:

- istnieje jedna zarejestrowana anomalia routingu I2S przy przejściu
  PAUSE -> PLAY (`shortWrites` 0 -> 1);
- nie ma dowodu powtarzania błędu w pełnych ustabilizowanych oknach
  (`delta shortWrites=0` dla obu kolejnych par);
- nie wolno z tego wywodzić analogowego dropout ani pełnego PASS;
- nie wykonano automatycznego ponowienia.

Zgodnie z `src/core/bt_audio_input.cpp` licznik może wzrosnąć po niepełnym
`i2s_write()` albo gdy aktywny routing nie uzyska mutexu wyjścia w limicie.
Bieżące dane nie rozstrzygają, która z tych dwóch ścieżek wystąpiła.

## Pamięć i stan końcowy

Po cyklach 1–4 `freeHeap` wynosił 157132–157248 B, `minimumFreeHeap`
pozostał 128760 B, a `psramFree` 7803103–7805431 B. Początek miał
`freeHeap=157148 B`, `psramFree=7798447 B`; po cleanup/restore odczyt wynosił
odpowiednio 157140 B i 7800775 B. Nie ma obserwowanej degradacji heap/PSRAM,
ale krótki PARTIAL nie zamyka bramki endurance.

## Integralność dowodów

Katalog dowodów:
`/home/piotrek/voxone-hil/VOXONE-L0-BT-STRESS-VU-HIL-1-20261102T165006+0100/`

- `run/RESULT.json`: `5d2bcd8ec451d3577187205bec59f71a0fba7197fcddfae09fd3f7d94ceb191f`
- `run/events.jsonl`: `0db2b48dad426ca98708c69091c86a8453013ba3f0ee64755c6ad5d23c112ff4`
- `run/operations.jsonl`: `43af86f3b99548cb17a5b91dfb18893ef64776a094841307737dc7d561e7a24c`
- `run/telemetry.jsonl`: `28ea1591d282bf68557a5bb3190ba07d06869e794c7c2ec206d8a40d39405d00`
- `run/ws.jsonl`: `f4cc6f29d1c64bf09ce9da433e110c7fec71ab50cafe9a42cb5ff2dc8bf0c76b`
- `bridge/uart/uart.raw`: `91fd7595db31880bfa281fac125b26b39e9d203b67e18df9e940f3f37192d76e`
- wykonany harness: `9264d5684f517d02e750b5dc46c5f26f1fbe15ce9781d6a59d402a04b30ef70a`
- WAV 60 s: `70cecf0641087896c75f126235f3ab1b32247c7fef3cd8a91bc6aa409a6fc6e1`

743 rekordy `uart-chunks.jsonl` składają się dokładnie w 41523 B
`uart.raw`; logger zakończono kontrolowanym SIGINT po STOP, raportując
`bootIdChanged=false` i zero niezapisanych bajtów. Status `INTERRUPTED`
dotyczy wyłącznie planowanego czasu capture, nie integralności zapisanych
danych.

## Pozostały zakres

Nie zmieniono formalnych statusów `RC1-BT-LIFECYCLE` ani
`RC1-SOURCE-MANAGER`. Nadal wymagane są: brakujące pokrycie 20-cyklowego
endurance z logiką oceniającą przyrost liczników skumulowanych po granicy,
volume/MUTE podczas A2DP bez podnoszenia poziomu, pełne pokrycie historycznego
HCI assert, metadata i wymagania S0. Dla Source Manager pozostają jego osobne
kryteria utraty runtime/heartbeat i S0. Cztery pełne cykle, reconnect VU oraz
restore z tego raportu są zachowanymi dodatnimi dowodami, ale nie zamykają
żadnej z tych bramek.
