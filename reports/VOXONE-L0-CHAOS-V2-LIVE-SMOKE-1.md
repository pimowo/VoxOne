# VOXONE-L0-CHAOS-V2-LIVE-SMOKE-1

Data: 2026-11-02, Europe/Warsaw. Wynik: **ograniczony L0 PHYSICAL HIL SMOKE PASS** dla RADIO/WS/volume/MUTE i mechanizmu restore. **Pełny CHAOS pozostaje PARTIAL**: BT, TTS, MQTT i faza mieszana nie były wykonywane; fizyczna ciągłość dźwięku nie była oceniana. Nie wykonano OTA, flash, resetu, zmian SECURITY, playlisty ani firmware.

## Zakres i preflight

`tools/chaos_v2.py --live --smoke --confirm-live --safe-volume-confirmed --duration 900`, seed `16328847690973426075`. Tryb smoke odrzuca adaptery BT/TTS/MQTT. Przed LIVE: 13/13 testów offline PASS, dry-run PASS, `py_compile` PASS, `git diff --check` PASS. Na DEV-VM i `voxone-bridge` nie wykryto równoległego procesu HIL/loggera; FT232R `A7039N0Z` był wolny. Runner trzymał wyłączny lock v1/v2 i jednego klienta WS do sterowania, a pasywna telemetria używała odrębnego HTTP i UART RX-only. Test STOP/cleanup został wcześniej zweryfikowany offline.

Stan wejściowy 08:13:40: `bf1d147`, profil L0, RADIO/PLAY/playing, stacja 1 Afterhours (`https://nl.ah.fm/live`), volume raw 7 / volume100 12, MUTE OFF, balance 0, `bootId=ca5d5657`, `app0/app0 VALID`, WWW READY/verified/compatible. Free heap 117720 B, PSRAM free 7805431 B, RSSI −61 dBm. SHA256 playlisty `adeb7551a651b8d2bf86205b36538894f058fc1aff08a2f6a96967b9b3f79054`; backupu Config `2095191838176c0127374a6866a6904f9e0623a3d384b5ebe6055d0ec84c5389`; SECURITY OPEN. Logger L0_LOG_RX został uzbrojony 08:13:42.536, **przed pierwszą komendą**.

## Przebieg i pokrycie

Próba trwała od 08:13:39.297 do 08:27:15.745, czyli **816,448 s (13 min 36,448 s)**, poniżej limitu 900 s. Aktywne okno wyniosło 806,756 s; 90 s rezerwy było dostępne na bezpieczne zakończenie. Trzy cykle wykonały 27/27 zaplanowanych scenariuszy: po trzy razy każdy z dziewięciu typów (siedem pierwotnych RADIO/WS oraz dwa dodatkowe volume/MUTE). Kolejność każdego cyklu jest zapisana z seedem w `events.jsonl`; wszystkie scenariusze i komendy mają chronologię w `operations.jsonl`.

| Typ | Wykonania | Odczyt po zmianie |
|---|---:|---|
| RADIO STOP→PLAY | 3 | stan `stopped` → `playing` |
| Zmiana stacji | 3 | numer stacji i PLAY |
| Idempotentne PLAY | 3 | PLAY i zachowana stacja |
| Dwie szybkie, ograniczone zmiany stacji | 3 | obie stacje i PLAY |
| Normalne odtwarzanie 30 s | 3 | zachowany PLAY |
| Zmiana stacji z równoległą telemetrią WS/HTTP | 3 | stacja, PLAY, READY |
| WS disconnect/reconnect | 3 | nowy pełny GETINDEX/GETSYSTEM/GETWEBSTATUS |
| Volume100 12→6→12 | 3 | każda wartość potwierdzona |
| MUTE OFF→ON→OFF | 3 | każdy stan potwierdzony |

Aktywne interakcje: **63** = 33 komendy zmieniające stan + 27 dodatkowych odczytów `/api/ready` po scenariuszach + 3 kontrolowane reconnecty WS. Nie liczymy w tym automatycznych, licznych snapshotów WS. Dwie dalsze komendy (`playstation=1`, `start=1`) należały wyłącznie do końcowego restore, więc łącznie wysłano **35 komend**. W fazie aktywnej: STOP 3, PLAY 6, MUTE 6, volume 6, playstation 12. **Nieudane komendy/scenariusze: 0/0.** Minimum odstępu między aktywnymi komendami 8,213 s; maksimum 4 komendy w ruchomym oknie 60 s. Dwie komendy restore nie są objęte normalnym pacingiem, aby możliwie szybko odzyskać stan wejściowy. Mediana czasu do zgodnego readbacku aktywnej komendy 798,21 ms, maksimum 1797,84 ms. Pełne ramki WS, odpowiedzi i latency zachowano w dowodach.

## Stabilność i stan końcowy

401 próbek niezależnego HTTP READY, **0 błędów**, wyłącznie `bootId=ca5d5657`. Free heap z readbacków WS: 117720 B na wejściu, minimum 101520 B, końcowo 117600 B; PSRAM free 7805431 B, minimum 7739855 B, końcowo 7800775 B; RSSI od −63 do −60 dBm. Nie było osiągnięcia progu STOP, trendu trwałego wycieku w tym krótkim oknie, panic/assert/WDT, restartu ani utraty READY. Te dane nie są pomiarem fizycznej ciągłości audio.

Końcowy WS readback 08:27:13 potwierdził `source=radio`, stację 1 Afterhours z poprzednimi metadanymi, RADIO/PLAY/playing, raw volume 7, volume100 12, MUTE OFF i balance 0. Osobny read-only postcheck po zamknięciu runnera potwierdził ten sam stan. Końcowy `/api/ready` miał READY, `app0 VALID`, WWW READY/verified/compatible i niezmieniony `bootId`. SHA256 playlisty i backupu Config pozostały identyczne; SECURITY pozostało OPEN.

Pasywny UART ma **50738 B**, 841 chunków, 0 nieutrwalonych bajtów, pełną zgodność rekonstrukcji chunków i brak markerów panic/assert/WDT/heap corruption oraz błędów audio w sprawdzonym logu. Manifest loggera ma `status=INTERRUPTED` z powodu **celowego SIGINT po zakończeniu smoke**; oznacza to weryfikowaną integralność zebranego odcinka, **nie** PASS 960 s żądanego czasu loggera. Nie było nieoczekiwanego boot evidence; bootId przed/po jest stały.

## Dowody i ograniczenie wyniku

Katalog: `/home/piotrek/voxone-hil/chaos-v2/runs/20261102T081339-fe76d4e0/`. `sha256sum -c SHA256SUMS`: **9/9 OK** (RESULT, events, operations, telemetry, WS, uart-live, surowy UART, chunks, manifest). SHA256 surowego UART `1d3e8b1fd47e7bbda9043aabcc3687597a47b3f3b16569f1a05a228f9addffdb`; operacji `e17cb434dd577b00ba119a4aa8fba0ede5fa4637000c871074307f9ce9470016`. Wynik maszyny `SMOKE_PASS` dotyczy tylko powyższego zakresu i nie zamyka żadnej pełnej bramki CHAOS ani niedostępnych BT/TTS/MQTT. Brak odsłuchu lub pomiaru sygnału oznacza **AUDIO UNVERIFIED**.

`docs/TODO.md` zachowuje v1 PARTIAL i wszystkie release gates; dopisano jedynie fizyczny PASS ograniczonego smoke v2 oraz pozostawiono niewykonane fazy otwarte. Bez commit i push.
