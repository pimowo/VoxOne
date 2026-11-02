# VOXONE-L0-CHAOS-SOAK-8H-1 — wynik fizycznego L0

Wynik: **PARTIAL (BT/TTS/MQTT blocked)**.

- Start: 2026-11-01T23:10:09.187+01:00
- Koniec: 2026-11-02T07:13:11.007+01:00
- Czas monotoniczny: 28980.762 s (target 28800 s)
- Powód STOP: none
- Stan przywrócony: True
- Awaryjne przywrócenie po zakończeniu runnera: {"attempted": false}
- Operacje i próbki: {"samples": 5738, "wsFrames": 29343, "commands": 170, "controlledWsReconnects": 27}
- Metryki: {"freeHeap": {"first": 117508, "last": 159816, "min": 111064, "max": 159848}, "minimumFreeHeap": {"first": 92156, "last": 92156, "min": 92156, "max": 92156}, "psramFree": {"first": 7796119, "last": 7828791, "min": 7739855, "max": 7828791}, "rssi": {"first": -62, "last": -60, "min": -63, "max": -55}, "uptimeSeconds": {"first": 24422, "last": 53221, "min": 24422, "max": 53221}, "httpMs": {"min": 10.6, "max": 130.9}, "wsMs": {"min": 1200.5, "max": 1203.4}, "uart_audioBuffer": {"first": 282989, "last": 282989, "min": 0, "max": 283615}, "uart_loopMaxUs": {"first": 156602, "last": 151223, "min": 17981, "max": 1589547}, "uart_heap": {"first": 117588, "last": 118488, "min": 110324, "max": 159848}, "uart_minHeap": {"first": 92156, "last": 92156, "min": 92156, "max": 92156}, "uart_largestHeap": {"first": 98292, "last": 98292, "min": 81908, "max": 98292}, "uart_psram": {"first": 7800735, "last": 7805431, "min": 7749167, "max": 7828791}, "uart_minPsram": {"first": 7728215, "last": 7728215, "min": 7728215, "max": 7728215}, "uart_rssi": {"first": -60, "last": -60, "min": -63, "max": -55}, "uart_loopStackHwm": {"first": 2892, "last": 2892, "min": 2892, "max": 2892}, "uart_displayStackHwm": {"first": 1876, "last": 1876, "min": 1876, "max": 1876}, "uart_wifi": {"first": 1, "last": 1, "min": 1, "max": 1}}
- Integralność UART: {"status": "PASS", "bytes": 285086, "bootIdChanged": false, "durationRequestedSeconds": 28980.0}
- Markery UART: {"panic": 0, "assert": 0, "WDT": 0, "heap_corruption": 0, "HTTP_TLS_ICY_MP3_error": 0}
- Incydenty do kwalifikacji P0/P1: []
- Playlista bez zmian: True
- Backup konfiguracji bitowo zgodny: True
- Weryfikacja SHA256 logów: PASS

Granice dowodu: aktywny A2DP, TTS i MQTT retained były BLOCKED bez sterowalnego partnera lub autoryzacji brokera; AAC nie miał zweryfikowanej stacji. AUDIO pozostaje UNVERIFIED bez odsłuchu/pomiaru sygnału. Nie ogłaszać RC READY.

Pełny raport, chronologiczny dziennik, WS, UART, próbki i SHA256: `/home/piotrek/voxone-hil/chaos-soak-8h/REPORT.md` oraz `/home/piotrek/voxone-hil/chaos-soak-8h/evidence/SHA256SUMS`.

Powyższe jest automatycznym podsumowaniem runnera; poniżej znajduje się postmortem z 2026-11-02. `docs/TODO.md` uzupełniono wyłącznie o częściowy dowód, bez nowego PASS bramki.

## Integralność i rzeczywiste pokrycie

`sha256sum -c evidence/SHA256SUMS`: **13/13 OK**. Rekonstrukcja 3487 fragmentów `uart-chunks.jsonl` jest bajtowo identyczna z `uart/uart.raw` (285086 B). Manifest loggera `PASS` dotyczy integralności przechwycenia, nie wyniku aplikacyjnego. `uart-live.raw` (283676 B) jest zgodnym prefiksem pełnego raw, ale kończy się **1410 B wcześniej**, bo live-tail nie został opróżniony po zakończeniu loggera. Analiza używa pełnego raw. Automatyczny regex błędów nie jest pełnym dowodem braku każdego błędu transportu/dekodera.

| Faza | Rzeczywiste pokrycie | Status |
|---|---|---|
| F baseline, 30 min | HTTP/WS/UART read-only, 360 próbek | obserwacja pasywna |
| A RADIO, 90 min | 92 komendy STOP/PLAY/stacja | aktywne RADIO |
| B BT, 90 min | 0 akcji BT, brak sterowalnego A2DP Source | **BLOCKED** |
| C TTS, 90 min | 0 żądań TTS, anonimowy MQTT CONNECT `0x05` | **BLOCKED** |
| D WWW/WS/MQTT, 90 min | 18 reconnectów WS, bez MQTT | WS częściowo, MQTT **UNVERIFIED** |
| E mixed, 90 min | 78 komend RADIO/volume/MUTE, 9 reconnectów WS; bez BT/TTS/MQTT | częściowo |

5738 próbek co około 5 s i 29343 ramek WS nie są miarą intensywności scenariuszy. Baseline oraz B/C/D to razem **5 h bez komend zmieniających stan** (w D tylko reconnect WS). Runner zaplanował B i C na pełne 90 min mimo `BLOCKED_CASES` na wejściu. AAC bez zweryfikowanej stacji; USB VoxOneBT nie otwierano bez procedury RX-only. Nie wolno tych faz liczyć jako stress BT/TTS/MQTT.

## Dlaczego tylko 170 komend

`commands.jsonl` zawiera 197 rekordów: **170 komend zmieniających stan** oraz 27 oddzielnie liczonych `WS normal close/reopen`. Wśród 170: `playstation` 73, `stop` 35, `vol100` 26, `mute` 20, `prev` 7, `start` 6, `next` 3. W A było 92, w E 78. Godziny według lokalnego czasu zakończenia readbacku:

| CET | playstation | stop | vol100 | mute | prev | start | next | Razem | WS reconnect |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 01.11 23 | 13 | 3 | 0 | 0 | 2 | 0 | 1 | 19 | 0 |
| 02.11 00 | 36 | 20 | 0 | 0 | 2 | 4 | 2 | 64 | 0 |
| 02.11 01 | 8 | 1 | 0 | 0 | 0 | 0 | 0 | 9 | 0 |
| 02.11 02–03 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 02.11 04 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 10 |
| 02.11 05 | 2 | 1 | 10 | 4 | 1 | 0 | 0 | 18 | 9 |
| 02.11 06 | 12 | 8 | 12 | 14 | 2 | 1 | 0 | 49 | 7 |
| 02.11 07 | 2 | 2 | 4 | 2 | 0 | 1 | 0 | 11 | 1 |

W A akcje rozdzielał losowy odstęp 50–100 s, w E 60–120 s oraz czas readbacku/pauz. `readbackMatched=false` w 27 rekordach dotyczy reconnectów bez predykatu, a nie błędów 170 komend; komendy stanu miały dopasowany readback.

## Liczniki i anomalie

`loopMaxUs=1589547` jest w pełnym UART `##[ENDURANCE]#` o **01.11 23:50:14.004 CET**, uptime 26827 s. `src/main.cpp` mierzy maksimum pojedynczego przebiegu głównej `loop()` przez `micros()` i resetuje licznik po raporcie co 60 s. Zdarzenie nastąpiło **gdzieś w poprzedniej minucie**, niekoniecznie o 23:50:14. O 23:50:08.820 runner zakończył `playstation=1` po STOP stacji 7; UART pokazał STOP → łączenie → PLAY Afterhours. Przy raporcie: `playerRunning=1`, `audioBuffer=282989 B`, Wi-Fi=1, RSSI −57; sąsiednie maksima 465935 i 215339 us. WS około 23:50:15 potwierdza RADIO/PLAY i stały bootId. Związek z przełączeniem jest prawdopodobny, lecz konkretne blokujące wywołanie i słyszalny efekt są **UNVERIFIED**.

`audioBuffer` oznacza `Audio::inBufferFilled()` — bajty wejściowego `InBuff` w chwili minutowego raportu, nie underrun I2S. W 483 raportach ENDURANCE były **64 zera**: 62 przy `playerRunning=0` odpowiadały STOP; dwa przy `playerRunning=1` przypadły na granicę przełączenia stacji: **02.11 00:58:15.482** (`playstation=1`, natychmiast `stop=1`) oraz **06:12:17.915** (UART 7→2/łączenie/PLAY; po minucie 262517 B). Nie znaleziono zera w ustalonym RADIO PLAY oddalonym od zmian stanu. Minutowy sampling nie wyklucza krótszego opróżnienia ani nie dowodzi ciągłości wyjścia audio.

`wsMs=1200.5–1203.4 ms` pochodzi z `Ws.collect(..., seconds=1.2)` w `runner.py`: klient celowo zbiera ramki do końca okna **1,2 s**. To nie timeout serwera ani latency pierwszej odpowiedzi. Podobnie `readbackLatencyMs≈1200` zawiera okno kolekcji; następny runner musi osobno mierzyć czas pierwszej poprawnej odpowiedzi i czas obserwacji.

## Pamięć i restore

Ostatnia próbka `samples.jsonl` ma STOP i freeHeap 159816 B, więc jej porównanie z pierwszą próbką PLAY jest nieważne. Przy tej samej stacji #1 w ustalonym RADIO PLAY, z odrzuceniem 30 s wokół komend, 3479 próbek ma medianę freeHeap w pierwszej/ostatniej połowie **117548/117536 B** (−12 B), PSRAM **7796119/7796119 B**. Dla stacji #2 mediany heap 116960/116968 B (198 próbek), #3 157772/157764 B (59), #7 157780/157812 B (45). STOP: 727 próbek, mediana heap 159816 B; różnice ~40 kB są związane ze stanem/stacją, nie dowodzą wycieku. `minimumFreeHeap=92156 B` to historyczny low-water mark od startu ESP, a nie zużycie narastające w teście.

`preflight.json` i **końcowy** `restored-state.json` potwierdzają `activeSource=radio`, stację **#1 Afterhours**, `playing`, surowe volume **7** i `volume100=12`, `muted=0`, `balance=0`. Metadane naturalnie się zmieniły. Porównywalny końcowy snapshot PLAY ma freeHeap 117692 B wobec 117508 B na początku, PSRAM 7805431 wobec 7796119 B. `postcheck.json`: READY, stały `bootId=ca5d5657`, `app0 VALID`, WWW READY/verified/compatible, niezmienione SHA playlisty i backupu konfiguracji. UART bez wykrytego panic/assert/WDT/heap corruption w zakresie zastosowanego regexu. AUDIO nadal **UNVERIFIED**.

## Autoryzacja, Raspberry Pi 3B i następny runner

Anonimowy CONNECT do brokera zwrócił `0x05 not authorized`; nie należy obchodzić zabezpieczeń ani zgadywać hasła. Zatwierdzona ścieżka TTS istnieje: **użytkownik ręcznie publikuje z Home Assistant** pojedynczy URL na `voxone-8CBD1C/command`, QoS 2, retain false, jak w fizycznych A/B/C na `bf1d147`. `src/core/mqtt.cpp` kieruje URL (>=20 B) do `Player::requestTemporaryUrl()`. Jest to droga interaktywna, nie automatyczne 8 h. WS/HTTP pozwalają sterować RADIO/volume/MUTE i odczytywać stan, lecz nie są równoważnym endpointem TTS. Pasywny retained przez HA ma wcześniejszy HIL PASS; ten runner nie miał klienta MQTT. Reconnect MQTT nadal UNVERIFIED.

Read-only na `voxone-bridge`: BlueZ 5.82 aktywny, `hci0` B8:27:EB:49:56:19 obecny, `Powered=no`, `PowerState=off-blocked`, `/sys/class/rfkill/rfkill0/{soft,hard}=1/0`; `rfkill` CLI nie ma. Nie ma połączonego peer-a ani `pactl`, `wpctl`, `bluealsa` i backendu PipeWire/PulseAudio/BlueALSA. `aplay` sam nie daje A2DP Source. FT232R A7039N0Z nadal wskazuje `/dev/ttyUSB0`. Kontroler BT jest odrębny od USB UART, ale operacyjne A2DP Source i bezkolizyjność po aktywacji RF są **UNVERIFIED**. Niczego nie odblokowano, nie parowano i nie instalowano.

Lokalnie poza repo przygotowano `/home/piotrek/voxone-hil/chaos-soak-8h/runner_v2_gate.py`: **offline admission/coverage gate**, jeszcze nie sterownik live. `--dry-run` PASS: RADIO/WS mogą przejść, BT/TTS/MQTT/mixed bez adapterów dostają natychmiast BLOCKED i **0 s stress**, zaś 2 komendy w 90 min dają PARTIAL i 0 s stress. `--audit-legacy evidence/` odtwarza 170+27 i blokady. Nie ma ścieżki live bez osobno sprawdzonych, autoryzowanych adapterów.

Następny przebieg powinien rozpocząć się od krótkiego preflightu metod, nie od kolejnych 8 h: potwierdzony A2DP Source, świadomie autoryzowane akcje HA/TTS i obserwacja MQTT, pasywny UART RX-only, jeden WS, READY/bootId/heap, zapis i przywrócenie początkowego source/stacji/volume/MUTE/balance. Brak adaptera blokuje daną fazę **przed uruchomieniem jej zegara**. Dopiero później ograniczona sesja z jawnie mierzonymi fazami RADIO, BT, TTS, WS, MQTT i mixed. Proponowane minima, sprawdzone offline: RADIO >=12 komend/15 min i >=0,6/min; BT pełny connect/PLAY/STOP/disconnect/reconnect; TTS żądanie+cleanup+restore; WS >=3 reconnecty i 3 readbacki; MQTT trzy retained i jedna live update; mixed >=2 przejścia cross-source/15 min. To **propozycja następnej próby**, nie retroaktywny PASS. STOP: panic/assert/WDT, zmiana bootId, utrata READY/WWW, niespójny restore, brak autoryzacji albo nieosiągalna intensywność. Należy oddzielnie raportować czas obserwacji i kwalifikowanego stress, mierzyć rzeczywisty WS first-response, a po zakończeniu porównać live-tail z pełnym UART.

Bramki Stable/RC pozostają bez zmian: PASS wcześniejszych osobnych testów RADIO/BT/TTS/MQTT nie przenosi się na ten chaos soak. AH.FM i AUDIO nadal wymagają odsłuchu lub obiektywnego pomiaru; **nie potwierdzono nowego defektu firmware**.
