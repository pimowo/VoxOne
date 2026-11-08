# VoxOne — kierunki rozwoju produktu

Ten dokument opisuje oficjalny zakres planowanych funkcji i koncepcji dla
przyszłych wersji VoxOne. Nie jest listą bieżących zadań implementacyjnych; ich
kanoniczną listą pozostaje [`TODO.md`](TODO.md).

## Hardware

- L1, M1, S1 i DIN1 — przyszłe rewizje obecnych profili sprzętowych.
- VoxOne XL — koncepcja większego wariantu urządzenia.
- W5500 Ethernet — opcjonalna łączność przewodowa.
- PCM1808 — wejście analogowe AUX przez zewnętrzny przetwornik ADC.
- A2DP Transmitter — tryb TX w module VoxOneBT; urządzenie działa w danym
  momencie tylko jako odbiornik RX albo nadajnik TX.
- Odbiornik IR — lokalne sterowanie radiem pilotem podczerwieni.

## Audio i DSP

- DSPmini oparty na ADAU1701.
- Parametric EQ.
- Crossover.
- Obsługa subwoofera.
- Loudness.
- AutoEQ.

## Sieć i źródła

- DLNA z wykorzystaniem `minidlna`.
- AUX.
- Multiroom.
- Lokalna konfiguracja Wi-Fi za pomocą enkodera i ekranu urządzenia.

## UI

- Opcjonalna obsługa ekranu LCD ST7920 w późniejszym etapie rozwoju.
