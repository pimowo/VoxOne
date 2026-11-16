# VoxOne — Roadmapa

Ten plik jest jedyną kanoniczną listą otwartych prac w projekcie. Zawiera wyłącznie
zadania niewykonane albo wymagające fizycznej weryfikacji. Historia zakończonych
checkpointów pozostaje w historii Git i w istniejących raportach.

## 1. HIL PENDING (do fizycznego przetestowania przez użytkownika)

- [ ] Po VOXONE-STABILIZE-01B.2A fizycznie potwierdzić Power REST na L0/S0:
  GET bieżącego trybu, POST `202` → `saved`, trwałość i ponowienie po błędzie
  NVS, sesje/Origin oraz brak restartów `async_tcp`. Zapis preferencji nie
  może sam wywołać standby ani przełączenia przekaźnika. Testy programowe
  używają symulowanego HTTP/NVS, bez sterowania fizycznym zasilaniem.
- [ ] Po VOXONE-STABILIZE-01B.1 fizycznie potwierdzić na L0/S0 zapis HA name
  i display theme przez REST `202` → odczyt wyniku operacji: oczekiwanie,
  trwałość ustawień, izolację sesji, anulowanie po wygaśnięciu uprawnień,
  kolejność z ustawieniami WS i brak restartów `async_tcp`. Testy natywne
  używają symulowanego HTTP/NVS i nie zastępują HIL ani pomiaru stosu.
- [ ] Po VOXONE-STABILIZE-01A (wraz z otwartym HIL
  VOXONE-WWW-AUDIO-CONFIG-CRASH-FIX-1) fizycznie potwierdzić na L0/S0 zwykłe
  ustawienia WebSocket (barwa, balance, startup, jasność, encoder/flip,
  timeouty/scroll, interwały czasu, HA discovery, LINE OUT i VU): kolejność
  przyjętych zmian i szybkich presetów/Bass/Middle/Treble, trwałość po
  restarcie, brak przedwczesnego potwierdzania zapisu i restartów `async_tcp`.
  Potwierdzić regresję głośności/MUTE oraz NEXT/PREV; wynik natywny nie
  zastępuje HIL ani pomiaru zapasu stosu.
- [ ] Po VOXONE-HLS-MEMORY-SAFETY-1 fizycznie potwierdzić na L0 i S0
  odtwarzanie HLS z absolutnymi i względnymi URL wariantów oraz segmentów, w
  tym playlistą bazową zakończoną `/`, bez regresji przełączania i ponawiania
  strumienia; po VOXONE-TS-PARSER-SAFETY-1/2/3 objąć ten HIL również standardowymi
  segmentami AAC-in-TS z PAT/PMT zawierającymi pola adaptacyjne, ciągłością
  wielu segmentów oraz rzeczywistą telemetrią AAC, bitrate i sample rate.
- [ ] Po VOXONE-WWW-NEXT-PREV-STACK-FIX-1 fizycznie potwierdzić na L0
  przełączanie NEXT/PREV z WWW, w tym zawijanie pierwsza/ostatnia i szybką
  sekwencję poleceń, bez restartu `async_tcp`, utraty kolejności ani błędnego
  zapisu wybranej stacji.
- [ ] Zweryfikować przycisk zasilania na L0 (GPIO42) i S0 (GPIO3): krótki
  klik (Wakeup/Mute albo Home z Menu/karuzeli), dwuklik otwierający Menu
  Główne wyłącznie z Playera oraz długie naciśnięcie uruchamiające shutdown
  wyłącznie z Playera; potwierdzić, że dwuklik enkodera z Playera otwiera
  Wybór Źródła jednakowo na TFT i OLED; w Menu/karuzeli długie naciśnięcie ma
  wracać do Playera albo otwierać „Zapisać zmiany?” bez wyłączania radia;
  sprawdzić automatyczny powrót Home po 10 s bezczynności oraz restart tego
  czasu przez każdy obrót lub klik w Menu, Źródłach, DLNA, Liście Stacji i
  transporcie BT; sprawdzić nawigację enkoderem bez zmiany głośności przy
  otwartej karuzeli, klik wyboru, dwuklik cofania o poziom i długi klik wyjścia
  do Playera,
  renderowanie hierarchii bez tekstowych wskaźników `>` na TFT/OLED,
  sekwencję wyłączania
  „Do widzenia!” z natychmiastowym zatrzymaniem audio, płynne odświeżanie Menu,
  kontekstową belkę Menu na TFT, wygaszenie ekranu z ochroną OLED oraz tryby
  enkodera NORMAL/REVERSED (w tym obrót z wciśniętym przyciskiem i
  synchronizację ustawienia pomiędzy WWW a Menu), głośność po wybudzeniu ze
  Standby dla `Startup Volume` ustawionego na wartość stałą oraz „Ostatnia”
  (wraz z brakiem trzasku przy odblokowaniu DAC), kontekstowe karuzele po
  długim kliknięciu w trybie NORMAL oraz nakładkę głośności po długim kliknięciu
  w trybie REVERSED, a także transakcyjny ekran
  „Zapisać zmiany?” na TFT/OLED: zapis przez TAK, pełne przywrócenie wyglądu i
  ustawień przez NIE oraz natychmiastowe wyjście, gdy nie ma zmian; dodatkowo
  sprawdzić polskie znaki w nazwach stacji i metadanych ICY/DLNA/ID3,
  wyśrodkowanie kontekstowej belki i wielokropki długich nazw na L0 oraz brak
  migotania przy zmianie metadanych/Wi-Fi na TFT i przewijaniu na OLED S0;
  ocenić widoki stacji, DLNA, Źródła i Menu: karuzelę (nagłówek, jedna duża
  pozycja, licznik oraz prawostronny wskaźnik `GRA`) na TFT L0 oraz
  wielowierszową listę z akcentem zaznaczenia bez inwersji na OLED S0;
  potwierdzić płynne przewijanie długich nazw źródeł (w szczególności
  „Home Assistant”) zamiast wielokropka oraz poprawne ikony WEB, BT, HA,
  DLNA, AUX i SD w dolnej belce Playera na TFT/OLED;
  dla pustych list DLNA/Źródła potwierdzić wyśrodkowany napis „Brak elementów”
  oraz działające wyjście dwuklikiem (Back) i długim kliknięciem (Home);
  po zimnym starcie L0 potwierdzić wzrokowo pełne przejście TFT z logo do
  widoku Playera (zdalny most nie ma podłączonej kamery).
- [ ] Na L0 z rzeczywiście sparowanym i odtwarzającym telefonem potwierdzić
  słyszalny dźwięk Bluetooth po wyborze źródła, niezerowy transfer I2S do
  DAC także przy opóźnionym stanie AVRCP, synchronizację ustawionej głośności
  oraz zachowanie MUTE; sprawdzić brak słyszalnych ubytków przy krótkiej
  kontencji I2S, zamknięcie toru po fizycznym zatrzymaniu PCM i ograniczenie
  restartów I2S1 do pięciu kolejnych nieudanych prób.
- [ ] Fizycznie przetestować media z Home Assistant jako pełnoprawne źródło
  `SOURCE_HA`: metadane title/artist i `N/A` zamiast bitrate na TFT/OLED,
  pozostanie na HA po EOF bez flagi, powrót do wcześniej używanego źródła po
  EOF z `return_to_previous=true` oraz standardową komendę Stop.
- [ ] Fizycznie zweryfikować na DIN0 wspólny pinout bazy Zero odziedziczony z
  S0: I2S `BCLK5/DOUT6/LRC4`, enkoder `A9/B8/KEY7`, I2C `SDA1/SCL2` oraz
  przycisk zasilania GPIO3.
- [ ] Fizycznie podłączyć i przetestować przekaźnik dla trybu Hard Power.
- [ ] Sprawdzić na L0 i S0 tryb LINE OUT: stały poziom wyjścia,
  oddzielne zapamiętywanie nastawy, napis `LINE`, konfigurację w WWW i menu
  oraz automatyczny AVRCP Absolute Volume z rzeczywistym źródłem Bluetooth.
- [ ] Na L0 przywrócić odpowiedź fizycznej karty microSD po hard resecie
  (`CMD0` nie odpowiada przy 4 MHz/1 MHz także wtedy, gdy montowanie odbywa się
  przed inicjalizacją ST7796S), a następnie potwierdzić słuchowo odtwarzanie i
  stabilność po wdrożeniu blokady FAT/SPI oraz odczytów SD w paczkach 1–4 KiB.
- [ ] Dokończyć fizyczny test DLNA na
  `minidlna`: sprawdzić wykrywanie i
  przełączanie co najmniej dwóch serwerów w panelu WWW oraz powrót do
  poprzedniego źródła po naturalnym EOF; na urządzeniu sprawdzić nawigację
  enkoderem, klik folderu lub utworu, dwuklik do katalogu nadrzędnego oraz
  płynność renderowania na TFT i OLED; test odtwarzania z Windows DLNA jest
  **ZABLOKOWANY** do czasu włączenia komputera z Windowsem i podłączenia go do
  sieci (obecnie VoxOne wykrywa wyłącznie serwer OMV); na Windows WMPNSS
  potwierdzić wybór
  zasobu MP3/FLAC/AAC zamiast okładki JPEG lub WMA, poprawne długie URL z
  encjami XML bez OOM, nagłówek `User-Agent: VoxOne/<wersja>`, odtwarzanie po
  przekierowaniu 302 oraz poprawne WWW/PWA po restarcie.
- [ ] Po implementacji DSPmini przetestować posiadany moduł ADAU1701, w tym
  crossover, Parametric EQ, subwoofer i Loudness.
- [ ] Po implementacji AUX przetestować posiadany moduł PCM1808 i przełączanie
  źródła analogowego.
- [ ] Po implementacji W5500 sprawdzić Ethernet na SPI3 profilu L0 pod
  obciążeniem audio.
- [ ] Po dodaniu wyjścia klasy D zweryfikować poziomy i mute z rzeczywistą
  zewnętrzną końcówką mocy.
- [ ] Po implementacji VoxOnePilot sprawdzić ESP32-C3 z WebSocket i odbiornikiem
  IR.
- [ ] Po implementacji VoxOneBT sprawdzić naprzemienne tryby A2DP RX/TX.
- [ ] Po dodaniu LCD ST7920 sprawdzić renderowanie i obsługę urządzenia na tym
  ekranie.
- [ ] Po wdrożeniu Snapcast sprawdzić słyszalną synchronizację kilku urządzeń z
  serwerem OMV.

## 2. Wersja 1.2.0 (UI / UX / Sterowanie)

- [ ] Dodać trzeci tryb enkodera „Tylko stacje” (bez regulacji głośności) oraz
  zapewnić jego poprawne zachowanie z LINE OUT dla wszystkich trybów enkodera.
- [ ] Rozbudować działający szkielet Menu na TFT i OLED o pełne ekrany
  konfiguracji oraz odczyt i zapis ustawień; zachować obsługę enkoderem.
- [ ] Dodać docelowe widoki `HOME`, `STATIONS`, `SETTINGS`, `UPDATE` i
  `STANDBY`, zachowując kontekstową górną belkę i układy TFT/OLED.
- [ ] Dodać lokalną konfigurację Wi-Fi za pomocą enkodera i ekranu urządzenia.
- [ ] Zintegrować VoxOnePilot na ESP32-C3: obsłużyć sterowanie przez WebSocket
  oraz odbiór poleceń z pilota IR.
- [ ] Po przejściu panelu na HTTPS dodać Service Workera i tryb offline do
  wdrożonego manifestu instalacyjnego PWA; nie rejestrować Service Workera
  przy obecnym transporcie HTTP.
- [ ] Dodać opcjonalną obsługę ekranu LCD ST7920 w późniejszym etapie rozwoju.

## 3. Wersja 1.3.0+ (Audio / Sprzęt / Sieć)

- [ ] Fizycznie potwierdzić pinout M0 po migracji na wspólną bazę ESP32-S3
  N16R8 DevKit i zmianie domyślnego wyświetlacza na SSD1322 oraz uzupełnić
  `AMP_ENABLE` i `MUTE/XSMT` dla L0, S0, M0 i DIN0.
- [ ] DSPmini: obsłużyć ADAU1701 przez I2C, dodać konfigurację do WWW oraz
  Parametric EQ, Crossover, wyjście subwoofera, Loudness i AutoEQ; wykorzystać
  posiadany moduł do testów HIL.
- [ ] W5500 Ethernet: dodać obsługę przewodowego internetu na profilu L0 przez
  dedykowaną magistralę SPI3.
- [ ] Uzupełnić Multi-DLNA o opcjonalne ręczne adresy IP oraz osobny wybór
  aktywnego serwera bezpośrednio w menu urządzenia.
- [ ] AUX: dodać wejście analogowe oparte na PCM1808 i zintegrować je z wyborem
  źródła; wykorzystać posiadany moduł do testów HIL.
- [ ] A2DP Transmitter: dodać naprzemienne tryby RX/TX w VoxOneBT, bez ich
  jednoczesnej pracy.
- [ ] Dodać wyjście do zewnętrznej końcówki mocy klasy D i opisać wymagania
  profili sprzętowych.
- [ ] Multiroom: dodać klienta Snapcast i zsynchronizowane odtwarzanie na wielu
  urządzeniach z wykorzystaniem serwera OMV.
- [ ] Zaprojektować przyszłe rewizje sprzętowe L1, M1, S1 i DIN1.
- [ ] Ocenić i opisać zakres większego wariantu VoxOne XL.
- [ ] FLAC Optimization: przeprowadzić test HIL strumieni FLAC o wysokim
  bitrate i ocenić liczniki underflow/minimum zapełnienia; przy migracji na
  framework budowany ze źródeł ustawić kompilacyjne `TCP_WND=32768` i
  `TCP_RECVMBOX_SIZE=32` (obecny Arduino Core 2 używa prekompilowanego lwIP).

## 4. Backlog / Inne

- [ ] WWW/Config — etap 01B.2B: przenieść poza `async_tcp` pozostały zwykły
  trwały zapis endpointu REST hardware, bez
  zmiany enforcement ani transakcyjnego restore; zachować lifetime żądań
  i potwierdzać zapis dopiero po sukcesie. Uwzględnić ponowienie tej samej
  wartości po błędzie persistence (RAM nie jest dowodem trwałości).
- [ ] WWW/Config — wykonać pełny test przeglądarki HA/theme z dostosowanym
  harnessem `test/voxone_update_headless.py` (`202` → odczyt wyniku operacji).
  Celowane testy Node wykonują produkcyjne formularze i wspólne scenariusze
  mocków REST, ale symulują DOM/HTTP/autoryzację/persistence; nie zastępują
  Chromium ani HIL. Zachować wcześniejszą diagnostykę timeoutu Chromium.
  Dodać analogiczny scenariusz Power REST do harnessu przeglądarki;
  jego formularz i kontrakt `202` są obecnie objęte celowanym testem Node.
- [ ] MPEG-TS PSI: dodać weryfikację CRC32 PAT/PMT, poprawnie ignorować
  duplikaty continuity counter podczas składania sekcji wielopakietowych oraz
  zastąpić albo formalnie uzasadnić limit pierwszych czterech PID PMT wraz z
  wyborem programu; zachować ograniczone bufory i testy ASan/UBSan.
- [ ] HLS: dodać testy integracyjne rzeczywistego `Audio::parsePlaylist_M3U8`
  (nie tylko resolvera) oraz kontrolowaną obsługę nieprawidłowych i
  niekompletnych URL, `#EXTINF` bez następnego URL i rozdzielenie błędu
  playlisty od prawidłowego braku nowego segmentu.
- [ ] HLS/MPEG-TS: dodać pełny test integracyjny ścieżki
  `Audio::loop()` → `httpPrint()` → `parsePlaylist_M3U8()` →
  `processWebStreamTS()` → rzeczywisty `InBuff`/dekoder oraz zachowania
  Playera i SourceManagera. Obecny natywny harness wykonuje wyciętą bez zmian
  metodę produkcyjną `processWebStreamTS()` z produkcyjnym parserem i recovery,
  ale używa `FakeClient`/`FakeBuffer` i nie obejmuje pobierania playlisty,
  żądań HTTP ani zewnętrznego przejścia PLAYING/STOP/reconnect.
- [ ] HLS: zweryfikować i w razie potrzeby skorygować 10-sekundowy timeout
  transportu, gdy w `InBuff` nadal są dostępne dane audio lub dekoder wywiera
  backpressure; dodać regresję wykluczającą fałszywy STOP/reconnect bez
  maskowania rzeczywistego braku postępu transportu.
- [ ] HLS: zastąpić zgodnościowe łączenie URL pełnym rozwiązywaniem referencji
  według RFC 3986 oraz jawnie ograniczyć schematy do HTTP/HTTPS, zabezpieczając
  `httpPrint` przed błędnym lub niezgodnym schematem.
- [ ] GPL / yoRadio: przeprowadzić analizę prawną i w razie potrzeby usunąć
  zależności.
