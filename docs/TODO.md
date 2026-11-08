# VoxOne — Roadmapa

Ten plik jest jedyną kanoniczną listą otwartych prac w projekcie. Zawiera wyłącznie
zadania niewykonane albo wymagające fizycznej weryfikacji. Historia zakończonych
checkpointów pozostaje w historii Git i w istniejących raportach.

## 1. HIL PENDING (1.1.x — do fizycznego przetestowania przez użytkownika)

- [ ] Zweryfikować przycisk zasilania na L0 (GPIO42) i S0 (GPIO3): krótki
  klik (Wakeup/Mute, wejście w podmenu lub wybór pozycji), dwuklik (otwarcie
  Menu albo powrót z podmenu), długie naciśnięcie (zamknięcie Menu albo
  shutdown), nawigację enkoderem bez zmiany głośności przy otwartym Menu,
  renderowanie hierarchii i wskaźników `>` na TFT/OLED, sekwencję wyłączania
  „Do widzenia!” z natychmiastowym zatrzymaniem audio, płynne odświeżanie Menu,
  kontekstową belkę Menu na TFT oraz wygaszenie ekranu z ochroną OLED.
- [ ] Fizycznie przetestować TTS/media z Home Assistant: źródło `SOURCE_HA`,
  metadane title/artist i `N/A` zamiast bitrate na TFT/OLED oraz powrót do
  wcześniej używanego źródła i jego stanu po EOF albo komendzie Stop.
- [ ] Zweryfikować mapowanie pinów na nowym profilu DIN0 po zmianie `I2S_LRC`
  na GPIO9.
- [ ] Fizycznie podłączyć i przetestować przekaźnik dla trybu Hard Power.
## 2. Wersja 1.2.0 (UI / UX / Sterowanie)

- [ ] Rozbudować działający szkielet Menu na TFT i OLED o pełne ekrany
  konfiguracji oraz odczyt i zapis ustawień; zachować obsługę enkoderem.
- [ ] Dodać lokalną konfigurację Wi-Fi za pomocą enkodera i ekranu urządzenia.
- [ ] Zintegrować VoxOnePilot: obsłużyć zewnętrzny pilot przez WebSocket i dodać
  odbiornik IR w radiu.
- [ ] Przekształcić obecny interfejs WWW w Progressive Web App, z możliwością
  instalacji i ikoną na telefonie.
- [ ] Dodać opcjonalną obsługę ekranu LCD ST7920 w późniejszym etapie rozwoju.

## 3. Wersja 1.3.0+ (Audio / Sprzęt / Sieć)

- [ ] DSPmini: obsłużyć ADAU1701 przez I2C, dodać konfigurację do WWW oraz
  Parametric EQ, Crossover, wyjście subwoofera, Loudness i AutoEQ.
- [ ] W5500 Ethernet: dodać obsługę przewodowego internetu na profilu L0 przez
  SPI.
- [ ] DLNA / UPnP: rozbudować PoC SSDP + ContentDirectory Browse o nawigację
  w UI, wybór źródła i odtwarzanie plików z telefonu lub NAS przez `minidlna`.
- [ ] AUX: dodać wejście analogowe oparte na PCM1808 i zintegrować je z wyborem
  źródła.
- [ ] A2DP Transmitter: dodać naprzemienne tryby RX/TX w VoxOneBT, bez ich
  jednoczesnej pracy.
- [ ] Multiroom: dodać zsynchronizowane odtwarzanie na wielu urządzeniach.
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
