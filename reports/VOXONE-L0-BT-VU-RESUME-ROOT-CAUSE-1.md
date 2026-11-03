# VOXONE-L0-BT-VU-RESUME-ROOT-CAUSE-1

Data: 2026-11-02

Wynik: **UNVERIFIED — anomalia zawężona do źródła/VoxOneBT/I2S, brak dowodu rozstrzygającego warstwę**

Analizowane firmware: L0 `4ef1e53`, VoxOneBT `d76b139`

Nie wykonano LIVE HIL, OTA, flashowania, restartu ani zmiany konfiguracji.

## Wniosek

`peak=0` z cyklu 14 nie powstał w parserze UART, `SourceManager`, stanie VU,
WWW/WS ani display. Pole `peak` raportu `##[BT-AUDIO]` jest liczone niezależnie
bezpośrednio z próbek odebranych przez MAIN na I2S1, przed `outputSilent()` i
przed zapisem do I2S0. W trzech raportach po wznowieniu MAIN odebrał pełny
strumień około 192 kB/s, ale wszystkie odebrane próbki miały wartość zero.
Rosnące `txBytes` potwierdza tylko zapis liczby bajtów; nie dowodzi niezerowej
zawartości. W tym przebiegu MAIN mógł przesyłać do I2S0 wyłącznie zera.

Dokładne źródło zer pozostaje **UNVERIFIED**. Zachowany log nie zawiera
zsynchronizowanych wartości RAW VU przed regulacją głośności i PCM po regulacji
w VoxOneBT. Nie można więc odróżnić:

- zer przychodzących już z BlueALSA/A2DP;
- wyzerowania przez niskopoziomowe skalowanie/kwantyzację ESP32-A2DP;
- braku nowych callbacków PCM po PLAY przy działającym zegarze I2S i zerowanym
  DMA.

Nie znaleziono konkretnego defektu, który można bezpiecznie poprawić na
podstawie obecnych danych. Nie przygotowano pozornej poprawki produkcyjnej.

## Porównanie cykli 12, 13 i 14

| Cykl | PLAY — dwa pełne okna | PAUSE — dwa pełne okna | PLAY po PAUSE |
|---:|---|---|---|
| 12 | `peak=27/27`, RX/TX rośnie | `peak=0/0`, TX stoi | `peak=27/27`, RX/TX rośnie |
| 13 | `peak=27/27`, RX/TX rośnie | `peak=0/0`, TX stoi | `peak=27/27`, RX/TX rośnie |
| 14 | `peak=27/27`, RX/TX rośnie | `peak=0/0`, TX stoi | odrzucone + dwa pełne okna: `peak=0/0/0`, RX/TX `192166/191948/191948 B/s` |

W każdym z trzech wadliwych raportów cyklu 14 `rate=48000`, skumulowane
`bytes` i `txBytes` rosły, a `errors=writeErrors=shortWrites=0`. Nie była to
pojedyncza próbka przejściowa. Odcinki obejmowały około 15 s. WAV ma w każdym
5-sekundowym oknie peak 1642 i 191358–191676 niezerowych próbek, więc cyklu 14
nie wyjaśnia jednosekundowa cisza fixture występująca co 5 s.

## Pełna droga danych VU i PCM

1. ESP32-A2DP wywołuje `raw_stream_reader` przed `update_audio_data()`, a
   `stream_reader` po skalowaniu głośności. VoxOneBT mierzy RAW w pierwszym
   callbacku i przekazuje drugi callback do własnego I2S0.
2. `RawVuMeter` zachowuje maksimum i raportuje je co 50 ms. PAUSE/STOP lub
   disconnect czyści akumulator i wysyła jedno `VU 0 0`. Podczas PLAY
   `App::processVu()` ponownie wysyła świeże wartości RAW.
3. MAIN przyjmuje `VU` tylko, gdy `connected && PLAYING`. PAUSE i STOP zerują
   `rawVuLeft`, `rawVuRight` i znacznik czasu, więc opóźniona ramka nie może
   odtworzyć starego VU po późniejszym PLAY.
4. `SourceManagerState` kopiuje te wartości; `sourceVuSelect()` wymaga BT
   PLAYING i świeżości do 150 ms, następnie skaluje L/R dla display. WS nie
   eksportuje surowych liczników RAW VU.
5. Niezależny `##[BT-AUDIO] peak` MAIN jest liczony z fizycznego I2S1. Dopiero
   później MAIN może wyzerować bufor przez `player.outputSilent()` i zapisuje
   go do I2S0. Z tego powodu parser/protokół/display nie mogą wytworzyć zera
   obserwowanego w tym polu.

Zerowanie przy PAUSE i ponowne przyjmowanie świeżej ramki po PLAY ma testy
native PASS. Reset filtrów VU nie zawiera filtra czasowego poza 50 ms i nie
wyjaśnia 15 s zer. Kolejność `raw -> volume -> stream` jest jednoznaczna w
przypiętej bibliotece. Nie znaleziono w MAIN race'u, który zmieniałby próbki
przed obliczeniem `peak`; stan i prezentacja VU są od tej diagnostyki
oddzielone.

## Hipoteza skalowania — silna, lecz niepotwierdzona

Po pierwszej zmianie volume/MUTE peak spadł z 1644 do 27 i pozostał 27 w
cyklach 2–13 oraz pierwszym PLAY cyklu 14. MAIN mapuje `volume100=12` na
absolute volume `21`. Domyślny regulator ESP32-A2DP wylicza dla 21 współczynnik
całkowity `69/4096`:

```text
1642 * 69 / 4096 = 27
  27 * 69 / 4096 = 0
```

Sygnatura jest dokładnie zgodna z jednym etapem tłumienia po zmianie volume i
z dwoma etapami tłumienia w anomalii. Bridge działa na BlueALSA 4.3.1, a
`bluealsa.service` nie ma `--a2dp-volume`; dokumentacja zainstalowanej wersji
określa dla A2DP domyślny soft-volume, który skaluje PCM. Odczyt wykonano po
sesji, gdy peer był rozłączony, więc nie zachowano wartości `Volume` i
`SoftVolume` aktywnego PCM z cyklu 14. Nie ma dowodu, że BlueALSA faktycznie
zastosowała wtedy pierwszy z dwóch etapów. Równie możliwy na podstawie logu
pozostaje brak callbacków z nowym PCM przy aktywnym, wyzerowanym I2S DMA.

## Weryfikacja offline i buildy

- deterministyczna analiza cykli 12–14, WAV i arytmetyki kwantyzacji: PASS;
- VoxOneBT `RawVuMeterTest` i `A2dpAudioGateTest`: PASS w C++17;
- te same dwa testy pod ASan/UBSan/LSan: PASS;
- VoxOneBT `pio run -e esp32dev`: PASS, bez uploadu;
- pełny `tools/verify.ps1` MAIN: PASS — 158 native, transport strict 16/16,
  ASan/UBSan, Web Update, testy narzędzi HIL, SPIFFS, WWW, full-image
  L0/M0/S0/DIN0/W0, walidacja obrazów, L0 TX-only i `git diff --check`;
- pierwsza ręczna próba `RawVuMeterTest` w C++11 nie skompilowała istniejącej
  inicjalizacji agregatu; zgodny z bieżącym testem C++17 i firmware build mają
  PASS. Nie jest to błąd runtime VU.

Build MAIN pochodzi z bieżącego dirty worktree (`8ff8706-dirty`), a nie z
wdrożonego obrazu `4ef1e53`; służy tylko do regresji offline i nie jest
atestacją fizycznego firmware.

Analiza:
`/home/piotrek/voxone-hil/VOXONE-L0-BT-VU-RESUME-ROOT-CAUSE-1-20261102T182706+0100/offline_analysis.py`

SHA-256: `29be982d11d018d64a5228c454d9292148f6e353260e6895012147c1ea35cf13`

## Jeden minimalny retest HIL VU

Nie powtarzać stressu 20 cykli. Potrzebny jest jeden cykl
CONNECT -> PLAY 10 s -> PAUSE 8 s -> PLAY 20 s -> disconnect -> restore, z tym
samym 60-sekundowym WAV i początkową głośnością 12.

Przed osobno zatwierdzonym wdrożeniem diagnostycznym dodać wyłącznie
callback-safe instrumentację VoxOneBT. Callbacki mają tylko akumulować bez
alokacji i bez logowania:

- peak i liczbę bajtów `raw_stream_reader` przed volume;
- peak i liczbę bajtów `stream_reader` po volume;
- liczbę/bajty udanych prób `I2sOutput::write` oraz stan gate;
- absolute volume i aktualny `volumeFactor`.

Pętla główna ma najwyżej raz na sekundę emitować po USB jedną ramkę:

```text
VU_PATH t=<ms> raw=<L,R> post=<L,R> rawBytes=<n> postBytes=<n> i2sBytes=<n> abs=<n> factor=<n> gate=<0|1>
```

Równolegle zachować istniejący MAIN UART `##[BT-AUDIO]` oraz, gdy PCM jest
aktywny, odczytać `bluealsa-cli info`, `volume` i `soft-volume`. Logować
timestampy z jednego hosta. Nie logować z callbacku Bluetooth, aby nie dodać
alokacji ani zmienić historycznego ryzyka HCI assert.

Rozstrzygnięcie jest deterministyczne:

- `raw=0` przy rosnącym `rawBytes` -> zera powstają przed VoxOneBT, w źródle;
- `raw>0`, `post=0` -> skalowanie/kwantyzacja w VoxOneBT;
- `post>0`, `i2sBytes` rośnie, MAIN `peak=0` -> interfejs I2S lub odbiór MAIN;
- brak wzrostu `rawBytes/postBytes` po PLAY -> callback/gate A2DP, nie VU;
- MAIN `peak>0`, ale brak świeżego VU na display -> dopiero wtedy badać UART,
  parser i stan prezentacji.

Ten retest wymaga osobnej zgody na diagnostyczny firmware. W tym zadaniu nie
zbudowano ani nie wdrożono takiego obrazu i nie zmieniono fizycznych urządzeń.
