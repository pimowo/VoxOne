# VoxOne — Roadmapa

Ten plik jest jedyną kanoniczną listą otwartych prac w projekcie. Zawiera wyłącznie
zadania niewykonane albo wymagające fizycznej weryfikacji. Historia zakończonych
checkpointów pozostaje w historii Git i w istniejących raportach.

## 1. HIL PENDING (do fizycznego przetestowania przez użytkownika)

- [ ] Zweryfikować przycisk zasilania na L0 (GPIO42) i S0 (GPIO3): krótki
  klik (Wakeup/Mute, wejście w podmenu lub wybór pozycji), dwuklik (otwarcie
  Menu albo powrót z podmenu), długie naciśnięcie (zamknięcie Menu albo
  shutdown), nawigację enkoderem bez zmiany głośności przy otwartym Menu,
  renderowanie hierarchii i wskaźników `>` na TFT/OLED, sekwencję wyłączania
  „Do widzenia!” z natychmiastowym zatrzymaniem audio, płynne odświeżanie Menu,
  kontekstową belkę Menu na TFT, wygaszenie ekranu z ochroną OLED oraz tryby
  enkodera NORMAL/REVERSED (w tym obrót z wciśniętym przyciskiem i
  synchronizację ustawienia pomiędzy WWW a Menu), a także transakcyjny ekran
  „Zapisać zmiany?” na TFT/OLED: zapis przez TAK, pełne przywrócenie wyglądu i
  ustawień przez NIE oraz natychmiastowe wyjście, gdy nie ma zmian; dodatkowo
  sprawdzić polskie znaki w nazwach stacji i metadanych ICY/DLNA/ID3,
  wyśrodkowanie kontekstowej belki i wielokropki długich nazw na L0 oraz brak
  migotania przy zmianie metadanych/Wi-Fi na TFT i przewijaniu na OLED S0;
  po zimnym starcie L0 potwierdzić wzrokowo pełne przejście TFT z logo do
  widoku Playera (zdalny most nie ma podłączonej kamery).
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
- [ ] Dokończyć fizyczny test DLNA na `minidlna`: sprawdzić przeglądarkę w
  panelu WWW i powrót do poprzedniego źródła po naturalnym EOF; na urządzeniu
  sprawdzić nawigację enkoderem, klik folderu lub utworu, dwuklik do katalogu
  nadrzędnego oraz płynność renderowania na TFT i OLED.
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
- [ ] Ujednolicić gesty: w Menu obrót/klik/dwuklik/długi klik odpowiadają za
  nawigację/wybór/wstecz/wyjście, a dwuklik w Playerze przełącza źródła.
- [ ] Rozbudować działający szkielet Menu na TFT i OLED o pełne ekrany
  konfiguracji oraz odczyt i zapis ustawień; zachować obsługę enkoderem.
- [ ] Dodać docelowe widoki `HOME`, `STATIONS`, `SETTINGS`, `UPDATE` i
  `STANDBY`, zachowując kontekstową górną belkę i układy TFT/OLED.
- [ ] Dodać lokalną konfigurację Wi-Fi za pomocą enkodera i ekranu urządzenia.
- [ ] Zintegrować VoxOnePilot na ESP32-C3: obsłużyć sterowanie przez WebSocket
  oraz odbiór poleceń z pilota IR.
- [ ] Przekształcić obecny interfejs WWW w Progressive Web App, z możliwością
  instalacji i ikoną na telefonie.
- [ ] Dodać opcjonalną obsługę ekranu LCD ST7920 w późniejszym etapie rozwoju.

## 3. Wersja 1.3.0+ (Audio / Sprzęt / Sieć)

- [ ] DSPmini: obsłużyć ADAU1701 przez I2C, dodać konfigurację do WWW oraz
  Parametric EQ, Crossover, wyjście subwoofera, Loudness i AutoEQ; wykorzystać
  posiadany moduł do testów HIL.
- [ ] W5500 Ethernet: dodać obsługę przewodowego internetu na profilu L0 przez
  dedykowaną magistralę SPI3.
- [ ] Rozszerzyć klienta DLNA do obsługi maksymalnie trzech serwerów
  równocześnie: automatycznego wykrywania przez SSDP, opcjonalnych ręcznych
  adresów IP oraz wyboru serwera w WWW i menu urządzenia.
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
- [ ] Ostatecznie uporządkować stare wpisy i testy.
