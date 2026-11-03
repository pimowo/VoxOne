# VOXONE-L0-BT-LIFECYCLE-HIL-1

Data: 2026-11-02, 09:57:12–09:57:40 CET. Wynik zbiorczy: **PARTIAL — STOP po pierwszej anomalii**. Nie wykonano pięciu cykli ani pełnej macierzy Source Manager; nie przypisuje się AUDIO PASS ani zamknięcia bramek RC1.

## Preflight i granice próby

- L0: `0.8.0-dev` / build `bf1d147`, profil L0, `bootId=ca5d5657`, READY, `app0`/image `valid`, WWW READY/verified/compatible. Początkowo `BT/STOP`, `btConnected=true`, stacja #1 `Afterhours`, `volume=2/50` (`volume100=4`), MUTE OFF, balance 0, freeHeap 146252 B, free PSRAM 7828731 B, RSSI −62 dBm.
- Raspberry: `VoxOneBT-EFF35A` `30:76:F5:EF:F3:5A` było przed próbą `Paired=yes`, `Bonded=yes`, `Connected=yes`, `Trusted=no`. BlueALSA 4.3.1 udostępniała A2DP Source/SBC, PCM S16_LE stereo 48 kHz; `bluetooth`, `bluealsa` i `bluealsa-aplay` były aktywne. Nie wykonywano pair, trust, instalacji ani restartu usług. Pi `get_throttled=0x50000` nie ma bieżących bitów throttling; wartość historyczna pozostała taka sama po teście.
- Nie było innego procesu HIL/loggera. Osobny FT232R `L0_LOG_RX` uzbrojono przed zmianą stanu; aktywny był jeden klient WS. Nie otwierano USB VoxOneBT. Syntetyczny WAV PCM16 stereo 48 kHz, 10 s, limit −26 dBFS, SHA256 `d0b913799f15bd0f30715ba975c6b1b50b405c5871cf7afa39a6fdbf763a0d41`.
- Ponieważ połączenie Raspberry było zastane, użytkownik jawnie wybrał końcowy restore `BT connected/STOP`. Ograniczony runner dowodowy jest poza repozytorium: `/home/piotrek/voxone-hil/bt-lifecycle-hil-1/runner.py`, SHA256 `9f138d665f9da2991da5cfdd15d2be699e4448aaa9b55f2fb37475159df2555c`. Wykorzystał istniejące mechanizmy WS, loggera i nadajnika BlueALSA, ale nie traktował zastanego połączenia jako nowego bondingu.

## Rzeczywisty przebieg

| Czas CET | Zdarzenie i odczyt |
|---|---|
| 09:57:17 | FT232R ARMED, WS i BlueALSA gotowe; `BT/STOP`, `btConnected=true`. |
| 09:57:24 | Kontrolowany ręczny wybór RADIO i PLAY, następnie rozłączenie zastanego A2DP: `RADIO/PLAY/playing`, `btConnected=false`, stacja #1. |
| 09:57:27 | Jeden reconnect tego samego bondu: automatyczny wybór `BT` z powodem `bt-connect` w UART; `btConnected=true`, lecz status `STOP/stopped`. BlueALSA zestawiła A2DP Source/SBC 48 kHz. |
| 09:57:27–36 | Jedno uruchomienie WAV przez `aplay` otworzyło PCM i rozpoczęło transport; UART L0 potwierdza I2S1 RX oraz 146110, potem 191961 B/s odbioru, `rxBlocks` 714→1653, bez read/write errors. Jednak każdy świeży WS readback przez 8 s nadal pokazywał `BT/STOP/stopped`, a UART `tx=0 B/s`, `txBytes=0`. |
| 09:57:36 | Pierwsza anomalia: brak oczekiwanego `BT/PLAY/playing` mimo danych A2DP. STOP, bez kolejnej próby ani cykli 2–5. |
| 09:57:37–40 | WAV zatrzymany; restore i końcowy readback potwierdziły stan początkowy. Logger celowo zakończono. |

Wyniki w potwierdzonym zakresie: pairing/bonding i A2DP Source/SBC **PASS**, otwarcie PCM oraz dostarczanie danych do I2S1 RX **PASS**, przejście `RADIO/PLAY` → automatyczne `BT` po connect **PASS**. Oczekiwany stan `BT/PLAY` i routing RX→DAC **nie zostały osiągnięte** (`txBytes=0`); dlatego pierwszy cykl nie został zaliczony. STOP/RESUME, disconnect podczas PLAY, ręczny RADIO podczas BT PLAY, pięć cykli, VU i jakość analogowa pozostają **UNVERIFIED**. Nie było odsłuchu ani pomiaru analogowego.

## Interpretacja anomalii

To nie jest brak strumienia A2DP: dziennik BlueALSA pokazał `PCM1.Open`, start transportu A2DP Source/SBC o 09:57:27 i `PCM drop` po STOP o 09:57:36, a UART potwierdził rosnące liczniki I2S1 RX. W kodzie VoxOneBT `BluetoothService::updatePlayback()` aktualizuje stan PLAY/STOP z callbacku AVRCP playstatus (`src/bluetooth/BluetoothService.cpp`); osobny callback A2DP audio state nie ustawia tego stanu. W MAIN `SourceManagerState::bluetoothAudioOutputAllowed()` wymaga `BtPlayback::Playing`, a `BtAudioInput::loop()` przekazuje ten stan do routingu I2S0. Zatem przy `BT/STOP` odbierane próbki mogą pozostać bez wyjścia, zgodnie z zaobserwowanym `txBytes=0`.

Na Pi działa `mpris-proxy`, ale odczyt user D-Bus podczas postmortem nie wykazał zarejestrowanego `org.mpris.MediaPlayer2.*`. **Hipoteza**, nie dowód przyczyny: sama transmisja `aplay`/BlueALSA nie dostarczyła AVRCP PLAY. Bez równoczesnego śladu AVRCP albo diagnostyki VoxOneBT nie da się rozstrzygnąć, czy zawiódł nadajnik statusu, interoperacyjność, czy moduł. Nie należy na tej podstawie zmieniać firmware ani oznaczać defektu MAIN jako potwierdzonego. Najmniejszy kolejny krok to osobno zatwierdzona krótka próba z kontrolowanym, zgodnym z istniejącym historycznym HIL MPRIS/AVRCP PLAY i równoczesną obserwacją statusu VoxOneBT, WS oraz `rx/tx`, bez ponownego pair.

## Stabilność, restore i dowody

- Stan końcowy: `BT/STOP/stopped`, `btConnected=true`, Pi `Paired=yes/Bonded=yes/Connected=yes`, BlueALSA PCM `Running=false`, brak testowego procesu `aplay` i loggera; stacja #1 `Afterhours`, `volume=2/50`/`volume100=4`, MUTE OFF, balance 0. READY, `app0 valid`, WWW READY; `bootId=ca5d5657` bez zmiany. FreeHeap 144296 B względem początkowego 146252 B (−1956 B w porównywalnym BT/STOP); PSRAM free 7828731 B bez zmiany. Minimum freeHeap 92156→85384 B jest historycznym minimum, nie miarą bieżącego wycieku.
- UART RX-only: 7406 B, 137/137 zapisanych niepustych odczytów, zero niezapisanych bajtów, zgodność raw/chunks i `bootId` PASS; brak `panic`, `assert`, WDT, stack canary i markerów korupcji heap. Manifest ma `INTERRUPTED` wyłącznie z powodu celowego zakończenia po teście; **nie** przypisywać 540 s PASS loggera.
- Dowody: `/home/piotrek/voxone-hil/bt-lifecycle-hil-1/20261102T095712+0100-59e6a5/`. `sha256sum -c SHA256SUMS`: 11/11 OK. Najważniejsze SHA256: `uart-uart.raw` `bf71575937228439bcfef99ba0c73b364659cee4fee542b441c9fe94f17483e8`; `ws.jsonl` `1ae59575e269983e5a9c6538d0370985d32e07fcf2741fa5ae0cdc750d9ebfe2`; `events.jsonl` `201577f94abac384a3254322729bafba6a2571b3a9f0ccfd208ca6d73de66684`; `initial.json` `5835f5af98cfc3b49ef673e85998dc83292e17d33514c234e7ffea91a5266164`; `final.json` `406e0b6d48d5a0f6b86a170e675a49696456bb3f1549da3da220f5eb20dc374d`.

`RC1-BT-LIFECYCLE`, `RC1-SOURCE-MANAGER` i `RC1-PROFILE-L0` pozostają OPEN/PARTIAL. Nie zmieniono firmware, SECURITY, konfiguracji radia ani bondingu; nie wykonano OTA, flash, resetu, commit ani push.
