# VOXONE-STABLE-BT-LOSS-AUDIO-SAFETY-1

Data: 2026-10-24

Stan: **BT LOSS AUDIO SOFTWARE PASS — HIL PENDING**

## Wynik kontraktu

Programowa ścieżka utraty VoxOneBT jest fail-closed po wykryciu zdarzenia:

- `DISCONNECTED` natychmiast czyści sesję, playback, sample rate i VU;
- brak poprawnej ramki UART przez `OfflineTimeoutMs=15000` wywołuje
  `goOffline()`, czyści ten sam stan i generuje fallback `BtOffline`;
- `STOPPED` pozostawia wybrane BT, ale wyłącza oba wyjścia audio, ustawia
  output rate 0, zeruje VU i sprowadza logical XSMT do LOW;
- nagłe offline/disconnect usuwa własność BT nad I2S0. RADIO może odzyskać
  gotowość routingu, lecz XSMT pozostaje LOW przy intencji RADIO STOP;
- RADIO PLAY jest wznawiane wyłącznie z zachowanej intencji, a RADIO STOP nie
  uruchamia autoplay;
- user MUTE pozostaje niezależny od wyboru źródła. Przy aktywnym źródle PCM
  jest zerowane przez `Player::outputSilent()`, a utrata źródła dodatkowo
  wyłącza route i logical XSMT;
- powtarzane disconnect/offline są idempotentne, a spóźnione samodzielne
  `CONNECTED`/`PLAYING`/`VU` nie przywracają modułu po timeout — wymagany jest
  kompletny snapshot statusu;
- reconnect po kompletnym snapshotcie tworzy nową krawędź połączenia, bez
  syntetycznej komendy PLAY;
- utrata BT podczas TTS zachowuje komunikat i intencję RADIO, jednorazowo
  blokując restore/autoplay po zakończeniu.

Timeout 15 s jest istniejącą granicą detekcji utraty UART. Przed jego upływem
oprogramowanie nie ma dowodu, że moduł zniknął; fizyczny HIL musi potwierdzić
ciszę toru także w tym oknie.

## Znaleziony i naprawiony defekt

`btAudioRouteTarget(false, ..., radioRate=0)` zwracał wcześniej
`radioOutput=true`. Oznaczało to gotowy routing RADIO mimo jawnego braku
dostępnego źródła. Dodatkowo szybka ścieżka `BtAudioInput::route()` mogła
utrzymać `radioReady=true` dla zerowego rate.

Minimalna poprawka:

- RADIO jest gotowe tylko wtedy, gdy nie wybrano BT i `radioRate != 0`;
- `radioRate=0` daje jednoznaczny stan no-source: oba wyjścia false i rate 0;
- szybka ścieżka działa tylko dla niezerowego rate;
- ścieżka bez mutexu kopiuje fail-closed wynik `BtAudioRouteTarget`, zamiast
  bezwarunkowo oznaczać RADIO jako gotowe.

Nie zmieniono protokołu VoxOneBT, semantyki fallbacku, architektury audio ani
przygotowanej poprawki AsyncTCP.

## Zmienione pliki

- `src/core/bt_audio_route_state.h`;
- `src/core/bt_audio_input.cpp`;
- `test/bt_audio_route_native.cpp`;
- `test/bt_loss_audio_safety_native.cpp`;
- `tools/verify.ps1`;
- `docs/TODO.md`;
- ten raport.

## Weryfikacja

PASS:

- nowy `bt_loss_audio_safety_native` — build zoptymalizowany oraz ASan/UBSan
  z detekcją wycieków;
- 19 istniejących regresji BT, SourceManager, audio, MUTE/XSMT, VU, RADIO,
  TTS i WebStatus;
- ASan/UBSan: `bt_audio_input_native`, `bt_audio_route_native`,
  `dac_mute_native`, `mute_state_native`, `source_stop_native` oraz
  `temporary_audio_native`;
- PlatformIO `w0` — RAM 75 556 B, flash 1 642 293 B;
- PlatformIO `m0` — RAM 74 852 B, flash 1 638 497 B;
- PlatformIO `s0` — RAM 74 852 B, flash 1 636 445 B;
- PlatformIO `din0` — RAM 74 852 B, flash 1 636 445 B;
- PlatformIO `l0` — RAM 80 128 B, flash 1 716 901 B;
- `git diff --check`.

## Pozostały HIL

Na fizycznym L0 pozostają: poziomy XSMT, rzeczywista cisza i brak trzasków,
ownership oraz zegary I2S0/I2S1, PLAYING → STOPPED, disconnect, fizyczne
odłączenie lub zanik UART modułu, 15-sekundowe okno detekcji, reconnect,
RADIO PLAY/STOP, user MUTE oraz TTS. Wynik programowy nie jest fizycznym PASS
DAC, PCM5102A ani toru analogowego.
