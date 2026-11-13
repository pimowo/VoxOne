# VoxOne — Roadmapa

Ten plik jest jedyną kanoniczną listą otwartych prac w projekcie. Zawiera wyłącznie
zadania niewykonane albo wymagające fizycznej weryfikacji. Historia zakończonych
checkpointów pozostaje w historii Git i w istniejących raportach.

## 1. HIL PENDING (do fizycznego przetestowania przez użytkownika)

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
- [ ] Fizycznie przetestować TTS/media z Home Assistant: źródło `SOURCE_HA`,
  metadane title/artist i `N/A` zamiast bitrate na TFT/OLED oraz powrót do
  wcześniej używanego źródła i jego stanu po EOF albo komendzie Stop.
- [ ] Zweryfikować mapowanie pinów na nowym profilu DIN0 po zmianie `I2S_LRC`
  na GPIO9.
- [ ] Fizycznie podłączyć i przetestować przekaźnik dla trybu Hard Power.
- [ ] Sprawdzić na L0 i S0 tryb LINE OUT: stały poziom wyjścia,
  oddzielne zapamiętywanie nastawy, napis `LINE`, konfigurację w WWW i menu
  oraz automatyczny AVRCP Absolute Volume z rzeczywistym źródłem Bluetooth.
- [ ] Po implementacji Local Files podłączyć kartę microSD i sprawdzić
  stabilność odtwarzania przy współdzieleniu SPI z wyświetlaczem i osobnym CS.
- [ ] Dokończyć fizyczny test DLNA na
  `minidlna`: sprawdzić wykrywanie i
  przełączanie co najmniej dwóch serwerów w panelu WWW oraz powrót do
  poprzedniego źródła po naturalnym EOF; na urządzeniu sprawdzić nawigację
  enkoderem, klik folderu lub utworu, dwuklik do katalogu nadrzędnego oraz
  płynność renderowania na TFT i OLED; na Windows WMPNSS potwierdzić wybór
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

- [ ] Dokończyć Etap 2 synchronizacji Hardware Matrix v2: po fizycznym
  potwierdzeniu pinów przenieść profil M0 na ESP32-S3 N16R8 i domyślny
  ST7796S oraz uzupełnić `AMP_ENABLE` i `MUTE/XSMT` dla L0, S0, M0 i DIN0.
- [ ] DSPmini: obsłużyć ADAU1701 przez I2C, dodać konfigurację do WWW oraz
  Parametric EQ, Crossover, wyjście subwoofera, Loudness i AutoEQ; wykorzystać
  posiadany moduł do testów HIL.
- [ ] W5500 Ethernet: dodać obsługę przewodowego internetu na profilu L0 przez
  dedykowaną magistralę SPI3.
- [ ] Uzupełnić Multi-DLNA o opcjonalne ręczne adresy IP oraz osobny wybór
  aktywnego serwera bezpośrednio w menu urządzenia.
- [ ] AUX: dodać wejście analogowe oparte na PCM1808 i zintegrować je z wyborem
  źródła; wykorzystać posiadany moduł do testów HIL.
- [ ] Local Files: dodać odtwarzanie z karty microSD na SPI współdzielonym z
  wyświetlaczem, z osobnym pinem CS i bez zakłócania renderowania.
- [ ] Po implementacji backendów AUX i Local Files dołączyć je do wspólnej
  matrycy źródeł SourceManagera i UI; Spotify zachować jako późniejsze
  rozszerzenie.
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

- [ ] GPL / yoRadio: przeprowadzić analizę prawną i w razie potrzeby usunąć
  zależności.
