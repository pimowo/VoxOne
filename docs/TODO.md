# VoxOne — Roadmapa

Ten plik jest jedyną kanoniczną listą otwartych prac w projekcie. Zawiera wyłącznie
zadania niewykonane albo wymagające fizycznej weryfikacji. Historia zakończonych
checkpointów pozostaje w historii Git i w istniejących raportach.

## 1. HIL PENDING (1.1.x — do fizycznego przetestowania przez użytkownika)

- [ ] Zweryfikować przycisk zasilania na L0 (GPIO42) i S0 (GPIO3): długie
  naciśnięcie, sekwencję wyłączania „Do widzenia!”, zatrzymanie normalnego
  odświeżania oraz wygaszenie ekranu z ochroną OLED.
- [ ] Fizycznie przetestować TTS z Home Assistant i wybór źródła, w tym powrót
  do wcześniej używanego źródła po zakończeniu komunikatu.
- [ ] Zweryfikować mapowanie pinów na nowym profilu DIN0 po zmianie `I2S_LRC`
  na GPIO9.
- [ ] Fizycznie podłączyć i przetestować przekaźnik dla trybu Hard Power.

## 2. Wersja 1.2.0 (UI / UX / Sterowanie)

- [ ] Rozbudować obsługę przycisku zasilania: krótki klik — Mute, dwuklik —
  Menu, długi klik — On/Off.
- [ ] Stworzyć pełne menu konfiguracji na urządzeniu dla TFT i OLED, obsługiwane
  enkoderem.
- [ ] Zintegrować VoxOnePilot: obsłużyć zewnętrzny pilot przez WebSocket i dodać
  odbiornik IR w radiu.
- [ ] Przekształcić obecny interfejs WWW w Progressive Web App, z możliwością
  instalacji i ikoną na telefonie.

## 3. Wersja 1.3.0+ (Audio / Sprzęt / Sieć)

- [ ] DSP Mini: obsłużyć zewnętrzne procesory DSP, na przykład ADAU1701, przez
  I2C i dodać ich konfigurację do interfejsu WWW.
- [ ] W5500 Ethernet: dodać obsługę przewodowego internetu na profilu L0 przez
  SPI.
- [ ] DLNA / UPnP: umożliwić wysyłanie muzyki z sieci lokalnej, między innymi
  z telefonu i NAS.
- [ ] FLAC Optimization: zoptymalizować dekoder audio przez przypisanie do
  rdzenia i użycie buforów w PSRAM oraz porównać rozwiązanie z projektem vtom.

## 4. Backlog / Inne

- [ ] GPL / yoRadio: przeprowadzić analizę prawną i w razie potrzeby usunąć
  zależności.
- [ ] Ostatecznie uporządkować stare wpisy i testy.
