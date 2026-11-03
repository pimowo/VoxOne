# VOXONE-BT-VOLUME-VU-ROOT-CAUSE-1

Data: 2026-11-02

Repozytoria: `/home/piotrek/projects/VoxOne`, `/home/piotrek/projects/VoxOneBT`

Wersje fizyczne objęte dowodami: MAIN `4ef1e53`, VoxOneBT `d76b139`, BlueALSA `4.3.1`, SBC 48 kHz

## Wynik

| Pytanie | Werdykt |
|---|---|
| Gdzie w badanej ścieżce BT tracona jest amplituda? | Jedyny **potwierdzony niejednostkowy gain** podczas HIL znajdował się w `A2DPDefaultVolumeControl` VoxOneBT, przed jego I2S TX. Dla `volume100=12` MAIN wysyła ABSVOL 21, biblioteka wybiera `69/4096`, a próbka 1642 daje 27. |
| Czy działały dwa tłumiki? | **NIE w zachowanym HIL.** BlueALSA miała `SoftVolume: true`, lecz oba kanały miały `Volume: 127`, czyli gain 1,0. MAIN nie stosuje `Audio::Gain()` ani DSP do BT; przekazuje I2S1 do I2S0 bez regulacji amplitudy, poza binarnym MUTE. Dwa potencjalne stopnie istnieją konfiguracyjnie, ale tylko jeden tłumił. |
| Czy BT jest nadmiernie tłumiony względem RADIO? | **Nie potwierdzono defektu gain.** Przy tym samym znanym peak i stanie `volume100=12`, `maximumVolume=50`, nominalne RADIO (neutralne filtry, `ovol=0`) daje około 22 po własnym headroom 1/2 i gain 7/256, a BT daje 27 po 69/4096. Kod sam nie wyjaśnia zgłoszenia „BT dużo ciszej”. Różnica źródeł, masteringu stacji, `ovol` i filtrów RADIO pozostaje fizycznie nieporównana. |
| Czy mechanizm gain powoduje `peak=0` po PAUSE -> PLAY? | **UNVERIFIED.** Matematyka podwójnego zastosowania daje dokładnie `1642 -> 27 -> 0`, lecz kod wywołuje gain dokładnie raz na callback, a PAUSE/PLAY nie zmienia volume/factor. Brak zsynchronizowanego RAW i post-volume z feralnego cyklu nie pozwala rozstrzygnąć zer źródła, ponownego przetworzenia bufora ani warstwy I2S. |
| Czy jest to błąd samego VU? | Dla licznika użytego przez stress: **nie**. `##[BT-AUDIO] peak` jest liczony z fizycznych próbek MAIN I2S1 przed MUTE i I2S0; zero oznacza próbki zerowe na wejściu MAIN. Osobny display VU używa RAW VU VoxOneBT sprzed gain i nie był eksportowany przez WS w tej sesji, więc jego zachowanie po resume pozostaje niezmierzone. |
| Zmiana produkcyjnego audio | Brak. Dodano wyłącznie domyślnie wyłączoną instrumentację i testy; niczego nie wgrano. |

## Dowody fizyczne z istniejącej sesji

Źródło: `VOXONE-L0-BT-STRESS-V2-FINAL-1-20261102T170815+0100/run/events.jsonl`.

- Cykle 12 i 13: stabilne PLAY / PAUSE / PLAY miały odpowiednio `peak=27 / 0 / 27`.
- Cykl 14 przed pauzą: dwie pełne próbki miały `peak=27`, RX/TX około 192 kB/s i brak nowych błędów.
- Cykl 14 po resume: próbka przejściowa i dwa pełne okna przez około 15 s miały rosnące `rxBytes` i `txBytes`, brak `errors`, `writeErrors`, `shortWrites`, ale `peak=0`.
- WAV 60 s jest niezerowy w każdym pięciosekundowym odcinku; nie był to naturalny koniec sygnału.
- Readback PCM dla cyklu 14: `SoftVolume: true`, `Volume: L: 127 R: 127`, bez MUTE. W BlueALSA 4.3.1 soft-volume skaluje przed SBC, lecz 127 jest pełną skalą, więc w tej sesji nie zmieniał PCM.
- MAIN zachował `volume100=12`, surowe `volume=7`, `maximumVolume=50`, MUTE OFF. Nie ma dowodu zmiany tych wartości pomiędzy PAUSE i resume.

SHA-256 zachowanych wejść:

- `events.jsonl`: `115d7fb3afab83bf58571a3ed5d7c138887ed42e2e3318cda55924a7286bed62`
- `uart.raw`: `d3851b844f789757e6cfca797eb634b542601760ea151902a2c4f3ece18f57d8`
- `bt-tone-60s.wav`: `70cecf0641087896c75f126235f3ab1b32247c7fef3cd8a91bc6aa409a6fc6e1`

## Faktyczna ścieżka amplitudy

| Etap | Faktyczna operacja na PCM |
|---|---|
| Raspberry ALSA -> BlueALSA | `aplay` dostarcza S16_LE stereo 48 kHz. BlueALSA 4.3.1 ma domyślnie soft-volume dla A2DP i skaluje przed kodowaniem, ale zachowany readback 127/127 oznaczał mnożnik 1,0. Ustawienie nie było zmieniane w tej analizie. |
| SBC/A2DP | Kodowanie/dekodowanie i ponowna kwantyzacja stratna; brak osobnego user gain w dowodach. |
| VoxOneBT RAW callback | `set_raw_stream_reader(measureRawAudio)` dostaje zdekodowane S16 przed volume. `RawVuMeter` mierzy tutaj. |
| VoxOneBT ABSVOL/volume | `btUserToAbsolute(12)=21`; `A2DPDefaultVolumeControl` liczy factor 69/4096 i modyfikuje S16 in-place, z dzieleniem całkowitym w kierunku zera. To potwierdzony punkt utraty amplitudy. |
| VoxOneBT post-volume/I2S TX | `streamAudio` otrzymuje ten sam bufor po jednym `update_audio_data()` i przekazuje go do `I2sOutput`. Własny backend I2S biblioteki jest wyłączony przez `set_stream_reader(..., false)`, więc nie ma drugiego wyjścia/gain. |
| MAIN I2S1 RX | Odczytuje S16 i oblicza `peak16` **przed** MUTE oraz zapisem I2S0. Nie stosuje tu `Audio::Gain`, balance ani filtrów RADIO. |
| MAIN MUTE/I2S0 TX | Tylko `player.outputSilent()` może wyzerować blok; dzieje się to po pomiarze peak. Następnie blok jest zapisany bez zmiany amplitudy do PCM5102A. |
| PCM5102A | Stałe wyjście DAC; w analizowanym kodzie brak programowalnego gain. |

### RADIO nie korzysta z tej samej ścieżki

RADIO po dekodowaniu wykonuje kolejno: stałe `sample >> 1`, trzy filtry IIR, pomiar VU, `Audio::Gain()` z user volume/balance i zapis I2S0. Przy `volume100=12`, `maximumVolume=50` mapa daje raw 7; dla neutralnych filtrów i `ovol=0` znany peak 1642 daje nominalnie `1642 >> 1 -> 821`, następnie `821 * 7 >> 8 -> 22`. BT dla tego samego peak daje 27. RADIO może być głośniejsze przez inny poziom programu, ustawienie stacji `ovol` i dodatnie gainy filtrów (`bass=5`, `middle=-2`, `treble=4` w HIL), ale istniejący zapis nie jest pomiarem A/B tego samego sygnału.

## PAUSE, PLAY, ABSVOL i reconnect

- Ręczny PAUSE/PLAY zmienia `A2dpAudioGate` i wysyła AVRCP; nie wywołuje `set_volume()` ani nie resetuje `A2DPVolumeControl`.
- `I2sOutput::setActive(false)` zatrzymuje I2S i zeruje DMA. PLAY uruchamia wyjście ponownie po znanym sample rate; nie odtwarza ani nie przelicza gain.
- Test rzeczywistej klasy volume wykazał zachowanie factor 69 przez symulowaną przerwę bez nowego `set_volume`.
- Reconnect tworzy nową sesję protokołu MAIN/VoxOneBT, lecz obiekt biblioteki zachowuje `s_volume` przez czas procesu. Zdalny `SET_ABSOLUTE_VOLUME` może jawnie ustawić nowy factor. MAIN traktuje pierwszy peer value jako baseline i nie zmienia od niego `userVolume`, ale sam sink zastosuje wartość otrzymaną od kontrolera. To jest ważne dla przyszłego reconnect HIL, lecz cykl 14 miał PAUSE -> PLAY bez reconnectu.
- BlueALSA soft-volume nie synchronizuje AVRCP; w trybie soft-volume skaluje lokalnie. W badanej sesji jego wartość była pełna.

## Dwie różne ścieżki „VU”

1. VoxOneBT RAW VU: pomiar przed ABSVOL -> UART `VU L R` -> parser MAIN -> `SourceManager` -> display. PAUSE/STOP zeruje stan, próbki starsze od PLAY są odrzucane, a świeżość wynosi 150 ms.
2. MAIN `##[BT-AUDIO] peak`: maksimum fizycznego I2S1 po gain VoxOneBT; to właśnie pole oceniał stress. Nie przechodzi przez parser VU, WWW ani display.

Dlatego cycle-14 `peak=0` nie jest artefaktem skalowania widżetu. Nie dowodzi jednak, że RAW VU VoxOneBT także było zerowe.

## Testy offline

| Test | Wynik |
|---|---|
| Rzeczywisty `A2DPDefaultVolumeControl` z przypiętego ESP32-A2DP v1.8.2 | PASS: ABSVOL 21 -> factor 69; 1642 -> 27; ponowne przetworzenie 27 -> 0. |
| Kwantyzacja | PASS: przy factor 69 próbka 59 -> 0, próbka 60 -> 1; dzielenie ujemnych próbek jest symetryczne przez truncation toward zero. |
| Overflow/clipping | PASS: factor nie przekracza 4096, INT16_MIN/MAX przy ABSVOL 127 przechodzą bez zmiany; iloczyn mieści się w int32. |
| PAUSE/PLAY i stan gain | PASS offline: bez nowego `set_volume` factor pozostaje 69, a 1642 po resume nadal daje 27. |
| Okna RAW/post-volume | PASS: osobne peak/bytes/callbacks, atomowy `take()` zeruje poprzednie okno; peak sprzed PAUSE nie przecieka do resume. |
| MAIN mapping | PASS: `volumeUserToRaw(12,50)=7`, `btUserToAbsolute(12)=21`, nominalne RADIO 22 vs BT 27 dla tej samej próbki i neutralnego toru. |
| Native + ASan/UBSan | 6/6 PASS w VoxOneBT (`A2dpAudioGate`, `RawVuMeter`, `AudioPathProbe`, `AvrcTgVolumeSession`, `VolumeSessionProtocol`, `A2dpVolumeControl`); MAIN `bt_volume_native` PASS normalnie i pod ASan/UBSan. |

## Minimalna instrumentacja i buildy

Dodano domyślnie wyłączoną flagę `VOXONE_AUDIO_PATH_PROBE_ENABLED` i osobne środowisko `esp32dev-audio-path-probe`. Normalny obraz nie zawiera kodu sondy. W diagnostycznym wariancie jedna linia `[AUDIO_PATH]` na sekundę podaje:

- peak RAW i post-volume;
- bytes i liczbę callbacków obu warstw;
- ABSVOL i rzeczywisty `volumeFactor` (dopiero po `factorKnown=1`);
- connection/playback/gate oraz gotowość rate I2S.

Sonda tylko czyta PCM w już istniejących callbackach i nigdy go nie zapisuje ani nie skaluje. Buildy:

- `esp32dev`: PASS, RAM 44 880 B, Flash 1 141 405 B, SHA-256 BIN `b2696673020fe750c2a4028371c8342cd16dba7b22f9e5caa6c8092b07818dca`;
- `esp32dev-audio-path-probe`: PASS, RAM 44 920 B, Flash 1 142 293 B, SHA-256 BIN `5f646f84b208789a9bb20b7cd607ddda1cba55faf9a6f9d0f8e2fcdfeb9ee376`.

Żaden obraz nie został wdrożony.

## Decyzja o HIL

Nie wykonano pozornego HIL na aktualnym `d76b139`: istniejąca telemetria nie udostępnia równocześnie RAW i post-volume, a byte counters nie dowodzą niezerowych próbek. Read-only sprawdzenie bridge potwierdziło BlueALSA 4.3.1 i usługę bez `--a2dp-volume`; aktualny peer był rozłączony. Plik persistence jest root-only, a `sudo -n` wymaga hasła, więc nie obchodzono uprawnień. Zachowany readback z właściwego cyklu 14 jest wystarczający do potwierdzenia 127/127 w tej sesji.

Minimalny potrzebny retest, po osobnej zgodzie na wdrożenie diagnostycznego obrazu, to jeden krótki PLAY -> PAUSE -> PLAY ze znanym PCM. Trzeba zebrać `[AUDIO_PATH]`, MAIN `##[BT-AUDIO]` i read-only BlueALSA PCM w tych samych pełnych oknach:

- RAW > 0, post = 0: strata w VoxOneBT volume/ponownym przetworzeniu;
- RAW = 0: strata przed RAW callbackiem (źródło/SBC/bufor wejściowy);
- post > 0, MAIN peak = 0: strata po callbacku post-volume, na I2S TX/łączu/I2S1;
- RAW i post > 0, MAIN peak > 0: historyczna anomalia nieodtworzona.

Nie należy przed tym powtarzać 20-cyklowego stressu.

## Zmiany i ograniczenia

- Nie przygotowano poprawki produkcyjnego gain: brak jednoznacznego defektu.
- Przygotowano tylko minimalną, default-off instrumentację i deterministyczne regresje.
- Nie wykonano OTA, flashowania, resetu, zmiany NVS/SECURITY, commit ani push.
- Zastane eksperymentalne zmiany AVRCP STOP w repozytorium VoxOneBT zachowano i nie przypisuje się ich temu zadaniu.
- Formalne bramki RC1 pozostają PARTIAL.

Status zadania: **PARTIAL / ROOT CAUSE OF RESUME PEAK UNVERIFIED; DOUBLE ATTENUATION DISPROVED FOR THE CAPTURED HIL**.
