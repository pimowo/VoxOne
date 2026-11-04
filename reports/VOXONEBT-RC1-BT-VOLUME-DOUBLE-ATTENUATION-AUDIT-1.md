# VOXONEBT — RC1-BT-VOLUME-DOUBLE-ATTENUATION-AUDIT-1

Data: 2026-11-03. Zakres: analiza kodu i zachowanych dowodów oraz testy
natywne offline. Bez kontaktu z L0/VoxOneBT, HIL, OTA, flashowania, restartu
lub zmian firmware i konfiguracji.

Werdykt: **INCONCLUSIVE**. Podwójne zastosowanie `69/4096` reprodukuje
deterministycznie sygnaturę `1642 -> 27 -> 0`, ale produkcyjna ścieżka
`d76b139` zawiera tylko jedno wywołanie regulatora na callback PCM. Normalna
sekwencja PAUSE -> PLAY nie wywołuje regulatora ponownie, nie zmienia factor
i w teście natywnym daje po resume ponownie `1642 -> 27`. Nie potwierdzono
ani przyczyny cyklu 14, ani defektu produkcyjnego powodującego podwójne
tłumienie.

## Baza audytu

- VoxOneBT: czysty eksport drzewa
  `d76b13911944a413145e54f07e193040a4d87418`; bieżący dirty worktree,
  zawierający eksperyment AVRCP STOP i sondy, nie był używany jako źródło.
- Zależność produkcyjna: ESP32-A2DP tag `v.1.8.2`, commit
  `7ca18517ffb7c03bbf36abfa2e860da4f685cdd1`, zgodnie z pinem w
  `platformio.ini` z `d76b139`.
- MAIN i dowody fizyczne: `4ef1e53`; 13 pełnych cykli zachowuje PHYSICAL PASS,
  cykl 14 pozostaje częściowy i niezaliczony.
- `docs/TODO.md` nie zmieniono. Nie zmieniono też kodu żadnego repozytorium;
  test i czysty eksport są w katalogu dowodowym poza repozytoriami.

## Rzeczywista ścieżka callback -> I2S

```text
ESP-IDF/Bluedroid: zdekodowane SBC, S16 stereo
  -> BluetoothA2DPSink::audio_data_callback(data, len)
       -> opcjonalny swap L/R                         [wyłączony]
       -> raw_stream_reader(measureRawAudio)          [odczyt, bez zapisu]
       -> A2DPDefaultVolumeControl::update_audio_data [jeden raz, in-place]
       -> stream_reader(streamAudio)                  [post-volume]
            -> I2sOutput::write(data, len)
                 -> i2s_write(I2S0, data + offset, ...)
```

`BluetoothService::begin()` rejestruje RAW i post-volume oraz wywołuje
`set_stream_reader(streamAudio, false)`. Argument `false` wyłącza własny
backend I2S biblioteki ESP32-A2DP, więc nie istnieje drugi równoległy zapis
ani drugie wywołanie gain w tym backendzie. `streamAudio()` tylko przekazuje
ten sam bufor do `I2sOutput::write()`, a pętla `write()` tylko obsługuje
częściowe `i2s_write()`; nie skaluje i nie zeruje danych wejściowych.

## Wszystkie miejsca modyfikacji lub wyciszenia PCM

| Miejsce | Operacja | Stan produkcyjny / znaczenie |
|---|---|---|
| `BluetoothA2DPSink::audio_data_callback()` | opcjonalna zamiana kanałów in-place | `swap_left_right=false`; nie jest tłumieniem. VoxOneBT nie ustawia tej opcji. |
| `A2DPVolumeControl::update_audio_data()` | opcjonalny mono downmix, następnie `sample * factor / 4096`, zapis in-place | `mono_downmix=false`; volume jest jedyną produkcyjną operacją amplitudy. |
| volume `0` | factor `0`, więc rzeczywiste wyzerowanie wszystkich próbek | Wymaga ustawienia ABSVOL 0. PAUSE/PLAY tego nie robi. |
| `I2sOutput::setActive(false)` i zmiana rate | stop I2S oraz `i2s_zero_dma_buffer()` | Zeruje kolejkę DMA, nie bufor callbacku. |
| `tx_desc_auto_clear=true` | przy underflow sterownik czyści deskryptor TX, aby nie odtwarzać śmieci | Może dać zera na przewodzie przy braku nowych zapisów, ale nie jest skalowaniem ani drugim przejściem PCM. |
| MAIN `player.outputSilent()` | `memset()` bloku odebranego przez I2S1 | Jest poza VoxOneBT i następuje **po** obliczeniu `##[BT-AUDIO] peak`; nie może wyjaśnić historycznego `peak=0`. |

RAW VU tylko czyta próbki. W produkcyjnym sinku nie znaleziono innego
`memset`, MUTE, gain, saturacji, DSP ani kopii z ponownym skalowaniem między
callbackiem A2DP a `i2s_write()`.

## Pochodzenie `69/4096` i arytmetyka

MAIN mapuje `volume100=12` na ABSVOL 21. Domyślna krzywa ESP32-A2DP liczy:

```text
zero_ofs = 1.4^-12
rawFactor = (1.4^(volume*12/127 - 12) - zero_ofs)
            * 4096 / (1 - zero_ofs)
volume=21: rawFactor ~= 69.8413 -> int32 factor=69
gain = 69/4096 ~= 0.0168457
```

Konwersja `float -> int32_t` odrzuca część ułamkową. Następnie mnożenie i
dzielenie całkowite `int32_t` zaokrągla w kierunku zera:

```text
1642 * 69 / 4096 = 27
  27 * 69 / 4096 = 0
  59 * 69 / 4096 = 0
  60 * 69 / 4096 = 1
```

Factor dla wejść 0..127 jest monotoniczny i mieści się w 0..4096. Największy
iloczyn `32768 * 4096 = 134217728` mieści się w `int32_t`. Ponieważ factor
nie przekracza 4096, wynik mieści się w S16; brak saturacji nie powoduje tu
overflow ani wrap. Przy 127 factor ma 4096 i granice INT16_MIN/MAX przechodzą
bez zmiany.

## AVRCP Absolute Volume i stan po PAUSE/PLAY

- Lokalny `SET_VOLUME[_SESSION]` kończy w `BluetoothA2DPSink::set_volume()`:
  wartość jest ograniczana do 127, factor jest **zastępowany**, regulator
  włączany, a powiadomienie AVRCP wysyłane. Samo ustawienie nie dotyka PCM.
- Zdalny `SET_ABSOLUTE_VOLUME` kończy w `volume_set_by_controller()` i również
  zastępuje `s_volume`/factor. Kolejne callbacki używają jednej aktualnej
  wartości; historia ustawień nie tworzy iloczynu gainów.
- PLAY/PAUSE zmienia `A2dpAudioGate` i stan I2S oraz wysyła AVRCP transport.
  Na tej ścieżce nie ma `set_volume()` ani ponownej konfiguracji regulatora.
- Przy tym samym sample rate 48 kHz `setSampleRate()` wraca bez zmiany
  zegara. Resume wykonuje zero DMA i `i2s_start()`, nie przelicza factor.
- Factor celowo pozostaje zachowany przez PAUSE/PLAY. Obiekt sinka zachowuje
  go także przez reconnect w ramach tego samego procesu, dopóki lokalna lub
  zdalna komenda volume go nie zastąpi. Nowa sesja volume może zatem ustawić
  inną wartość, ale cykl 14 miał PAUSE -> PLAY bez reconnectu.

W klasie volume factor i flaga enable nie mają własnej blokady. Potencjalne
przełączenie między starym a nowym factorem podczas równoległego callbacku
warto objąć telemetrią, ale pojedynczy odczyt skalaru może dać starą albo
nową wartość — nie tworzy drugiego przejścia przez pętlę skalującą. Nie jest
to dowód przyczyny cyklu 14.

## Czy podwójne tłumienie jest możliwe

Rozróżnienie jest kluczowe:

1. **Matematycznie: TAK.** Jawne dwukrotne wywołanie
   `update_audio_data()` na tym samym już zmodyfikowanym `Frame` daje
   deterministycznie `1642 -> 27 -> 0`.
2. **W normalnym przepływie produkcyjnego callbacku: nie znaleziono ścieżki.**
   `audio_data_callback()` wywołuje regulator dokładnie raz, aplikacja nie
   wywołuje go ponownie, a PAUSE/PLAY nie odtwarza bufora ani factor.
3. **W całym systemie: nadal możliwe, ale nieudowodnione.** Ten sam efekt
   wystąpi, jeśli do normalnego pojedynczego gain VoxOneBT trafi PCM już
   stłumiony do około 27 (np. upstream) albo gdy niższa warstwa dostarczy
   ponownie zawartość bufora, której nie nadpisała świeżymi próbkami.

Biblioteka rzutuje wejściowe `const uint8_t*` na `Frame*` i modyfikuje bufor
in-place. Samo ponowne użycie tego samego adresu przez dekoder nie jest
problemem, jeśli dekoder wpisze świeży PCM. Dowody nie pokazują, czy w cyklu
14 treść przed gain była świeża i niezerowa.

## Porównanie z HIL 1–14

- Cykle 1–13 używały tej samej ścieżki i przechodziły PLAY -> PAUSE -> PLAY.
  Cykle 12–13 miały po resume stabilne `peak=27`; to mocny argument przeciw
  deterministycznemu błędowi samej sekwencji PAUSE/PLAY.
- Cykl 14 przed PAUSE miał `peak=27`. Po resume pierwszy raport przejściowy
  nadal miał `peak=27`; dopiero trzy następne pełne okna około 192 kB/s miały
  `peak=0`, przy rosnących `bytes/txBytes` i zerowych błędach transportu.
- Każdy odczyt BlueALSA wykonany przy CONNECT pokazywał `SoftVolume=true`,
  `Volume=127/127`, MUTE false. Harness nie wykonywał takiego odczytu w
  wadliwych oknach po resume. Dlatego pełna głośność przy CONNECT nie wyklucza
  późniejszej zmiany upstream i nie dowodzi braku drugiego stopnia w chwili
  awarii.
- Fixture ma peak 1642 i sygnał w każdym pięciosekundowym oknie. Żywy proces
  `aplay` oraz pełna przepływność nie dowodzą jednak wartości PCM za SBC ani
  w callbacku RAW.

Sygnatura liczbowa jest więc zgodna z dwukrotną kwantyzacją, ale korelacja
nie identyfikuje mechanizmu. Trzynaście PASS pozostaje ważne; cykl 14 nie
jest naprawiony ani zaliczony.

## Test natywny na czystej bazie

Test poza repozytoriami kompiluje bezpośrednio produkcyjny
`A2DPVolumeControl.h` z ESP32-A2DP `7ca1851` i produkcyjny
`A2dpAudioGate.h` z czystego eksportu `d76b139`.

Wynik normalny oraz ASan+UBSan: **PASS/PASS**:

```text
PASS factor21=69 one=27 twice=0 threshold=59/60
     allVolumes=0..127 allInt16 repeatedSet=1000 pausePlay=factor69
```

Zakres:

- wszystkie 128 wartości ABSVOL i wszystkie 65536 wartości S16;
- monotoniczność/range factor, zgodność arytmetyki i brak overflow/wrap;
- granice INT16_MIN/MAX i próg kwantyzacji 59/60;
- 1000 powtórzeń `set_volume(21)` na świeżym PCM — zawsze 27, bez kumulacji;
- zastąpienie 21 -> 20 -> 21 — powrót do factor 69;
- lokalny PAUSE/SUSPENDED/PLAY/STARTED oraz zdalny STARTED po suspend — factor
  pozostaje 69, świeży peak 1642 po resume daje 27;
- jawne drugie przetworzenie tego samego już stłumionego frame — daje 0.

Źródło testu:
`/home/piotrek/voxone-hil/RC1-BT-VOLUME-DOUBLE-ATTENUATION-AUDIT-1-20261103/native/volume_resume_audit.cpp`,
SHA-256 `33a5dd18e6b347da0bfc4e451d0328ad2f6645779a31db5fedd7ff13578ad10f`.

## Pozostałe hipotezy i minimalny następny krok

Na podstawie siły obecnych dowodów pozostają:

1. zera albo próbki o amplitudzie <=59 już w RAW po dekoderze SBC, wskutek
   źródła/BlueALSA/A2DP albo zawartości bufora wejściowego;
2. nieoczekiwana zmiana ABSVOL/factor w czasie cyklu, nieobserwowana przez
   historyczny harness;
3. ponowne dostarczenie już stłumionej treści bufora do pojedynczego normalnego
   gain — matematycznie zgodne, lecz bez znalezionej ścieżki w aplikacji;
4. brak nowych skutecznych zapisów i zera z DMA/`tx_desc_auto_clear` mimo
   aktywnego zegara I2S;
5. strata po `i2s_write()` na DMA/przewodach/I2S1 MAIN.

Zachowane logi nie rozdzielają tych warstw. Minimalny rozstrzygający następny
krok, dopiero po osobnym zatwierdzeniu, to **jeden** PLAY -> PAUSE -> PLAY z
jednoczesnymi oknami:

- RAW peak/nonzero/bytes przed volume;
- post-volume peak/nonzero/bytes i bieżący ABSVOL/factor;
- request/written bytes oraz inactive/error/short-write I2S;
- fazowy readback BlueALSA Volume/Mute/Running i MAIN `peak`.

Najpierw można dodać `btmon` i fazowe readbacki BlueALSA bez zmiany firmware,
ale nie rozdzielą one regulatora VoxOneBT od I2S TX. Pełne rozstrzygnięcie
wymaga domyślnie wyłączonej, wydzielonej sondy na czystym `d76b139` albo
zewnętrznego analizatora I2S. Nie należy uruchamiać dalszego stressu ani
zmieniać gain na podstawie samej sygnatury `1642 -> 27 -> 0`.
