# VoxOne — kierunki rozwoju produktu

Ten dokument opisuje oficjalny zakres planowanych funkcji i koncepcji dla
przyszłych wersji VoxOne. Nie jest listą bieżących zadań implementacyjnych; ich
kanoniczną listą pozostaje [`TODO.md`](TODO.md).

## Hardware

- L1, M1, S1 i DIN1 — przyszłe rewizje obecnych profili sprzętowych.
- VoxOne XL — koncepcja większego wariantu urządzenia.
- W5500 Ethernet — dedykowany kontroler sieci przewodowej na SPI3 profilu L0.
- PCM1808 — posiadany moduł wejścia analogowego AUX przez zewnętrzny
  przetwornik ADC.
- Karta microSD — źródło Local Files na magistrali SPI współdzielonej z
  wyświetlaczem, z osobnym pinem CS.
- Wyjście do zewnętrznej końcówki mocy klasy D.
- A2DP Transmitter — tryb TX w module VoxOneBT; urządzenie działa w danym
  momencie tylko jako odbiornik RX albo nadajnik TX.
- Odbiornik IR — lokalne sterowanie radiem pilotem podczerwieni.

## Audio i DSP

- Tryb LINE OUT ze stałym poziomem, osobną zapamiętaną nastawą, ekranem `LINE`
  oraz konfiguracją w WWW i menu.
- AVRCP Absolute Volume automatyzujące poziom Bluetooth w trybie LINE OUT.
- DSPmini oparty na posiadanym module ADAU1701.
- Parametric EQ.
- Crossover.
- Obsługa subwoofera.
- Loudness.
- AutoEQ.

## Sieć i źródła

- Docelowa matryca źródeł: WEB Radio, Bluetooth, Home Assistant `SOURCE_HA` z
  metadanymi, DLNA, AUX i Local Files; Spotify pozostaje koncepcją na
  przyszłość.
- DLNA z wykorzystaniem `minidlna`, nawigacją folderów i gestami enkodera.
- AUX przez PCM1808.
- Local Files z karty microSD.
- Multiroom synchronizowany przez Snapcast z serwerem działającym na OMV.
- Lokalna konfiguracja Wi-Fi za pomocą enkodera i ekranu urządzenia.

## Sterowanie i UI

- Tryby enkodera: Normalny (obrót reguluje głośność), Odwrócony (obrót
  przełącza stacje) i Tylko stacje (brak regulacji głośności enkoderem), z
  zachowaniem stałego poziomu w LINE OUT.
- Gesty Menu: obrót — nawigacja, klik — wybór, dwuklik — wstecz, długi klik —
  wyjście.
- Gesty DLNA: długi klik — wejście, klik — folder lub utwór, dwuklik — katalog
  nadrzędny.
- Dwuklik w widoku Player przełączający źródła.
- Kontekstowa górna belka, między innymi `MENU` i `AUDIO`, oraz osobny ekran
  `LINE`.
- Docelowe ekrany `HOME`, `STATIONS`, `SETTINGS`, `UPDATE` i `STANDBY`.
- Opcjonalna obsługa ekranu LCD ST7920 w późniejszym etapie rozwoju.

## Multiroom i sterowanie zewnętrzne

- VoxOnePilot na ESP32-C3, komunikujący się przez WebSocket i przyjmujący
  komendy z pilota IR.
- Snapcast z serwerem OMV jako warstwa dokładnej synchronizacji wielu
  urządzeń VoxOne.
