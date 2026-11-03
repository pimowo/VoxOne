# VOXONE-L0-AHFM-REMOTE-HIL-2 — zdalny HIL AH.FM

Data: 2026-11-01, 22:38–22:49 CET. Fizyczny L0: `bf1d147`, profil L0. Dowody: `~/voxone-hil/logs/l0/VOXONE-L0-AHFM-REMOTE-HIL-2-20261101T223813+0100/`.

## Wynik i zakres

**Werdykt całego zadania: PARTIAL.** Potwierdzono stabilność logiczną urządzenia i brak zaobserwowanych błędów transportu w ograniczonej instrumentacji; nie potwierdzono ciągłości słyszalnego audio. Proceduralne odstępstwo: mimo że AH.FM grał już w preflight, wysłano zbędną komendę `playstation=1`, która ponownie uruchomiła strumień przed właściwym oknem 600 s.

**Stabilność urządzenia: PHYSICAL LOGICAL HIL PASS (600,04 s).** 120/120 próbek `/api/ready` miało `ready=true` i niezmienny `bootId=ca5d5657`. WS w 120/120 próbek raportował stację 1 Afterhours (`https://nl.ah.fm/live`), `RADIO/PLAY/playing`, MP3 i 192 kb/s. Logger FT232R L0_LOG_RX zakończył się naturalnie po 650 s ze statusem integralności `PASS`, 3121/3121 B zapisanych i bez zmiany bootId. Nie znaleziono markerów panic/assert/WDT. Końcowo `app0 VALID`, WWW `READY/verified/compatible`.

**Transport i dekoder: PARTIAL / brak zaobserwowanej anomalii.** W UART nie ma raportowanych błędów HTTP/TLS/chunked/ICY/MP3, underflow ani spontanicznego reconnectu w oknie pomiaru. Co minutę UART raportował `playerRunning=1`, Wi-Fi=1 oraz `audioBuffer` 282989–283615 B; żadna próbka nie była bliska zeru. Dziewięć pełnych minutowych okien po początkowym przełączeniu miało `loopMaxUs` 61758–101571. Pierwsze okno miało 1510321 µs i obejmowało kontrolowany reconnect strumienia po komendzie testowej — nie jest dowodem spontanicznego zacięcia. Odczyty co minutę i brak szczegółowych liczników TLS/dekodera nie pozwalają wykluczyć krótkiego underflow między próbkami.

**Słyszalna ciągłość audio: UNVERIFIED.** Bez odsłuchu ani pomiaru sygnału I2S/analogowego nie da się potwierdzić lub zanegować zgłoszonych zacięć. Przyczyna pozostaje **UNVERIFIED**; brak podstaw do zmiany firmware lub progów bufora.

## Przebieg i stan

Preflight zastał już AH.FM na stacji 1, `RADIO/PLAY/playing`, `volume=7`, `volume100=12`, MUTE OFF, balance 0. Playlisty nie zmieniano. Po uzbrojeniu loggera wysłano `playstation=1`; było to zbędne, ponieważ AH.FM już grał, i wywołało widoczny w WS/UART krótki STOP/łączenie/PLAY około 22:38:26–27. Okno 600,04 s rozpoczęto dopiero po powrocie PLAY, o 22:38:28. Brak kolejnych STOP w okresie obserwacji. Ponieważ początkowa stacja i PLAY były już takie same, nie było konieczności wysyłania komendy przywrócenia; końcowy readback potwierdził stację 1, PLAY, volume 7/12%, MUTE OFF i balance 0. SHA256 playlisty przed i po: `adeb7551a651b8d2bf86205b36538894f058fc1aff08a2f6a96967b9b3f79054`.

Próbki WS (co około 5 s): wolny heap 115732–117680 B, wolny PSRAM 7789135–7805431 B, RSSI −62…−58 dBm. Pole WS `heap` przy NRSSI miało stale 0, lecz w tym kodzie jest procentem napełnienia bufora zależnym od `runtime.audioInfo`, nie wolnym heap; nie użyto go jako pomiaru pamięci ani bufora. `sampleRate=0` w `webStatus` nie jest dowodem zatrzymania dekodera. Rzeczywisty `audioBuffer` jest dostępny tylko w minutowych liniach ENDURANCE UART. Łącznie zachowano 596 ramek WS, w tym 132 `webStatus`.

## Integralność i porównanie

`SHA256SUMS` zweryfikowano: 10/10 plików `OK`. Kluczowe SHA256: `uart/uart.raw` `df75f8937deb5a1a14b27b8526f5a100ee8b828e7db89e997c7dee7f8db1e281`; `uart/manifest.json` `e70a6d94dc59d9b1c2528071000a0059cacd40ae351b7b547e6a8287b12a478f`; `samples.jsonl` `b1f367b7f3071d5702e3106e43bcc5ae7269c7d5b6e8912a074c0f39897aaaa0`; `ws.jsonl` `f812d86fd0f06e1bd2b474d672e87af7721d555fbf06d7fe255a169cbbd0457a`.

Wcześniejszy `VOXONE-RADIO-AHFM-OFFLINE-REGRESSION-1` potwierdził na DEV-VM HTTP/1.1 MP3 192 kb/s, chunked, ICY `metaint=16384` i poprawny deterministyczny replay parsera z ASan/UBSan. Niniejszy HIL uzupełnia go o metryki fizycznego ESP32, ale nie jest dowodem płynnego audio. Porównanie z bazowym yoRadio nadal nie wskazuje potwierdzonej regresji w parserze; rzeczywiste zacięcia pozostają do oceny przy odsłuchu lub obiektywnym pomiarze wyjścia audio.

TODO: zaktualizowano wyłącznie istniejący wpis AH.FM o obiektywny wynik tego HIL; AUDIO i przyczyna pozostają otwarte.
