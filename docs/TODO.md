# VoxOne — kanoniczny backlog

> To jest jedyna lista otwartych prac VoxOne. Dokumenty sprzętowe, baseline'y i inventory opisują stan lub historię, ale nie są roadmapą. Po ukończeniu i właściwej weryfikacji zadanie znika z tego pliku; nie prowadzimy sekcji DONE.
>
> Klasyfikacja: `[BUG]` — znany problem, `[STABLE]` — wymagane do wiarygodnego stable, `[DECISION]` — potrzebna jawna decyzja, `[POST-STABLE]` — praca po pierwszym stable, `[HARDWARE]` — projekt lub test sprzętowy.

## Stable hardening

### RADIO

- [STABLE] Wykonać na fizycznym A0 minimum czterogodzinny endurance RADIO: jedna stabilna stacja przez co najmniej 2 h, minimum trzy zmiany stacji i dalsze granie do minimum 4 h.
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

- [STABLE] Fizycznie sprawdzić Web Update firmware MAIN i SPIFFS: postęp zapisu flash, sukces dopiero po finalizacji obrazu, błąd, cancel/przerwanie, restart oraz brak samoczynnego wznowienia audio po błędzie.
- [POST-STABLE] UPDATE-CORE-1 — wprowadzić wspólny `UpdateProgress` i globalny `UPDATE_LOCK` dla targetów MAIN / FILESYSTEM / VOXONEBT, ze wspólnymi fazami aktualizacji; podczas właściwej aktualizacji zatrzymać audio i zbędne funkcje oraz przygotować wspólny ekran LCD/WWW.
- [POST-STABLE] Ujednolicić postęp MAIN, SPIFFS/WWW i VoxOneBT w jednym modelu `target`, `phase`, `totalBytes`, `writtenBytes`/`confirmedBytes`, opcjonalny `percent` i `result`/`error`; nie tworzyć osobnych systemów postępu.
- [POST-STABLE] Dla MAIN i SPIFFS stosować fazy PREPARE, WRITING, FINALIZING, SUCCESS, ERROR i RESTART. Procent liczyć z bajtów dopiero po udanym `Update.write`; 100% zapisu nie oznacza SUCCESS, który następuje po `Update.end`. Przy nieznanym rozmiarze firmware pokazywać „ZAPIS...”; SPIFFS wymaga znanego pełnego rozmiaru obrazu.
- [POST-STABLE] Przekazywać update progress ze współdzielonego stanu/backendu do DisplayTask; backend nie rysuje LCD. Ograniczyć publikacje do zmiany procentu i maksymalnie jednej na ok. 100–250 ms, z natychmiastowym przekazaniem faz PREPARE, FINALIZING, SUCCESS i ERROR.
- [POST-STABLE] Pokazać na A0 „AKTUALIZACJA”, pasek, procent i krótki status; na X0 wykorzystać istniejący ekran aktualizacji i pokazać procent albo „ZAPIS...”, bez przebudowy layoutu.
- [POST-STABLE] Udostępnić ten sam rzeczywisty stan zapisu MAIN/SPIFFS w WWW; obecny postęp HTTP uploadu pozostawić jako pomocniczy, wyraźnie odróżniony od postępu zapisu flash.
- [POST-STABLE] Zachować wspólny backend MAIN/SPIFFS dla `/update` i `/emergency`; progress awaryjnej aktualizacji nie może zależeć od assetów WWW ani zamontowanego SPIFFS.
- [POST-STABLE] Dodać testy modelu postępu MAIN: 0–100%, unknown total, finalizacja, sukces i błąd. Dla SPIFFS sprawdzić backup, unmount, wymagany pełny rozmiar, zapis, remount po błędzie oraz sukces/restart.
- [STABLE] Sprawdzić backup/restore całej konfiguracji, walidację schematu, błąd lub nieudany backup oraz zachowanie config po firmware/SPIFFS update.
- [STABLE] Sprawdzić recovery AP, `/update.html` i `/emergency` przy niedostępnym SPIFFS, błędne dane Wi-Fi, Serial CLI oraz ekran AP na telefonie.
- [STABLE] Zweryfikować restart i Config v7: volume, MUTE, source intent, aktywną stację oraz rozdział ustawień runtime/persistent.
- [STABLE] Sprawdzić auto reload WWW: powrót do STATUS, timeout i czytelny komunikat, gdy urządzenie nie wróci.

### Targety i release gate

- [STABLE] Wykonać fizyczną regresję X0, B0 i A0; dla B0 sprawdzić PCM5102A GPIO1/2/3, VoxOneBT, WWW, MQTT/HA i NoDisplay.
- [STABLE] Na X0 sprawdzić ukrycie suwaka jasności, restart z WWW, powrót Wi-Fi bez utraty stacji/config oraz osobno ekran aktualizacji bez regresji ScrollWidget/HOLD.
- [STABLE] Fizycznie zweryfikować aktualne WWW na A0 i X0 (gdy bezpieczna aktualizacja X0 będzie możliwa): MUTE, selector RADIO/BT, oznaczenie BT offline, manual source priority, reconnect/resnapshot, favicon bez 404 oraz build identity w SYSTEM i AKTUALIZACJA.
- [HARDWARE] Przypisać GPIO XSMT PCM5102A na A0 i fizycznie sprawdzić LOW przy PAUZA/STOP, HIGH przy PLAY, ciszę podczas przejść i Web Update oraz czerwoną ramkę VOL bez zmiany semantyki MUTE.
- [STABLE] Fizycznie sprawdzić MQTT/Home Assistant na A0; MQTT pozostaje wspierane w pierwszym stable.
- [STABLE] Uzupełnić dokumentację aktywnych profili, API, MQTT/HA, Source Managera, VoxOneBT oraz update/recovery; oznaczyć historyczne baseline'y i usunąć z dokumentów bieżącego stanu opisy sprzeczne z aktualnym runtime.
- [DECISION] Przed stable ustalić minimalny zakres uwierzytelniania, CSRF i ochrony mutujących REST/WebSocket oraz ekspozycji danych.

### Definition of Stable

- [STABLE] Zamknąć release gate: FULL VERIFY PASS; fizyczny X0, B0 i A0 PASS; RADIO endurance PASS; BT phone i LG TV PASS; SourceManager switching/reconnect PASS; TTS RADIO/BT restore PASS; firmware i SPIFFS update PASS; recovery PASS; Config v7 persistence PASS; L/R i VU PASS; brak otwartych błędów P0/P1.

## Znane błędy i pomiary

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
- [POST-STABLE] Dokończyć aktualizację VoxOneBT: podłączyć istniejący sender UART do wspólnego UpdateProgress i UI (BT-FW-6); osobno sprawdzić przerwanie UART, brak powrotu BT, błędną `FW_VERSION` oraz ciągłą pracę MAIN. Utrzymać warunek sukcesu po OTA/flash i powrocie VoxOneBT online z oczekiwaną wersją; MAIN nie restartuje się. VoxOneBT pozostaje osobnym repozytorium.
- BT-FW-7 — PHYSICAL PASS / zakończony: OTA V0 `0.6.1-dev` → `0.6.3-dev`; realny `FRAME_CRC`/NACK i skuteczny retry; `PENDING_VERIFY` → `VALID`; po ręcznym restarcie nadal `0.6.3-dev`.
- BT-UART-SPEED-1 — PHYSICAL PASS / zakończony: link MAIN ↔ VoxOneBT `921600 8N1`, PROTO 2, audio, PLAY/PAUSE, volume, metadata, VU i disconnect/reconnect; pełne OTA `0.6.3-dev` → `0.6.4-dev`, `FW_VERIFY`, `FW_OK`, `PENDING_VERIFY` → `VALID`, wersja zachowana po ręcznym restarcie V0. Wynik WWW i assety SPIFFS na A0 potwierdzone fizycznie (`Success=9`, `Error=10`, `Aborted=11`).
- BT-UART-SPEED-2 — PHYSICAL PASS / zakończony: TX burst 512 B, V0 RX buffer 2048 B; OTA `0.6.5-dev` → `0.6.6-dev` PASS, `PENDING_VERIFY` → `VALID`, po ręcznym restarcie nadal `0.6.6-dev`; throughput ok. 18 kB/s (ok. 3× szybciej).

## DLNA

- [POST-STABLE] Dodać DLNA jako capability-filtered źródło SourceManager. Cykl po implementacji obejmuje RADIO, BT i DLNA wyłącznie tam, gdzie są dostępne według capabilities i runtime availability.
- [POST-STABLE] Zaimplementować konfigurację IP serwera, discovery/scanning, ContentDirectory, browsing, pagination, wybór zasobu/play URL, next track i odtwarzanie folderu.
- [POST-STABLE] Zachować UX PLAYER: klik PLAY/PAUSE, dwuklik następne źródło, trójklik ALL/RND/ONE, przytrzymanie biblioteka.
- [POST-STABLE] W przeglądarce folderów: obrót wybiera, klik wchodzi/odtwarza, „ODTWÓRZ FOLDER” zaczyna od pierwszego utworu, dwuklik wraca poziom wyżej, przytrzymanie wraca do PLAYER, a timeout około 15 s wraca do PLAYER.
- [POST-STABLE] ALL/FOLDER odtwarza folder kolejno w pętli; RND odtwarza każdy utwór raz i tasuje ponownie; ONE zapętla bieżący utwór.
- [POST-STABLE] Zintegrować DLNA z metadata, VU z PCM, SourceManager, WebSocket, WWW, LCD i natywnym HA.

## AUX

- [HARDWARE] Zaprojektować wejście AUX na PCM1808 dla analogowego RCA i ewentualnego źródła wewnętrznego oraz potwierdzić piny, zegary i format I2S input.
- [POST-STABLE] Zintegrować AUX z SourceManager, VU z PCM, WWW, LCD i HA; zamiast bitrate pokazywać sample rate/format PCM oraz właściwy status źródła bez metadata.

## WWW, API i WebSocket

- [POST-STABLE] Rozszerzyć source-aware metadata WWW o przyszłe DLNA/AUX; RADIO/BT są bieżącym działającym baseline.
- [POST-STABLE] Ujednolicić później MUTE między WWW, LCD i HA.
- [POST-STABLE] Rozszerzyć istniejący WebSocket runtime state o source-aware metadata i codec/format DLNA/AUX, sample rate oraz bitrate tam, gdzie mają znaczenie.
- [POST-STABLE] Dokończyć edycję maksymalnie pięciu profili Wi-Fi: priority/last-known-good, nowe hasło, zachowanie lub wyczyszczenie hasła, walidacja, atomowy zapis i kontrolowany restart.
- [POST-STABLE] Dodać konfigurację restartu, sleep/screensaver, auto standby, backup/restore config, playlist import/export, Radio Directory i recovery bez przywracania starego WWW.
- [DECISION] Rozstrzygnąć, czy lokalny WebSocket ma walidować żądany subprotocol zamiast bezwarunkowo go odsyłać; połączyć decyzję z audytem auth/CSRF.

## Local UI / LCD

- [POST-STABLE] Zaimplementować przenośne menu konfiguracji obsługiwane jednym enkoderem, z jednym wspólnym drzewem/modelami pozycji i jednym `MenuController`/modelem stanu dla wszystkich wyświetlaczy. Logika menu nie może zależeć od rozdzielczości ani mieć osobnej implementacji dla każdego LCD; osobne renderery ST7796, ST7789, SSD1306 i pozostałych klas wyłącznie prezentują ten sam stan. Na dużych/średnich ekranach pokazywać u góry kontekst/ścieżkę (np. `KONFIGURACJA > AUDIO`, `DSP > CROSSOVER`), a na małych dopuszczać krótkie etykiety. Środkowy wiersz jest zawsze aktywną pozycją; poprzednia i następna są przygaszone, mogą być puste na granicach. Obrót przewija listę, utrzymując aktywną pozycję pośrodku; klik wybiera ją. Submenu używa tego samego układu i modelu. Obsłużyć uniwersalne typy `SUBMENU`, `TOGGLE`, `ENUM`, `NUMBER`, `ACTION` i `INFO`; capabilities urządzenia określają dostępne pozycje, bez zmiany wspólnego modelu. Ten sam model ma objąć konfigurację oraz przyszłe menu DSP (preset, tryb, PEQ, crossover, subwoofer, outputs i edycję parametrów pasma).
- [POST-STABLE] W menu krótki klik otwiera submenu albo rozpoczyna edycję. Podczas edycji obrót zmienia wartość, a klik zatwierdza ją wyłącznie do konfiguracji roboczej i wraca do listy. Rozróżniać konfigurację bieżącą, working copy i stan dirty; obracanie lub klikanie podczas nawigacji nie może samo powodować trwałego zapisu. Pokazywać kontekstową dolną belkę: przy braku zmian `WRÓĆ`, przy zmianach `ANULUJ` i `ZAPISZ`. Przytrzymanie `LONG` przenosi fokus z listy/edycji na belkę; obrót wybiera jej akcję, klik ją wykonuje. `WRÓĆ` wraca o poziom lub wychodzi zgodnie z kontekstem, `ANULUJ` odrzuca zmiany robocze, a `ZAPISZ` zatwierdza je zgodnie z polityką danej opcji. Nie wykonywać auto-save przy wyjściu. Zastosowanie runtime i kontrolowany zapis ustawień wymagających restartu muszą respektować istniejące zasady VoxOne; szczegółów persistence nie projektować w tym zadaniu.
- [POST-STABLE] Menu konfiguracji otwierać osobnym gestem `VERY_LONG` enkodera wyłącznie z głównych ekranów PLAYER, początkowo około 2,5 s; wejście do menu nie zatrzymuje audio ani nie zmienia source. Istniejący `LONG` zachowuje akcje zależne od źródła (RADIO — lista stacji, BT — sterowanie BT). Gesture manager musi rozstrzygać gest tak, aby przytrzymanie zakończone jako `VERY_LONG` nie uruchomiło wcześniej akcji `LONG`. Wstępne przedziały do późniejszego dopracowania: `CLICK` < ok. 0,7 s, `LONG` ok. 0,7–2,5 s, `VERY_LONG` ≥ ok. 2,5 s; progi nie są jeszcze finalne. Po około 30 s bezczynności automatycznie wrócić do PLAYER, odrzucić working changes bez zapisu i bez ukrytych akcji. Każdy obrót, klik i przytrzymanie resetuje timeout. Wartość 30 s jest domyślna; jej późniejsza konfigurowalność może być oceniona osobno.
- [POST-STABLE] Dzielić implementację menu na małe etapy: `MENU-CORE` (wspólne drzewo, typy pozycji, working copy i stan dirty), `MENU-INPUT` (enkoder, fokus belki, CLICK/LONG/VERY_LONG i timeout), `MENU-RENDER` (renderery klas LCD korzystające wyłącznie ze wspólnego stanu), `MENU-CONFIG` (pozycje konfiguracji i kontrolowane zastosowanie/zapis) oraz `MENU-DSP` (pozycje DSP po dostępności odpowiednich capabilities). Każdy etap zachowuje jeden model logiki; różnice małych LCD dotyczą wyłącznie prezentacji i skrótów etykiet.
- [POST-STABLE] Przebudować PLAYER: podnieść PLAY/PAUSE/STOP oraz bitrate/audio info, a niżej dodać czytelną ramkę faktycznego trybu wyjścia 2.0/2.1/2.2 pochodzącego z konfiguracji audio/DSP.
- [POST-STABLE] Ustalić wspólną lub jawnie przypisaną szybkość przewijania dla stacji, artysty, utworu i list; usunąć przypadkowo różne timingi rendererów.
- [POST-STABLE] Dodać ekran aktualizacji „AKTUALIZACJA” z rzeczywistym postępem, fazami, sukcesem, błędem i restartem MAIN/SPIFFS zgodnie ze wspólnym modelem UpdateProgress; VoxOneBT pokazać po UART ACK, weryfikacji i ponownym połączeniu, bez restartu MAIN.
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

- [POST-STABLE] HA-1 — ustalić wersjonowany kontrakt integracji na bazie istniejącego `/ws`, `webStatus`, CommandHandler i SourceManager: stabilne pola snapshotu i synchronizację po reconnect, błędy/wynik komendy oraz jawne source-aware transport commands. Nie tworzyć osobnego WebSocket ani dodawać REST API wyłącznie dla HA.
- [POST-STABLE] HA-2 — dodać mDNS/Zeroconf `_voxone._tcp` i config flow z potwierdzeniem urządzenia, pełnym MAC MAIN jako unique ID oraz ręcznym host fallback; hostname i IP nie są unique ID.
- [POST-STABLE] HA-3 — zbudować jedno urządzenie i główny `media_player` z runtime state, volume, mute, RADIO, podstawowym BT, `source`/`source_list` oraz source-aware metadata.
- [POST-STABLE] HA-4 — wyliczać `supported_features` zależnie od aktywnego źródła i capabilities; reklamować PLAY/PAUSE/STOP/NEXT/PREV tylko gdy dana akcja jest rzeczywiście obsługiwana. Uwzględnić RADIO i BT, a DLNA/AUX dopiero po ich implementacji.
- [POST-STABLE] HA-5 — dodać stabilne wywołanie URL przez WS dla TTS/announce; VoxOne zarządza temporary override i restore, HA nie implementuje przywracania audio. Reklamować `MEDIA_ANNOUNCE` dopiero po fizycznej weryfikacji.
- [POST-STABLE] HA-6 — dodać tylko minimalne dodatkowe encje: RSSI, uptime, restart i Max Volume; TTS Volume Mode/Fixed po wdrożeniu polityki TTS, standby później.
- [POST-STABLE] HA-7 — przetestować restart HA i VoxOne, zmianę IP, utratę/reconnect WebSocket, partial snapshot i duplikat komendy.
- [POST-STABLE] HA-8 — uruchomić native HA równolegle z MQTT i przeprowadzić audyt parytetu oraz fizyczne testy.
- [POST-STABLE] HA-9 — rozważyć wycofanie MQTT dopiero po potwierdzeniu fizycznego parytetu i stabilnego działania native HA.
- [DECISION] Przed dystrybucją native HA ustalić wymagania tokenu/auth i zabezpieczenia mutujących komend; mDNS nie jest autoryzacją. Istniejący hostname mDNS nie publikuje jeszcze usługi `_voxone._tcp`.
- [DECISION] Zweryfikować znaczenie `status.on = config.store.dspon` w obecnym `ha_yoradio` i nie zmieniać kontraktu MQTT bez fizycznej regresji.

## Network

- [POST-STABLE] Zachować maksymalnie pięć profili Wi-Fi, priority/last-known-good, tryb NORMAL bez wymuszonego AP, oczekiwanie RADIO na sieć, możliwość działania BT offline oraz jednoznaczne CONFIG/AP/recovery behavior.
- [HARDWARE] Dodać W5500 tylko na odpowiednich PCB i dedykowanej magistrali SPI; tryby AUTO/LAN/Wi-Fi, LAN preferred, Wi-Fi fallback i DHCP, a później opcjonalny static IP.
- [POST-STABLE] Wydzielić abstrakcję sieciową bez twardego założenia `WiFiClient`; RADIO i DLNA mają działać przez LAN lub Wi-Fi.
- [POST-STABLE] W WWW pokazywać aktywny interfejs/link/IP, a na LCD dla Ethernet używać małej ikony RJ45 zamiast słupków RSSI bez przesuwania stałego obszaru systemowego.

## Playlisty i stacje

- [POST-STABLE] Dopracować import/export yoRadio: preview, walidacja, raport błędnych rekordów i ewentualny import URL.
- [STABLE] Sprawdzić listy 50/100/250 stacji, power loss, brak miejsca SPIFFS, `current/lastStation`, usunięcie aktywnej stacji, reorder i zachowanie po restarcie.
- [STABLE] Zweryfikować eksport/import między X0 i A0: ID, kolejność, OVOL, A↔T, odświeżenie WWW oraz backup/restore przez Web Update.
- [STABLE] Fizycznie przetestować Radio Directory na A0 i X0: dodanie, odtwarzanie, restart, pamięć i brak zakłóceń audio.
- [POST-STABLE] Sprawdzić prezentację A↔T na LCD/WWW/MQTT oraz zachowanie po reorder i usunięciu stacji.

## Hardware targets

- [STABLE] Utrzymać X0, B0 i A0 bez zmiany architektury do pierwszego stable; klasyczny ESP32 pozostaje stabilnym legacy targetem.
- [HARDWARE] Po stable zaprojektować pierwsze PCB A1/B1/C1/D1 oraz prototypy C0/D0; mapy GPIO i złącza mogą różnić się od rewizji 0 bez zmiany logiki produktu.
- [HARDWARE] Potwierdzić GPIO, rewizje PCB, opcjonalne LCD/BT/DSP oraz warianty wyjścia PCM5102A, MAX98357 dla C-family i DSPmini.
- [POST-STABLE] Utrzymać zasadę jednego builda firmware na target PCB zamiast buildów dla każdej kombinacji opcji; `HardwareDescriptor` i capabilities są źródłem prawdy.
- [POST-STABLE] Aktualizację VoxOneBT przez MAIN na Ax/Bx/Cx/Dx dopuścić tylko przy obecnym module BT, odpowiednim capability oraz zasobach PSRAM/staging; na Xx aktualizować VoxOneBT wyłącznie bezpośrednio przez USB. Nie dodawać stagingu na Xx.

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
- P3 — UX/polish
- P4 — VoxOneBT hardening
- P5 — DLNA / AUX
- P6 — native HA
- P7 — A1/B1/C1/D1 + capabilities
- P8 — DSPmini
- P9 — Installer + pełna konfiguracja LCD
- P10 — W5500 / dalsze rozszerzenia
