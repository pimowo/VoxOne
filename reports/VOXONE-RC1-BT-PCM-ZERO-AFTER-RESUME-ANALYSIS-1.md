# VOXONE-RC1-BT-PCM-ZERO-AFTER-RESUME-ANALYSIS-1

Data: 2026-11-03. Zakres: wyłącznie analiza zachowanych dowodów i kodu.

Werdykt: **SOFTWARE ANALYSIS PASS; ROOT CAUSE INCONCLUSIVE; aktywny HIL
BLOCKED do osobnego zatwierdzenia**. Ustalono dokładne granice problemu i
minimalny eksperyment rozstrzygający, ale nie ma dowodu pozwalającego
przypisać fizyczną przyczynę jednej warstwie.

Nie uruchomiono cyklu BT, PCM, połączenia, capture ani komendy do L0. Nie
wykonano OTA, flashowania, restartu, zmiany konfiguracji ani firmware. Nie
zmieniono `docs/TODO.md`. Historyczne PHYSICAL PASS cykli 1–13 pozostają
zachowane; częściowy cykl 14 nie jest zaliczony ani uznany za naprawiony.

## Ustalenie z dowodów cyklu 14

- CONNECT miał poprawny A2DP Source/SBC, S16_LE stereo 48 kHz. Odczyt przy
  CONNECT pokazał `SoftVolume: true`, `Volume: 127/127`, MUTE false.
- Pierwsze PLAY cyklu 14 miało trzy kolejne raporty z `peak=27`, pełną
  przepływność i zerowe liczniki błędów.
- PAUSE zatrzymał zegary/dane: pełne okna miały RX/TX/peak zero i stałe
  liczniki bajtów.
- Po PLAY pierwszy mieszany raport po `route=BT rate=48000` miał jeszcze
  `rx=87859 B/s`, `tx=87244 B/s` i `peak=27`. Dopiero następne raporty przy
  około 192 kB/s miały `peak=0`.
- Zachowane pełne okna zera trwały co najmniej 15 s; kolejny raport przed
  cleanupem również miał zero. Nie był to brak startu I2S od pierwszej
  próbki po resume.
- `bytes`, `txBytes`, `rxBlocks`, `txBlocks` i `lastDataMs` rosły; `errors`,
  `writeErrors` i `shortWrites` pozostawały zero. Te pola dowodzą liczby
  odebranych i zapisanych bajtów, nie ich amplitudy.
- 60-sekundowy fixture ma peak 1642. Każdy blok 5 s zawiera sygnał; w każdym
  powtarzanym bloku tylko jedna sekunda jest zamierzoną ciszą. Nie może to
  wyjaśnić trzech kolejnych pełnych okien `peak=0`.
- Proces `aplay` pozostał aktywny do STOP, ale sama żywotność procesu nie
  dowodzi, które próbki zostały w tej chwili przyjęte, zakodowane i wysłane.

Istotne ograniczenie wcześniejszego wniosku o BlueALSA: w `events.jsonl`
jest 14 odczytów PCM, po jednym przy każdym CONNECT. Nie ma ponownego
`bluealsa-cli info` podczas PLAY, PAUSE ani wadliwego PLAY cyklu 14. Wartość
127/127 wyklucza tłumienie BlueALSA w chwili CONNECT, lecz nie dowodzi, że
nie zmieniła się później wskutek synchronizacji absolute volume.

## Produkcyjna ścieżka PCM

```text
WAV -> aplay -> ALSA bluealsa plugin -> BlueALSA soft-volume
    -> SBC encoder -> HCI/A2DP -> ESP32 Bluedroid/SBC decoder
    -> raw_stream_reader(measureRawAudio)             [RAW]
    -> A2DPDefaultVolumeControl::update_audio_data()   [in-place]
    -> stream_reader(streamAudio)                      [post-volume]
    -> I2sOutput::write() -> I2S0 TX/DMA VoxOneBT
    -> przewody BCLK/WS/DATA
    -> MAIN I2S1 slave i i2s_read()
    -> obliczenie blockPeak/peak
    -> opcjonalne outputSilent()/memset
    -> MAIN I2S0/DAC
```

Kolejność RAW → volume → post-volume jest jednoznaczna w przypiętym
ESP32-A2DP v1.8.2. Biblioteka modyfikuje bufor przekazany jako `const` in-place
przez rzutowanie do `Frame*`; kod aplikacji wywołuje regulator raz na callback.

PAUSE/PLAY nie ustawia ponownie volume ani factor. Lokalny PAUSE ustawia gate,
a `App::processBluetoothChanges()` zatrzymuje I2S i zeruje DMA. PLAY ponownie
uruchamia I2S, gdy znany rate i stan A2DP dają `desiredActive=true`. Przy
niezmienionych 48 kHz `I2sOutput::setSampleRate()` nie rekonfiguruje zegara;
`setActive(true)` zeruje DMA i wykonuje `i2s_start()`.

MAIN podczas PAUSE nie reinstaluje I2S1: rate modułu pozostaje znany jako
48 kHz. Zadanie RX czeka na powrót zegarów. Po wznowieniu liczy maksimum z
każdej odebranej próbki S16 przed jakimkolwiek `outputSilent()` i przed
zapisem do MAIN I2S0. `peak=0` nie ma progu: oznacza, że wszystkie próbki
objęte danym oknem I2S1 były dokładnie zerowe.

## Możliwe przyczyny według siły dowodów

### 1. Dynamiczny spadek amplitudy przed lub w post-volume VoxOneBT

To najsilniejsza szczegółowa hipoteza, ale nadal nie dowód. Po teście
volume/MUTE w cyklu 1 peak zmienił się `1644 -> 27` i pozostał 27 do
pierwszego PLAY cyklu 14. Dla ABSVOL 21 regulator VoxOneBT ma factor 69/4096:

```text
1642 * 69 / 4096 = 27
  27 * 69 / 4096 = 0
```

Sygnatura zera jest zgodna z drugim tłumieniem albo z wejściem RAW o
amplitudzie nie większej niż 59. Możliwe mechanizmy to dynamiczna zmiana
soft-volume BlueALSA po odczycie CONNECT, ponowne przetworzenie już
stłumionego bufora albo inna redukcja amplitudy przed callbackiem RAW.

Dowody przeciw przypisaniu: produkcyjny kod wywołuje gain raz, PAUSE/PLAY nie
zmienia factor, a odczyt BlueALSA przy CONNECT był 127/127. Brak wartości RAW,
post-volume, factor i BlueALSA volume z chwili awarii uniemożliwia wskazanie
konkretnego mechanizmu.

### 2. BlueALSA/A2DP dostarczały zera mimo aktywnego transportu

Pełne tempo A2DP i działający `aplay` nie dowodzą niezerowej zawartości.
BlueALSA albo nadajnik mogły po suspend/resume wejść w underrun, drain,
ciszę zastępczą lub zmieniony soft-volume i nadal wysyłać SBC/A2DP. Fixture
na dysku jest poprawny, lecz harness nie zapisywał bieżącego offsetu,
liczby niezerowych próbek przekazanych ALSA ani treści wychodzących ramek
SBC. Brak komunikatu XRUN w zachowanym stderr osłabia, ale nie wyklucza tej
hipotezy.

### 3. VoxOneBT I2S TX/DMA przeszedł na auto-clear po początkowym sygnale

`I2sOutput` ma `tx_desc_auto_clear=true`. Aktywny master może utrzymywać
BCLK/WS i wysyłać zera, gdy kolejka DMA nie dostaje skutecznych zapisów.
Produkcyjna telemetria nie zapisuje liczby requestów, faktycznie zapisanych
bajtów, inactive writes ani short writes modułu. Brak ciągłego USB UART
VoxOneBT podczas stressu dodatkowo usuwa nawet pojedynczy latched komunikat
`I2S PCM write failed or timed out` z dowodów.

Pierwszy mieszany raport z peak 27, a dopiero późniejsze zera, jest zgodny z
wyjałowieniem DMA po kilku sekundach bardziej niż z całkowitym nieudanym
startem. Przeciw: 13 wcześniejszych resume przeszło, a MAIN nie odnotował
błędu ani zaniku zegara.

### 4. Race lokalnego gate i zdarzeń A2DP STARTED/SUSPENDED

Komenda PAUSE/PLAY zmienia lokalną intencję synchronicznie, natomiast
STARTED/SUSPENDED przychodzi asynchronicznie z biblioteki. Nietypowa kolejność
mogła zatrzymać callbacki lub I2S po krótkim poprawnym starcie. Produkcyjny
log cyklu 14 nie zawiera zsynchronizowanego śladu gate/audio-state/callbacks z
VoxOneBT, więc hipotezy nie da się sprawdzić. Trzynaście poprzednich PASS
zmniejsza jej prawdopodobieństwo, ale nie wyklucza rzadkiego race'u.

### 5. I2S na przewodach albo odbiornik MAIN

Możliwy jest stan: post-volume niezerowe, `i2s_write()` skuteczne, lecz DATA
na przewodzie albo I2S1 MAIN daje zera. Jest to słabsza hipoteza: sample rate
nie zmienił się, MAIN nie reinstalował odbiornika, odebrał pełną przepływność
i najpierw zobaczył po resume peak 27. Brak fizycznego pomiaru DATA i brak
post-volume nadal nie pozwalają jej formalnie odrzucić.

Wykluczone jako samodzielna przyczyna `peak=0`: parser `VU`, SourceManager,
WWW/WS, display, MUTE MAIN, `outputSilent()`, naturalna jednosekundowa cisza
fixture oraz sama zmiana sample rate. Wszystkie znajdują się po pomiarze,
nie trwały dostatecznie długo albo nie wystąpiły.

## Dokładne miejsce utraty obserwowalności

Ostatni znany niezerowy punkt przed urządzeniem to plik WAV na Raspberry;
pierwszy rozstrzygający punkt za modułem to MAIN I2S1. Pomiędzy nimi brakuje
trzech synchronicznych tapów:

1. zawartość RAW po dekoderze SBC, przed regulacją VoxOneBT;
2. zawartość post-volume przekazana do `I2sOutput::write()`;
3. wynik requestów `i2s_write()` i stan I2S TX/DMA.

VoxOneBT mierzy RAW VU do protokołu, lecz stress nie zapisywał tej wartości:
nie jest eksportowana przez WS ani drukowana przez MAIN UART. Licznik callbacków
`stream` jest okresowo drukowany wyłącznie na USB VoxOneBT, którego podczas
stressu nie logowano, i nie opisuje zawartości próbek. MAIN `txBytes` jest po
drugiej stronie I2S1 i potwierdza tylko dalszy zapis tej samej liczby bajtów.

## Czy diagnoza jest możliwa bez zmiany firmware

Z zachowanych logów: **nie**. Można jedynie zawęzić problem do odcinka od
faktycznej zawartości BlueALSA/A2DP do wejścia I2S1 MAIN.

W przyszłym, osobno zatwierdzonym jednym cyklu można najpierw spróbować
diagnostyki bez zmiany firmware:

- zapisywać offset, peak/nonzero i SHA kolejnych okien danych podawanych do
  `aplay`, zamiast opierać się na samej żywotności procesu;
- odczytać `bluealsa-cli info` przy CONNECT, ustalonym PLAY, PAUSE, tuż po
  PLAY i w każdym pełnym oknie, szczególnie Volume/Mute/Running;
- uruchomić ograniczony `btmon` z prywatnym plikiem 0600 i zdekodować
  wychodzący A2DP/SBC offline.

Raspberry ma `btmon`, ale nie ma obecnie `tshark`, `ffmpeg` ani `sbcdec`.
Potrzebny byłby wcześniej zweryfikowany offline ekstraktor/dekoder, bez
instalowania pakietów w trakcie HIL. Nieprzerwany niezerowy SBC przy zerze
MAIN wykluczyłby source/BlueALSA, lecz nadal nie rozdzieliłby dekodera,
post-volume i I2S TX. Pełne rozstrzygnięcie tych warstw wymaga telemetrii
VoxOneBT albo fizycznego analizatora I2S; użytkownik nie ma obecnie dostępu
fizycznego.

## Minimalna instrumentacja, jeśli capture SBC nie rozstrzygnie

Przygotować osobny obraz diagnostyczny z czystego `d76b139`, zawierający
wyłącznie domyślnie wyłączoną sondę audio. Nie budować obrazu z bieżącego
dirty worktree VoxOneBT, ponieważ zawiera także niezwiązany eksperyment AVRCP
STOP.

Callbacki mają jedynie akumulować bez logowania i alokacji:

- RAW: peak L/R, nonzeroSamples, callbacks, bytes;
- post-volume: te same pola;
- I2S: requests/requestedBytes, driverWrites/writtenBytes,
  completed/inactive/errors/shortWrites;
- snapshot: ABSVOL, rzeczywisty factor, connection, playback, gate,
  sample-rate-ready i audio state.

Pętla główna emituje najwyżej jedną linię na sekundę przez USB. Istniejący
lokalny prototyp `AudioPathProbe`/`I2sWriteProbe` realizuje wymagany zakres,
ale pozostaje eksperymentalną, niewdrożoną zmianą i przed użyciem musi zostać
wydzielony od innych modyfikacji oraz ponownie zweryfikowany offline.

Rozstrzygnięcie:

- RAW zero przy rosnących rawBytes/callbacks: zera przed regulacją modułu;
- RAW dodatnie, post zero: regulator/kwantyzacja albo ponowne przetworzenie;
- post dodatnie, i2s writes niepełne/inactive: gate lub I2SOutput;
- post dodatnie, pełne i2s writes, MAIN zero: driver/DMA, przewody albo MAIN;
- RAW/post/MAIN dodatnie: historyczna anomalia nieodtworzona, nie naprawiona.

## Harness i baseline `peer connected=true`

Wykonany harness nie obsługuje bieżącego baseline'u:

- preflight jawnie wymaga `not peer0['connected']`;
- `bridge_connect()` odrzuca już połączonego peer-a;
- sukces i cleanup wymagają końcowo `not peer_status()['connected']`;
- `tools/chaos_v2.restore()` odtwarza stację, transport, volume, MUTE,
  balance i source, ale nie stan połączenia peer-a.

Minimalna poprawka narzędzia przed przyszłym HIL:

1. zapisać `initialPeerConnected`, Pair/Bond/Trusted, endpoint/Running oraz
   pełny snapshot L0;
2. odmówić przejęcia sesji przy obcym PCM, procesie HIL albo dryfie po
   snapshotcie;
3. kontrolować wyłącznie dokładny peer i rozróżniać połączenia zastane od
   utworzonych przez harness;
4. po cleanup odtworzyć odpowiednio connected albo disconnected, bez PCM i
   bez autoplay, a następnie odtworzyć source/stację/PLAY-STOP/volume/MUTE;
5. wykonać readback dwóch WS i peer-a; przy zmianie użytkownika natychmiast
   STOP bez nadpisywania nowszego stanu;
6. pokryć offline oba baseline'y i każdą ścieżkę wyjątku/cleanup.

Przy baseline `connected=true` przygotowawcze rozłączenie nie może być
liczone jako cykl kwalifikacyjny. Musi nastąpić dopiero w jawnie
zatwierdzonym oknie i mieć pełny restore.

## Kontrakt 20 cykli

Aktualny kanoniczny kontrakt w TODO wymaga wielokrotnych cykli i zachowuje
status `13/20`; nie zawiera słowa „kolejnych”, wymagania jednej sesji ani
zakazu agregacji. Wartość 20 pochodzi z zatwierdzonego planu/harnessu i
raportów, które nadal liczą zachowane 13.

Dlatego na podstawie aktualnego kontraktu **dopuszczalne jest 13 + 7 na tych
samych dokładnych wersjach i kryteriach**. Siedem oznacza jeden pełny
zastępczy cykl za niezaliczony cykl 14 oraz sześć dalszych. Nie wolno jednak
nazywać agregacji „jedną ciągłą sesją endurance 20 cykli”. Raport końcowy
musi podać osobno integralność, pamięć i terminalną stabilność obu sesji.

Jeżeli release gate ma wymagać nieprzerwanego 20-cycle endurance, jest to
nowe, ostrzejsze wymaganie, którego obecny kontrakt nie zawiera. Nie należy go
domniemywać ani powtarzać 13 zaliczonych cykli bez jawnej zmiany kontraktu.

## Bezpieczny plan dalszego HIL

1. Offline wydzielić i przetestować poprawkę harnessu dla connected baseline
   oraz source-side window ledger; bez firmware i bez kontaktu z L0.
2. Po osobnym zatwierdzeniu wykonać jeden diagnostyczny PLAY → PAUSE → PLAY
   z zapętlonym lub dostatecznie długim fixture, source ledger, fazowymi
   odczytami BlueALSA, `btmon`, MAIN UART i ciągłym USB UART VoxOneBT.
3. Jeśli SBC rozstrzyga problem po stronie Raspberry, najpierw usunąć i
   udowodnić tę przyczynę. Jeśli SBC jest niezerowe, wykonać osobno
   zatwierdzony jeden cykl z czystym obrazem probe, po czym przywrócić
   bitowo `d76b139` i VALID.
4. Nie zmieniać produkcyjnego gain/gate/I2S bez jednoznacznego wyniku.
5. Dopiero po dowodzie przyczyny i celowanym PASS wykonać 7 brakujących
   cykli. Nie powtarzać volume/MUTE, reconnect-zero-VU ani cykli 1–13.
6. Każdą sesję zakończyć terminalnym monitoringiem, pełnym restore baseline'u
   i osobnym raportem; brak reprodukcji nie oznacza naprawy cyklu 14.

STOP przy utracie READY, zmianie bootId/build/profile/PROTO, zmianie OTA/WWW,
module offline, nowym undervoltage, błędzie UART/I2S/HCI, obcym procesie albo
dryfie stanu, timeout CONNECT bez retry, rozbieżności WS, zmianie liczników w
stabilnym oknie, kolejnym zerze po oknie przejściowym i dwóch pełnych oknach,
albo braku dokładnego restore.

## Dowody

- `events.jsonl`: SHA-256
  `115d7fb3afab83bf58571a3ed5d7c138887ed42e2e3318cda55924a7286bed62`;
- MAIN UART: SHA-256
  `d3851b844f789757e6cfca797eb634b542601760ea151902a2c4f3ece18f57d8`;
- fixture WAV: SHA-256
  `70cecf0641087896c75f126235f3ab1b32247c7fef3cd8a91bc6aa409a6fc6e1`;
- analizowane commity: MAIN `4ef1e53e32d78af94165addf3d2953e0b3939451`,
  VoxOneBT `d76b13911944a413145e54f07e193040a4d87418`, ESP32-A2DP v1.8.2
  checkout `7ca18517ffb7c03bbf36abfa2e860da4f685cdd1`.
