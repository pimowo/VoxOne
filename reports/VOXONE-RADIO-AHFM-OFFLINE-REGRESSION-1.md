# VOXONE-RADIO-AHFM-OFFLINE-REGRESSION-1

Data: 2026-11-01. Zakres: wyłącznie DEV-VM i kod, bez kontaktu z L0.
Werdykt: **UNVERIFIED** — nie znaleziono potwierdzonego defektu VoxOne
wyjaśniającego słyszalne zacięcia AH.FM. Nie ma AUDIO PHYSICAL PASS.

## Tożsamość i rzeczywisty transport

Fizyczny kandydat L0 raportuje `bf1d147`; bieżący HEAD podczas analizy to
`f33408ed5396932d4dc55134b77b8fa7e6a7a4d4`. `git diff bf1d147..HEAD`
nie wykazał zmian w `Audio.cpp`, `AudioEx.h`, dekoderze MP3, `player.cpp`,
`main.cpp`, `config.cpp` ani wskazanych politykach RADIO. To porównanie źródeł,
nie bitowa atestacja fizycznego obrazu.

Krótki GET `https://nl.ah.fm/live` z DEV-VM, wymuszony jako HTTP/1.1 z
`Icy-MetaData:1` i UA zgodnym z żądaniem `Audio::connectToHost`, zwrócił
bez przekierowania `200 OK`, `Content-Type: audio/mpeg`,
`Transfer-Encoding: chunked`, `icy-br: 192`, `icy-sr: 44100` i
`icy-metaint: 16384`. TLS zestawił się z `37.59.107.240`; bajty MP3 zaczynają
się od nagłówka `FF FB B0` (MPEG-1 Layer III, 192 kb/s, 44,1 kHz). Osobne
żądanie HTTP/2 nie miało chunked ani metaint, dlatego nie służyło do replayu.
Ośmiosekundowe ograniczenie `curl` zakończyło *zapis próbki*, a nie wykazało
zerwania strumienia.

Lokalne artefakty, nieprzeznaczone do commita (zawierają krótki fragment
strumienia audio):

| Plik w `~/voxone-hil/diagnostics/VOXONE-RADIO-AHFM-OFFLINE-REGRESSION-1/` | Rozmiar | SHA-256 |
|---|---:|---|
| `headers.txt` | 747 B | `d3419c44921004fcde856784b53af1c1b8e98e08a051e4468e44fd588f55cee8` |
| `stream.raw` | 443233 B | `3ee704a6f89df581887faae92ff1567e231351d251118dc8762b9def5c106857` |
| `timing.trace` | 529623 B | `f4cccecdaf498bb905f8306da3bd9c73649bb3002eef4c8c231a8be6e3bcf1fd` |

W 7,685 s od pierwszego do ostatniego odbioru `curl` zarejestrował 147
porcji danych po 6–8814 B (mediana 1885 B); odstępy wyniosły 0,187–459,022
ms (mediana 10,526 ms, p95 163,383 ms). To odstępy na DEV-VM, **nie**
odczyty `WiFiClientSecure` ani opóźnienia pętli ESP32. Surowe body zawiera
97 pełnych chunków HTTP, po odjęciu framingu 442555 B ICY/MP3. Po odjęciu
27 bloków ICY otrzymano dokładnie 442368 B MP3, SHA-256
`e5f352f2c52bb35fdebcdd3539bc5fa19af93f2c49f2f0e23d616e15e27ea67f`.
W tej konkretnej próbce serwer umieścił
metadane w odrębnych chunkach; żadna metadata nie przecina granicy chunku
ani porcji TLS. Nie należy przypisywać temu strumieniowi fragmentacji ICY,
której próbka nie zawiera.

## Ścieżka kodu i porównanie z yoRadio 0.9.720

`Audio::connectToHost` wybiera `WiFiClientSecure` dla HTTPS i wysyła GET
HTTP/1.1 z `Icy-MetaData:1`; `parseHttpResponseHeader` rozpoznaje `audio/mpeg`,
chunked, `icy-br` i `icy-metaint`. `processWebStream` pobiera najwyżej
`available()`, dostępne miejsce bufora, pozostały payload bieżącego chunku
oraz pozostałe bajty audio do metadata. `chunkedDataTransfer` odcina framing,
`readMetadata` odcina ICY, a `playAudioData` przekazuje ramki do wspólnego
dekodera MP3 i `playSample`/I2S. L0 ma bufor PSRAM nominalnie 300000 B;
start odtwarzania następuje po przekroczeniu `maxFrameSize` (domyślnie 1600
B), nie po wypełnieniu bufora. `player.loop()` jest wykonywany z głównej
pętli razem z obsługą WWW/BT/sterowania; `loopMaxUs` jest raportowane co
60 s, ale ten offline replay nie uruchamia harmonogramu FreeRTOS.

Bazowy `reference/yoradio-main/yoRadio` ma bajtowo identyczne pliki dekodera
MP3, tę samą zasadę progu startu, to samo `playAudioData` i wywołanie
`i2s_write(..., 100)`. VoxOne zastąpił prosty parser rozmiaru chunku parserem
stanowym z walidacją CRLF/extension/trailer; dodał 10-sekundową politykę
martwego strumienia i dodatkowe obowiązki pętli. Te różnice są miejscami do
pomiaru na urządzeniu, ale nie stanowią samodzielnego dowodu regresji.

## Deterministyczny replay i wyniki

`tools/ahfm_transport_replay.py` odczytuje zachowane `stream.raw` i
`timing.trace`, przypisuje każdemu bajtowi rzeczywisty czas udostępnienia,
wycina z aktualnego produkcyjnego `Audio.cpp` dokładne implementacje
`readClient`, `readMetadata`, `chunkedDataTransfer` i `processWebStream`,
a następnie uruchamia je w istniejącym hostowym adapterze
`test/radio_transport_native.py`. Niezależny parser wyznacza oczekiwany
strumień MP3 i porównuje pełny rozmiar oraz hash. Replay pomija jedynie dwa
końcowe bajty `CRLF`: próbka została sztucznie ucięta dokładnie po chunku,
więc bez następnego nagłówka produkcyjny parser prawidłowo czekałby 2 s i
zgłosił timeout *końca próbki*, nie błąd strumienia. Żadnego bajtu audio nie
pominięto. Uruchomienie:

```sh
python3 tools/ahfm_transport_replay.py \
  ~/voxone-hil/diagnostics/VOXONE-RADIO-AHFM-OFFLINE-REGRESSION-1/stream.raw \
  ~/voxone-hil/diagnostics/VOXONE-RADIO-AHFM-OFFLINE-REGRESSION-1/timing.trace
python3 tools/ahfm_transport_replay.py --sanitize \
  ~/voxone-hil/diagnostics/VOXONE-RADIO-AHFM-OFFLINE-REGRESSION-1/stream.raw \
  ~/voxone-hil/diagnostics/VOXONE-RADIO-AHFM-OFFLINE-REGRESSION-1/timing.trace
```

Zwykły i ASan/UBSan replay: **PASS**, 443231/443231 bajtów skończonego
replayu zużytych, 442368/442368 bajtów MP3 identycznych z niezależnym
wynikiem, `stopCalls=0`, symulowany koniec 7688 ms. Syntetyczne przypadki
istniejącego `python3 test/radio_transport_native.py --strict`: **16/16 PASS**,
w tym częściowe odczyty, split nagłówka chunku, metadata ICY przecinające
chunk, poprawne wyłączenie framingu z audio i timeout. Nie dodano duplikatu
tych przypadków.

Replay wyklucza w tej próbce utratę/sklejenie bajtów w testowanych funkcjach
chunked/ICY. **Nie mierzy** rzeczywistego rozmiaru odczytów TLS na ESP32,
prędkości dekodera, drenażu bufora, `loopMaxUs`, czasu blokowania I2S,
wpływu BT/WWW/DSP ani słyszalnych dropoutów. W DEV-VM dostarczono około
18,4 s audio 192 kb/s w 7,7 s, lecz nie dowodzi to braku niedopełnienia
bufora na L0. Bez odsłuchu i fizycznego monitoringu `audioBuffer`, UART i
`loopMaxUs` nie można ustalić przyczyny zgłaszanego przycinania.

Nie znaleziono defektu uzasadniającego zmianę firmware, progu buforowania
lub architektury audio. `tools/verify.ps1` nie uruchamiano, bo nie było
poprawki produkcyjnej ani nowego testu regresyjnego defektu. TODO zachowuje
otwartą fizyczną kwalifikację słyszalnych przerw; dodano do niej wyłącznie
ten ograniczony wynik offline, bez zmiany bramek Stable.
