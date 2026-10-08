# VoxOne — kanoniczny backlog

> To jest jedyna lista otwartych prac VoxOne. Dokumenty sprzętowe, baseline'y i inventory opisują stan lub historię, ale nie są roadmapą. Po ukończeniu i właściwej weryfikacji zadanie znika z tego pliku; nie prowadzimy sekcji DONE.
>
> Klasyfikacja: `[BUG]` — znany problem, `[STABLE]` — wymagane do wiarygodnego stable, `[DECISION]` — potrzebna jawna decyzja, `[POST-STABLE]` — praca po pierwszym stable, `[HARDWARE]` — projekt lub test sprzętowy.

## Stable hardening

Cleanup przed stable jest zakończony. Bazą diagnostyczną testów długotrwałych jest checkpoint `voxone-stable-endurance-diag-1`.

### RADIO

- [STABLE] Wykonać na fizycznym SALON minimum czterogodzinny endurance RADIO: jedna stabilna stacja przez co najmniej 2 h, minimum trzy zmiany stacji i dalsze granie do minimum 4 h.
- [STABLE] Zebrać log z uptime, heap/minimum heap/largest block, PSRAM/minimum PSRAM, stack HWM głównej pętli i DisplayTask, `loopMaxUs`, `audioBuffer`, reconnectami, Wi-Fi oraz RSSI; ocenić trend pamięci, stack, blokady pętli, watchdog/reboot i serie reconnectów.
- [STABLE] Podczas endurance potwierdzić brak trwałych stall/dropout, poprawne ponowne uruchamianie streamu oraz ciągłą reakcję LCD i VU.
- [STABLE] Przetestować realne strumienie MP3, AAC/AAC+ i FLAC oraz SHOUTcast/Icecast, `ICY 200`, MIME `audio/aacp`, HTTP/HTTPS, metadata/brak metadata, reconnect i dead stream.
- [STABLE] Przetestować playlisty M3U/PLS, redirect, final URL, limit rekurencji i ochronę przed pętlą.
- [BUG] Jeśli na realnym streamie nadal wystąpi `Truncated MP3 frame`, przeanalizować ICY stripping, buforowanie, MP3 sync/frame length i Helix przed zmianą dekodowania.

### Bluetooth i VoxOneBT

- [STABLE] Fizycznie przetestować telefon i LG TV: connect, disconnect, reconnect, wielokrotne cykle połączenia, PLAY/PAUSE, volume, metadata, VU oraz wcześniejszy HCI allocation assert.
- [STABLE] Potwierdzić, że A2DP audio state steruje I2S, AVRCP pozostaje transportem/UI, a wybór lub fallback źródła nie wysyła przypadkowego PLAY/PAUSE/NEXT/PREV.
- [STABLE] Sprawdzić BT VU przy PLAY, PAUSE, STOP i disconnect oraz synchronizację VU ON/OFF między klientami WWW i po restarcie.

### Source Manager i TTS restore

- [STABLE] Wykonać fizyczną macierz RADIO ↔ BT: wybór ręczny, nowy BT connect edge, disconnect, offline, utrata runtime modułu i ponowne połączenie.
- [STABLE] Potwierdzić kontrakt normalnego fallbacku: RADIO PLAY intent wraca do RADIO PLAY, a RADIO STOP intent pozostaje RADIO STOP.
- [STABLE] Potwierdzić wyjątek TTS: utrata BT podczas aktywnego komunikatu nie przerywa TTS, po końcu wybiera RADIO fizycznie STOP i nie uruchamia autoplay; blokada restore jest jednorazowa, a radio intent pozostaje zachowany.
- [STABLE] Potwierdzić manual source priority: istniejące połączenie BT nie cofa ręcznie wybranego RADIO bez nowego connection edge.
- [STABLE] Przy utracie VoxOneBT sprawdzić logical XSMT, MUTE i routing audio; dla przyszłego profilu bez RADIO określić bezpieczny STOP/no-source po utracie wszystkich dostępnych źródeł.

### Kanały L/R i VU

- [STABLE] Odtworzyć kontrolowany materiał: 0–5 s LEFT 1 kHz, 5–7 s cisza, 7–12 s RIGHT 1 kHz, 12–14 s cisza, 14–19 s LEFT+RIGHT; sprawdzić fizyczne kanały i odpowiadające im VU.

### Update, recovery i persistence

- [STABLE] Fizycznie sprawdzić Web Update firmware MAIN i SPIFFS, rzeczywisty postęp, sukces, błąd, cancel/przerwanie, restart oraz brak samoczynnego wznowienia audio po błędzie.
- [STABLE] Sprawdzić backup/restore całej konfiguracji, walidację schematu, błąd lub nieudany backup oraz zachowanie config po firmware/SPIFFS update.
- [STABLE] Sprawdzić recovery AP, `/update.html` i `/emergency` przy niedostępnym SPIFFS, błędne dane Wi-Fi, Serial CLI oraz ekran AP na telefonie.
- [STABLE] Zweryfikować restart i Config v7: volume, MUTE, source intent, aktywną stację oraz rozdział ustawień runtime/persistent.
- [STABLE] Sprawdzić auto reload WWW: powrót do STATUS, timeout i czytelny komunikat, gdy urządzenie nie wróci.

### Targety i release gate

- [STABLE] Wykonać fizyczną regresję DESK, DIN i SALON; dla DIN sprawdzić PCM5102A GPIO1/2/3, VoxOneBT, WWW, MQTT/HA i NoDisplay.
- [STABLE] Na DESK sprawdzić ukrycie suwaka jasności, restart z WWW, powrót Wi-Fi bez utraty stacji/config oraz osobno ekran aktualizacji bez regresji ScrollWidget/HOLD.
- [HARDWARE] Przypisać GPIO XSMT PCM5102A na SALON i fizycznie sprawdzić LOW przy PAUZA/STOP, HIGH przy PLAY, ciszę podczas przejść i Web Update oraz czerwoną ramkę VOL bez zmiany semantyki MUTE.
- [STABLE] Fizycznie sprawdzić MQTT/Home Assistant na SALON; MQTT pozostaje wspierane w pierwszym stable.
- [STABLE] Uzupełnić dokumentację aktywnych profili, API, MQTT/HA, Source Managera, VoxOneBT oraz update/recovery; oznaczyć historyczne baseline'y i usunąć z dokumentów bieżącego stanu opisy sprzeczne z aktualnym runtime.
- [DECISION] Przed stable ustalić minimalny zakres uwierzytelniania, CSRF i ochrony mutujących REST/WebSocket oraz ekspozycji danych.

## Znane błędy i pomiary

- [BUG] Zdiagnozować sporadyczne pozostawienie lub powtórzenie tekstu pod „Utwór” na LCD; sprawdzić invalidate, clear, scroll oraz kolejność aktualizacji metadata.
- [BUG] Sprawdzić bezpieczeństwo bufora/okna i clipping w `ScrollWidget`, szczególnie przy długich metadata; powiązać wynik z błędem czyszczenia i nie zmieniać timingu bez testu rendererów.
- [HARDWARE] Wykonać kontrolowane porównanie poziomu RADIO i BT na tym samym materiale PCM. Użytkownik zmienił rezystory z 22 Ω na 41 Ω, ale trzeba nadal zmierzyć digital gain, BT Absolute Volume, VoxOneBT PCM, MAIN RX i poziom wyjścia.
- [BUG] Zbadać pstryknięcie lub glitch audio podczas mutacji playlisty/stacji.

## Audio, RADIO i kodeki

- [STABLE] Utrzymywać MP3, AAC/AAC+, FLAC oraz M4A wyłącznie jako kontener AAC; dokończyć detekcję AAC/AAC+ i `audio/aacp`, aby danych AAC nie kierować do dekodera MP3.
- [STABLE] Na realnych stacjach sprawdzić AAC+, w tym RMF DLA DZIECI.
- [STABLE] Dokończyć obsługę M3U, PLS, redirect i final URL streamu z limitem rekurencji i ochroną przed pętlą.
- [DECISION] Po stable ocenić usunięcie WAV, OGG/Vorbis remnants i Opus enum/remnants oraz potrzebę ASX, M3U8, HLS i TS na podstawie realnych consumerów. Nie traktować ich jako nowych planowanych funkcji.
- [DECISION] Ustalić źródło wiarygodnego kodeka i bitrate A2DP; publikować je dopiero, gdy VoxOneBT lub A2DP udostępni realne dane.

## TTS / PLAY_MEDIA

- [POST-STABLE] Wprowadzić tryby TTS Volume `CURRENT`, `FIXED` i `AUTO`. `CURRENT` używa normalnego `userVolume`; `FIXED` ma własne 0–100 bez zmiany `userVolume`; `AUTO` wybiera wartość na początku komunikatu na podstawie rzeczywistego base playback i utrzymuje ją do końca TTS.
- [POST-STABLE] Zachować Max Volume jako hard clamp i nie wysyłać BT Absolute Volume podczas TTS.
- [STABLE] Fizycznie przetestować restore RADIO PLAY/STOP oraz BT PLAY/PAUSE/STOP, ownership I2S0, sample rate i routing po normalnym końcu, błędzie, przerwaniu i timeout.
- [POST-STABLE] Na LCD pokazywać dla TTS stację „KOMUNIKAT TTS”, artystę „Powiadomienie głosowe”, pusty utwór i poprawną etykietę źródła.
- [POST-STABLE] Dokończyć deterministyczny cleanup TTS po błędzie/timeout; dodać HTTPS i redirect tylko jeśli wymagają tego używane endpointy.
- [POST-STABLE] Rozszerzyć temporary audio restore na przyszłe DLNA i AUX bez zmiany pierwszeństwa ręcznego wyboru źródła.

## Source control i VoxOneBT

- [DECISION] Ustalić zachowanie po ręcznym wyborze BT bez podłączonego telefonu: czy i na jak długo otwierać pairing/discoverability, zachowując manual source priority.
- [DECISION] Ustalić model parowania: PIN/passkey wymagany lub nie, stały albo konfigurowalny, preferred peer, pairing window, reconnect, discoverability i bezpieczeństwo ponownego parowania.
- [POST-STABLE] Zachować sterowanie enkoderem: RADIO — obrót Volume, klik PLAY/STOP, dwuklik następne dostępne źródło, przytrzymanie lista stacji; BT — obrót Volume, klik PLAY/PAUSE, dwuklik następne źródło, przytrzymanie transport BT; trójklik bez akcji poza źródłami, które jawnie go wykorzystują.
- [POST-STABLE] Rozstrzygać klik/dwuklik/trójklik po wspólnym krótkim oknie, aby trójklik nie wykonywał wcześniej dwukliku.
- [POST-STABLE] W VoxOneBT utrzymać A2DP audio state jako sterowanie I2S, a AVRCP wyłącznie jako transport/UI; uzupełnić status PLAY/PAUSE/STOP, reconnect, preferred peer, pairing window, discoverability, metadata, ograniczenia TV i realny codec/bitrate, jeśli stos udostępnia dane.
- [POST-STABLE] Znormalizować poziom BT względem RADIO dopiero po pomiarach toru cyfrowego i analogowego.
- [POST-STABLE] Zaprojektować aktualizację VoxOneBT: MAIN WWW → UART → VoxOneBT, walidacja rozmiaru i CRC, ACK/NACK, progress, bezpieczny slot OTA, restart tylko VoxOneBT i sprawdzenie `FW_VERSION` po restarcie. VoxOneBT pozostaje osobnym repozytorium.

## DLNA

- [POST-STABLE] Dodać DLNA jako capability-filtered źródło SourceManager. Początkowy cykl po implementacji: RADIO → BT → DLNA → RADIO, z pomijaniem źródeł niedostępnych według capabilities i runtime availability.
- [POST-STABLE] Zaimplementować konfigurację IP serwera, discovery/scanning, ContentDirectory, browsing, pagination, wybór zasobu/play URL, next track i odtwarzanie folderu.
- [POST-STABLE] Zachować UX PLAYER: klik PLAY/PAUSE, dwuklik następne źródło, trójklik ALL/RND/ONE, przytrzymanie biblioteka.
- [POST-STABLE] W przeglądarce folderów: obrót wybiera, klik wchodzi/odtwarza, „ODTWÓRZ FOLDER” zaczyna od pierwszego utworu, dwuklik wraca poziom wyżej, przytrzymanie wraca do PLAYER, a timeout około 15 s wraca do PLAYER.
- [POST-STABLE] ALL/FOLDER odtwarza folder kolejno w pętli; RND odtwarza każdy utwór raz i tasuje ponownie; ONE zapętla bieżący utwór.
- [POST-STABLE] Zintegrować DLNA z metadata, VU z PCM, SourceManager, WebSocket, WWW, LCD i natywnym HA.

## AUX

- [HARDWARE] Zaprojektować wejście AUX na PCM1808 dla analogowego RCA i ewentualnego źródła wewnętrznego oraz potwierdzić piny, zegary i format I2S input.
- [POST-STABLE] Zintegrować AUX z SourceManager, VU z PCM, WWW, LCD i HA; zamiast bitrate pokazywać sample rate/format PCM oraz właściwy status źródła bez metadata.

## WWW, API i WebSocket

- [POST-STABLE] Sprawdzić kompletność metadata wszystkich aktywnych źródeł w WWW; obecne metadata BT traktować jako działający baseline.
- [POST-STABLE] Dodać source selector do PLAYER/STATUS WWW, pokazujący wyłącznie źródła dostępne według capabilities i runtime availability.
- [POST-STABLE] Dodać MUTE przy sterowaniu głośnością WWW, używając jednego wspólnego stanu MUTE; ujednolicić później WWW, LCD i HA.
- [POST-STABLE] Ujednolicić WebSocket state dla RADIO, BT oraz przyszłych DLNA/AUX: aktywne źródło, transport/playback, metadata źródła, codec/format, sample rate i bitrate tam, gdzie mają znaczenie.
- [POST-STABLE] Dokończyć edycję maksymalnie pięciu profili Wi-Fi: priority/last-known-good, nowe hasło, zachowanie lub wyczyszczenie hasła, walidacja, atomowy zapis i kontrolowany restart.
- [POST-STABLE] Dodać konfigurację restartu, sleep/screensaver, auto standby, backup/restore config, playlist import/export, Radio Directory i recovery bez przywracania starego WWW.
- [POST-STABLE] Pokazać spójną identyfikację builda i dane systemowe w SYSTEM oraz AKTUALIZACJA.
- [POST-STABLE] Dodać Web Update VoxOneBT jako osobny, jawny proces po stabilizacji aktualizacji MAIN.
- [DECISION] Rozstrzygnąć, czy lokalny WebSocket ma walidować żądany subprotocol zamiast bezwarunkowo go odsyłać; połączyć decyzję z audytem auth/CSRF.

## Local UI / LCD

- [POST-STABLE] Zaprojektować pełną konfigurację urządzenia z LCD 480×320 i jednym enkoderem tak, aby po jednorazowym flashu urządzenie działało samodzielnie bez WWW; WWW i LCD mają używać wspólnego modelu konfiguracji.
- [POST-STABLE] Przebudować PLAYER: podnieść PLAY/PAUSE/STOP oraz bitrate/audio info, a niżej dodać czytelną ramkę faktycznego trybu wyjścia 2.0/2.1/2.2 pochodzącego z konfiguracji audio/DSP.
- [POST-STABLE] Ustalić wspólną lub jawnie przypisaną szybkość przewijania dla stacji, artysty, utworu i list; usunąć przypadkowo różne timingi rendererów.
- [POST-STABLE] Dodać ekran aktualizacji „AKTUALIZACJA” z rzeczywistym postępem, sukcesem, błędem i restartem dla MAIN, a później dla VoxOneBT.
- [POST-STABLE] Dodać ikonę/stan MUTE na DESK i ujednolicić go z SALON.
- [POST-STABLE] Dodać source-aware PLAYER, ekran TTS, przyszłą przeglądarkę DLNA oraz konfigurację DSP.
- [POST-STABLE] Przygotować wspólne `assets/branding` jako źródło logo WWW, splash/logo LCD i favicon; później użyć tych samych materiałów w README/GitHub.
- [POST-STABLE] Rozważyć opcjonalną skórkę YAMAHA AMBER: czarne tło i jeden bursztynowy kolor, punkt startowy `#FF9A1F` / RGB565 `0xFCC3`.
- [POST-STABLE] Rozwijać klasy display: wspólne 128×64 dla SSD1306 i SH1106, osobne SSD1322 256×64, GC9A01 240×240, ST7789 320×240 i ST7796S 480×320.
- [DECISION] Po stable zdecydować o SSD1309 w klasie 128×64 oraz o dalszym utrzymaniu legacy ST7789 284×76.
- [POST-STABLE] Uporządkować duplikację `_charSize` w widgetach tylko przy pracy nad rendererem; nie robić osobnego refaktoru bez korzyści testowej.
- [HARDWARE] Ocenić opcjonalny czujnik światła dopiero po ustaleniu docelowych PCB i display.

## Power, startup i alarm

- [POST-STABLE] Zaprojektować włączanie/wyłączanie VoxOne z rozróżnieniem reboot, standby, audio mute i pełnego wyłączenia.
- [HARDWARE] Dodać sterowanie `AMP_POWER` z anti-pop: mute/fade przed wyłączeniem, opóźnienie po włączeniu i unmute/fade po stabilizacji.
- [POST-STABLE] Dodać auto standby/auto power-off po konfigurowanym czasie bez dźwięku, ustawienie czasu w WWW i możliwość OFF; zdefiniować aktywność osobno dla RADIO, BT, DLNA, AUX i TTS, tak aby komunikat TTS nie został zablokowany.
- [POST-STABLE] Skonsolidować configurable boot source, boot station, startup PLAY/STOP, LAST/FIXED startup volume, max physical volume i MUTE semantics. Zachować rozdział technicznego checkpointu od release version.
- [POST-STABLE] Dodać alarm: czas, dni, stacja, volume, ON/OFF oraz tryb „włącz radio” albo „graj przez X”.

## DSPmini

- [HARDWARE] Docelowy DSP to moduł klasy ADAU1401/ADAU1701/ADAU1702. Ustalić schemat DSPmini, cztery wyjścia i workflow inicjalizacji/programowania z ESP bez drogiego USBi.
- [POST-STABLE] Dodać live tuning/runtime parameters, storage presetów, PEQ, crossover, subwoofer, 2.0/2.1/2.2, role/mute wyjść, delay, Auto Loudness, protection, balance/fader i trwałość presetów.
- [POST-STABLE] Zintegrować DSPmini z WWW i LCD. W profilu DSP rozbudowane ustawienia mają należeć do AUDIO i nie mogą dublować globalnej głośności.
- [DECISION] Po przygotowaniu integracji sprzętowej zdecydować, które elementy obecnej zakładki/demo DSP i `DSP_CUSTOM` zachować, zastąpić lub usunąć.

## Home Assistant i MQTT

- [POST-STABLE] Zbudować natywną integrację HA jako jedno urządzenie z `media_player`, source list z capabilities, source-aware play/pause/stop/next/prev, metadata oraz announce/TTS.
- [POST-STABLE] Dodać minimalne entities: TTS Volume Mode, TTS Fixed Volume, Max Volume, Wi-Fi RSSI, uptime, restart, a później standby.
- [DECISION] Zweryfikować znaczenie `status.on = config.store.dspon` w obecnym `ha_yoradio` i nie zmieniać kontraktu MQTT bez fizycznej regresji.
- [POST-STABLE] MQTT usunąć dopiero w osobnym etapie po uruchomieniu i fizycznym potwierdzeniu natywnego HA.

## Network

- [POST-STABLE] Zachować maksymalnie pięć profili Wi-Fi, priority/last-known-good, tryb NORMAL bez wymuszonego AP, oczekiwanie RADIO na sieć, możliwość działania BT offline oraz jednoznaczne CONFIG/AP/recovery behavior.
- [HARDWARE] Dodać W5500 tylko na odpowiednich PCB i dedykowanej magistrali SPI; tryby AUTO/LAN/Wi-Fi, LAN preferred, Wi-Fi fallback i DHCP, a później opcjonalny static IP.
- [POST-STABLE] Wydzielić abstrakcję sieciową bez twardego założenia `WiFiClient`; RADIO i DLNA mają działać przez LAN lub Wi-Fi.
- [POST-STABLE] W WWW pokazywać aktywny interfejs/link/IP, a na LCD dla Ethernet używać małej ikony RJ45 zamiast słupków RSSI bez przesuwania stałego obszaru systemowego.

## Playlisty i stacje

- [POST-STABLE] Dopracować import/export yoRadio: preview, walidacja, raport błędnych rekordów i ewentualny import URL.
- [STABLE] Sprawdzić listy 50/100/250 stacji, power loss, brak miejsca SPIFFS, `current/lastStation`, usunięcie aktywnej stacji, reorder i zachowanie po restarcie.
- [STABLE] Zweryfikować eksport/import między DESK i SALON: ID, kolejność, OVOL, A↔T, odświeżenie WWW oraz backup/restore przez Web Update.
- [STABLE] Fizycznie przetestować Radio Directory na SALON i DESK: dodanie, odtwarzanie, restart, pamięć i brak zakłóceń audio.
- [POST-STABLE] Sprawdzić prezentację A↔T na LCD/WWW/MQTT oraz zachowanie po reorder i usunięciu stacji.

## Hardware targets

- [STABLE] Utrzymać DESK, DIN i SALON bez zmiany architektury do pierwszego stable; ESP32 legacy pozostaje stabilnym legacy targetem.
- [HARDWARE] Po stable zaprojektować A-family/MAX na ESP32-S3 N16R8, B-family/MINI na ESP32-S3 Zero oraz C-family/PORTABLE na ESP32-S3 Zero portable.
- [HARDWARE] Potwierdzić GPIO, rewizje PCB, opcjonalne LCD/BT/DSP oraz warianty wyjścia PCM5102A, MAX98357 portable i DSPmini.
- [POST-STABLE] Utrzymać zasadę jednego builda firmware na target PCB zamiast buildów dla każdej kombinacji opcji; `HardwareDescriptor` i capabilities są źródłem prawdy.

## Installer, first boot i release

- [POST-STABLE] Przygotować VoxOne Installer dla Windows jako portable EXE dla VoxOne/VoxOneBT: wybór PCB A1/B1/C1 lub V1/V2, embedded stable factory binaries, install, erase/install, auto COM, auto chip i brak ręcznych offsetów.
- [POST-STABLE] Zaprojektować first boot AP `VoxOne-XXXXXX` i captive wizard do Wi-Fi oraz podstawowego hardware/config; dalsza konfiguracja ma być dostępna przez WWW i LCD.
- [POST-STABLE] Docelowy przepływ produkcyjny ma po flashu nie wymagać VS Code ani edycji kodu.
- [STABLE] Przed release potwierdzić artefakty firmware/SPIFFS/full image, instrukcję instalacji i recovery oraz zasady zachowania NVS/config.
- [STABLE] Po przejściu wszystkich gate'ów przygotować stable release i merge `project-cleanup-1` → `main`; dopiero release otrzymuje decyzję o zmianie `VOXONE_VERSION`.

## Compatibility i małe decyzje techniczne

- [DECISION] Uporządkować compatibility API strefy czasowej: system używa stałej Europe/Warsaw, pola Config v7 pozostają zachowane, ale `getTimezoneOffset()` nie może pozostać nieopisanym `return 0`; ustalić kontrakt bez zmiany serialized layout.

## Kolejność

- P0 — STABLE HARDENING
- P1 — BUGFIXES znalezione podczas stable
- P2 — STABLE RELEASE + merge `project-cleanup-1` → `main`
- P3 — UX/polish niewymagający nowej architektury
- P4 — VoxOneBT hardening
- P5 — DLNA / AUX
- P6 — native HA
- P7 — A1/B1/C1 hardware architecture
- P8 — DSPmini
- P9 — Installer / full standalone LCD configuration
- P10 — W5500 / dalsze rozszerzenia
