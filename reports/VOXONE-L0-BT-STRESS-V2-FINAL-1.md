# VOXONE-L0-BT-STRESS-V2-FINAL-1

Data: 2026-11-02  
Wynik zbiorczy: **PARTIAL — 13/20 pełnych cykli, STOP na VU w cyklu 14**  
L0: `4ef1e53`, firmware `0.8.0`, kanał `dev`, profil `L0`  
VoxOneBT: `0.6.7-dev`, obraz `d76b139`, protokół `2`  
Źródło: Raspberry Pi / BlueALSA A2DP Source, SBC, PCM S16_LE stereo 48 kHz

## Zmiana harnessu i testy offline

Nie zmieniono firmware produkcyjnego. V2 zastąpił wyłącznie logikę pomiarową:

- pierwsza próbka po potwierdzeniu routingu jest zawsze odrzucana;
- wymagane są dwie kolejne pełne próbki;
- skumulowane `errors`, `writeErrors` i `shortWrites` są oceniane jako delta
  między pełnymi próbkami, a nie jako bezwzględne zero;
- historyczna wartość większa od zera i przyrost wyłącznie przejściowy nie
  blokują PASS stabilnego okna;
- przyrost albo reset/zawinięcie między pełnymi próbkami jest rzeczywistą
  sprzecznością i powoduje STOP;
- każde wykryte zdarzenie licznika jest zachowywane diagnostycznie.

Offline PASS objął wymagane przypadki: historyczne `shortWrites > 0`, jeden
przyrost przejściowy, przyrost w stabilnym PLAY, reset/zawinięcie, brak dwóch
pełnych okien oraz PAUSE z historycznym licznikiem. Plan nadal ma 20 cykli i
limit poniżej 30 minut. Volume/MUTE przeniesiono do cyklu 1, a reconnect VU do
cyklu 2.

## Preflight i ograniczenia

- L0: `READY=true`, `bootId=e7582463`, `app1`, OTA `valid`, WWW
  `READY/verified`.
- VoxOneBT: online, firmware `0.6.7-dev`, protokół 2; peer był bonded i
  rozłączony.
- Bridge: `bluetooth`, `bluealsa`, `bluealsa-aplay` aktywne; brak innego HIL,
  loggera i procesu PCM.
- Pasywny UART `/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A7039N0Z-if00-port0`
  wskazywał `/dev/ttyUSB0`; otwarcie nie zmieniło bootId.
- WS nie eksportuje raw VU. VU oceniano z `peak` MAIN UART, a dwóch klientów
  WS porównywano w zakresie source/transport/connect/module i dostępnych pól
  metadata. Nie przyznano analogowego AUDIO PASS.

## Dokładne pokrycie LIVE

Sesja trwała 929,648 s. Wykonano 14 CONNECT; cykle 1–13 zakończyły pełną
sekwencję CONNECT bez autoplay -> PLAY -> PAUSE -> PLAY -> disconnect podczas
PLAY -> RADIO. Cykl 14 wykonał CONNECT bez autoplay, pierwsze PLAY i PAUSE,
lecz zatrzymał się po nieodzyskanej anomalii VU w PLAY po PAUSE. Cleanup
wykonał czternasty disconnect.

| Zakres | Wynik | Dowód |
|---|---|---|
| Endurance 20 cykli | **PARTIAL** | 13/20 pełnych; cykl 14 częściowy; cykli 15–20 nie rozpoczęto po STOP. |
| PLAY/PAUSE/PLAY i RX/TX | **PARTIAL** | 13 pełnych cykli miało po dwie pełne próbki w każdej fazie; cykl 14 ma PASS pierwszego PLAY i PAUSE, lecz brak dodatniego VU po resume. |
| `shortWrites` w stabilnym PLAY | **PASS dla wykonanego zakresu** | 41 zaakceptowanych par pełnych okien, zero przyrostu/resetu `errors`, `writeErrors` i `shortWrites` między próbkami. |
| Volume/MUTE | **PASS sterowania/readback** | W aktywnym A2DP: volume 12 -> 11 (425,20 ms), MUTE ON (413,10 ms), MUTE OFF (388,08 ms), volume 11 -> 12 (470,07 ms); oba WS zgodne, poziom nigdy nie przekroczył początkowego. Bez analogowego AUDIO PASS. |
| Metadata | **PARTIAL** | Oba WS zgodnie podały peer `voxone-bridge` i 48 kHz. Syntetyczny `aplay` nie reklamował title/artist/track metadata, więc te pola pozostają UNVERIFIED. |
| VU | **FAIL w resume cyklu 14 / ogólnie PARTIAL** | PAUSE we wszystkich 14 rozpoczętych cyklach: dwie pełne próbki `rx=tx=peak=0` i stałe `txBytes`. Reconnect bez PCM w cyklu 2: dwa pełne zerowe okna. Po resume cyklu 14 trzy kolejne raporty miały ciągłe RX/TX, ale `peak=0`. |
| READY/boot/MAIN/BT | **PASS dla obserwowanego czasu, bramka endurance PARTIAL** | 456 kontroli zdrowia, stały `bootId=e7582463`, READY, OTA valid, WWW verified, BT online/proto 2; brak panic/assert/WDT/HCI allocation assert/module offline. |
| Restore | **PASS** | `Italo4you` #3, RADIO/PLAY, volume100=12, MUTE OFF, balance 0, BT disconnected; peer nadal bonded. |

## Zdarzenia `shortWrites`

Harness LIVE zapisał 16 zdarzeń niezerowego baseline w odrzuconych próbkach.
Pełny UART zawiera dokładnie sześć rzeczywistych przyrostów `shortWrites`,
każdy 0 -> 1 albo 1 -> 2 i każdy poza stabilną parą pełnych okien. Zostały
zachowane osobno w `run/counter-increments-post.jsonl` wraz z numerem raportu,
offsetem UART, ostatnią granicą routingu, RX/TX, peak i `lastDataMs`.

W pełnych ustabilizowanych parach nie wystąpił ani jeden nowy `shortWrites`.
`errors` i `writeErrors` przez całą sesję pozostały zero. Spadki licznika
między sesjami odpowiadały utworzeniu nowego toru po reconnect i nie były
używane jako dodatni dowód.

## Incydent VU i sekwencja STOP

Fixture nie jest cichy: każde z dwunastu 5-sekundowych okien WAV 60 s ma
maksimum 1642 i około 191 tys. niezerowych próbek. Sekwencja telemetryczna:

1. Pierwsze PLAY cyklu 1: `peak=1644` w obu pełnych oknach.
2. Po volume/MUTE/restore w cyklu 1, resume miało `peak=27`.
3. Cykle 2–13 oraz pierwsze PLAY cyklu 14 zachowały dodatni, lecz niski
   `peak=27`; RX/TX pozostawały około 192 kB/s.
4. PAUSE cyklu 14 poprawnie dał dwa pełne okna `rx=tx=peak=0`.
5. Po PLAY i `route=BT rate=48000` raport odrzucony oraz dwa pełne raporty
   miały kolejno RX/TX `192166/192166`, `191948/191948` i
   `191948/191948 B/s`, rosnące `bytes`/`txBytes`, zero błędów, lecz
   `peak=0` we wszystkich trzech.
6. Po ograniczonym timeout wykonano STOP bez ponowienia, disconnect i restore.

Istnieje korelacja czasowa spadku 1644 -> 27 z pierwszym testem volume/MUTE,
ale bieżące dane nie dowodzą przyczyny ani defektu firmware. Nie wiadomo, czy
źródłem jest synchronizacja absolute volume, skala bardzo cichego fixture,
tor źródła czy odbioru. Pewny jest jedynie brak dodatniego VU przy ciągłym PCM
w resume cyklu 14; dlatego pełny VU/endurance nie otrzymuje PASS.

## Pamięć

- początek: `freeHeap=157288 B`, `minimumFreeHeap=128760 B`,
  `psramFree=7805431 B`;
- po 13 pełnych cyklach: heap 157040–157244 B, stałe minimum 128760 B,
  PSRAM 7800775–7805431 B;
- cykl 1 -> 13: heap 157244 -> 157228 B, PSRAM 7805431 -> 7805431 B;
- po cleanup/restore: heap 157156 B, minimum 128760 B, PSRAM 7800775 B.

Nie ma monotonicznej degradacji w wykonanym zakresie, ale 13/20 nie zamyka
pełnej bramki endurance.

## Integralność i SHA-256

Dowody:
`/home/piotrek/voxone-hil/VOXONE-L0-BT-STRESS-V2-FINAL-1-20261102T170815+0100/`

- `run/RESULT.json`: `f041a80a1ecc3ffd39cfee57632e535e9c7c1b33b4ad37073951a6a331a5e0d2`
- `run/events.jsonl`: `115d7fb3afab83bf58571a3ed5d7c138887ed42e2e3318cda55924a7286bed62`
- `run/counter-increments-post.jsonl`: `589cef7f4844479f8e82a0e8bf1930d9b4c8021f58609af73ff63a520ca60620`
- `run/operations.jsonl`: `79f4a638a5bc4761bf46e721e9b279890e4183df70a12bc350751cdd9ddfc8cf`
- `run/telemetry.jsonl`: `2a813136f636d6172e36a667f47a57534b88c6f61be18bdbfabba7faf8419934`
- `run/ws.jsonl`: `cc1e6254a28971a4f783019289a4da7e9da03dc59460bd53f2450fb9c8e5c1b0`
- `bridge/uart/uart.raw`: `d3851b844f789757e6cfca797eb634b542601760ea151902a2c4f3ece18f57d8`
- wykonany `stress_v2.py`: `28316596dfa4a6d8ba9f30a03dd89e3b162063e67e7c15fb4e2289a7a8f00435`
- `offline_test_v2.py`: `8a7632bdbb2480af2b170e820e5e250cdf72c9802d8925bd670705e46e2642d2`
- WAV 60 s: `70cecf0641087896c75f126235f3ab1b32247c7fef3cd8a91bc6aa409a6fc6e1`

2039 rekordów `uart-chunks.jsonl` składa się bajtowo w 116302 B
`uart.raw`; SHA jest zgodny. Logger zakończono kontrolowanym SIGINT po STOP,
z `bootIdChanged=false` i zero niezapisanych bajtów. `INTERRUPTED` opisuje
wyłącznie skrócenie planowanego capture po zakończeniu HIL.

## Pozostały zakres

Nie zmieniono formalnych bramek RC1. Do pełnego BT STRESS pozostaje
rozstrzygnięcie zerowego VU po resume przy ciągłym RX/TX oraz brakujące pełne
pokrycie 20 cykli. Track metadata wymaga źródła, które faktycznie je publikuje.
S0 i odrębne kryteria Source Manager runtime-loss/heartbeat pozostają poza
zakresem tej sesji i nadal są otwarte.
