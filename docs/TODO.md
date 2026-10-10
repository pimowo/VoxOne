# VoxOne — kanoniczny backlog

> To jest jedyna lista otwartych prac VoxOne. Dokumenty sprzętowe, baseline'y i inventory opisują stan lub historię, ale nie są roadmapą. Po ukończeniu i właściwej weryfikacji zadanie znika z tego pliku; nie prowadzimy sekcji DONE.
>
> Klasyfikacja: `[BUG]` — znany problem, `[STABLE]` — wymagane do wiarygodnego stable, `[DECISION]` — potrzebna jawna decyzja, `[POST-STABLE]` — praca po pierwszym stable, `[HARDWARE]` — projekt lub test sprzętowy.

## Stable hardening

### RADIO

- [STABLE] Wykonać na fizycznym L0 minimum czterogodzinny endurance RADIO: jedna stabilna stacja przez co najmniej 2 h, minimum trzy zmiany stacji i dalsze granie do minimum 4 h.
- [STABLE] Zebrać log z uptime, heap/minimum heap/largest block, PSRAM/minimum PSRAM, stack HWM głównej pętli i DisplayTask, `loopMaxUs`, `audioBuffer`, reconnectami, Wi-Fi oraz RSSI; ocenić trend pamięci, stack, blokady pętli, watchdog/reboot i serie reconnectów.
- [STABLE] Podczas endurance potwierdzić brak trwałych stall/dropout, poprawne ponowne uruchamianie streamu oraz ciągłą reakcję LCD i VU.
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

- [STABLE] Fizycznie sprawdzić negatywne ścieżki Web Update MAIN i SPIFFS: błędny obraz, cancel/przerwanie lub zerwane połączenie podczas uploadu, brak auto-resume audio po Error/Aborted oraz zwolnienie UPDATE_LOCK po błędzie.
- [POST-STABLE] Ograniczyć publikację postępu zapisu do zmiany procentu i nie częściej niż co ok. 100–250 ms; zmiany faz przekazywać natychmiast. Obecnie backend aktualizuje stan po kolejnych chunkach.
- [POST-STABLE] Udostępnić ten sam rzeczywisty stan zapisu MAIN/SPIFFS w WWW; obecny postęp HTTP uploadu pozostawić jako pomocniczy, wyraźnie odróżniony od postępu zapisu flash.
- [POST-STABLE] Uzupełnić testy negatywnych ścieżek SPIFFS update: błąd backupu, przerwanie po unmount i remount po błędzie.
- [STABLE] Sprawdzić backup/restore całej konfiguracji, walidację schematu, błąd lub nieudany backup oraz zachowanie config po firmware/SPIFFS update.
- [STABLE] Sprawdzić recovery AP, `/update.html` i `/emergency` przy niedostępnym SPIFFS, błędne dane Wi-Fi, Serial CLI oraz ekran AP na telefonie.
- [STABLE] Zweryfikować restart i Config v7: volume, MUTE, source intent, aktywną stację oraz rozdział ustawień runtime/persistent.
- [STABLE] Sprawdzić auto reload WWW: powrót do STATUS, timeout i czytelny komunikat, gdy urządzenie nie wróci.

### Targety i release gate

- [STABLE] Wykonać fizyczną regresję W0, L0 i M0 po zmianie nazewnictwa: full image, first boot/AP/Wi-Fi/WWW, RADIO, audio, wyświetlacz, enkoder i restart. Na L0 sprawdzić VoxOneBT oraz MQTT/HA; S0/DIN0 wymagają osobnej weryfikacji na docelowym sprzęcie.
- [STABLE] Na W0 sprawdzić ukrycie suwaka jasności, restart z WWW, powrót Wi-Fi bez utraty stacji/config oraz osobno ekran aktualizacji bez regresji ScrollWidget/HOLD.
- [STABLE] Fizycznie zweryfikować aktualne WWW na L0 i W0 (gdy bezpieczna aktualizacja W0 będzie możliwa): MUTE, selector RADIO/BT, oznaczenie BT offline, manual source priority, reconnect/resnapshot, favicon bez 404 oraz build identity w SYSTEM i AKTUALIZACJA.
- [HARDWARE] Przypisać GPIO XSMT PCM5102A na L0 i fizycznie sprawdzić LOW przy PAUZA/STOP, HIGH przy PLAY, ciszę podczas przejść i Web Update oraz czerwoną ramkę VOL bez zmiany semantyki MUTE.
- [STABLE] Fizycznie sprawdzić MQTT/Home Assistant na L0; MQTT pozostaje wspierane w pierwszym stable.
- [STABLE] Uzupełnić dokumentację aktywnych profili, API, MQTT/HA, Source Managera, VoxOneBT oraz update/recovery; oznaczyć historyczne baseline'y i usunąć z dokumentów bieżącego stanu opisy sprzeczne z aktualnym runtime.
- [DECISION] Przed stable ustalić minimalny zakres uwierzytelniania, CSRF i ochrony mutujących REST/WebSocket oraz ekspozycji danych.

### Definition of Stable

- [STABLE] Zamknąć release gate: FULL VERIFY PASS; fizyczny W0, L0 i M0 PASS; RADIO endurance PASS; BT phone i LG TV PASS; SourceManager switching/reconnect PASS; TTS RADIO/BT restore PASS; firmware i SPIFFS update PASS; recovery PASS; Config v7 persistence PASS; L/R i VU PASS; brak otwartych błędów P0/P1.

## Znane błędy i pomiary

- [BUG] Sprawdzić bezpieczeństwo bufora/okna i clipping w `ScrollWidget`, szczególnie przy długich metadata; powiązać wynik z błędem czyszczenia i nie zmieniać timingu bez testu rendererów.
- [HARDWARE] Wykonać kontrolowane porównanie poziomu RADIO i BT na tym samym materiale PCM. Użytkownik zmienił rezystory z 22 Ω na 41 Ω, ale trzeba nadal zmierzyć digital gain, BT Absolute Volume, VoxOneBT PCM, MAIN RX i poziom wyjścia.
- [BUG] Zbadać pstryknięcie lub glitch audio podczas mutacji playlisty/stacji.

## Audio, RADIO i kodeki

- [STABLE] Utrzymywać MP3, AAC/AAC+, FLAC oraz M4A wyłącznie jako kontener AAC; dokończyć detekcję AAC/AAC+ i `audio/aacp`, aby danych AAC nie kierować do dekodera MP3; przetestować MP3, AAC/AAC+ (w tym RMF DLA DZIECI), FLAC, SHOUTcast/Icecast, `ICY 200`, HTTP/HTTPS, metadata/brak metadata, reconnect i dead stream.
- [STABLE] Dokończyć obsługę M3U, PLS, redirect i final URL streamu z limitem rekurencji i ochroną przed pętlą; zweryfikować te ścieżki na rzeczywistych playlistach.
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
- [STABLE] Fizycznie sprawdzić negatywne ścieżki aktualizacji VoxOneBT: przerwanie UART, brak powrotu modułu, nieoczekiwaną `FW_VERSION`, timeout identity, uszkodzony/błędny obraz oraz Error/Aborted z odzyskaniem sterowania i zwolnieniem UPDATE_LOCK.

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

- [POST-STABLE] Zaimplementować przenośne menu konfiguracji, aby urządzenie można było konfigurować bez WWW, obsługiwane jednym enkoderem, z jednym wspólnym drzewem/modelami pozycji i jednym `MenuController`/modelem stanu dla wszystkich wyświetlaczy. Logika menu nie może zależeć od rozdzielczości ani mieć osobnej implementacji dla każdego LCD; osobne renderery ST7796, ST7789, SSD1306 i pozostałych klas wyłącznie prezentują ten sam stan. Na dużych/średnich ekranach pokazywać u góry kontekst/ścieżkę (np. `KONFIGURACJA > AUDIO`, `DSP > CROSSOVER`), a na małych dopuszczać krótkie etykiety. Środkowy wiersz jest zawsze aktywną pozycją; poprzednia i następna są przygaszone, mogą być puste na granicach. Obrót przewija listę, utrzymując aktywną pozycję pośrodku; klik wybiera ją. Submenu używa tego samego układu i modelu. Obsłużyć uniwersalne typy `SUBMENU`, `TOGGLE`, `ENUM`, `NUMBER`, `ACTION` i `INFO`; capabilities urządzenia określają dostępne pozycje, bez zmiany wspólnego modelu. Ten sam model ma objąć konfigurację oraz przyszłe menu DSP (preset, tryb, PEQ, crossover, subwoofer, outputs i edycję parametrów pasma).
- [POST-STABLE] W menu krótki klik otwiera submenu albo rozpoczyna edycję. Podczas edycji obrót zmienia wartość, a klik zatwierdza ją wyłącznie do konfiguracji roboczej i wraca do listy. Rozróżniać konfigurację bieżącą, working copy i stan dirty; obracanie lub klikanie podczas nawigacji nie może samo powodować trwałego zapisu. Pokazywać kontekstową dolną belkę: przy braku zmian `WRÓĆ`, przy zmianach `ANULUJ` i `ZAPISZ`. Przytrzymanie `LONG` przenosi fokus z listy/edycji na belkę; obrót wybiera jej akcję, klik ją wykonuje. `WRÓĆ` wraca o poziom lub wychodzi zgodnie z kontekstem, `ANULUJ` odrzuca zmiany robocze, a `ZAPISZ` zatwierdza je zgodnie z polityką danej opcji. Nie wykonywać auto-save przy wyjściu. Zastosowanie runtime i kontrolowany zapis ustawień wymagających restartu muszą respektować istniejące zasady VoxOne; szczegółów persistence nie projektować w tym zadaniu.
- [POST-STABLE] Menu konfiguracji otwierać osobnym gestem `VERY_LONG` enkodera wyłącznie z głównych ekranów PLAYER, początkowo około 2,5 s; wejście do menu nie zatrzymuje audio ani nie zmienia source. Istniejący `LONG` zachowuje akcje zależne od źródła (RADIO — lista stacji, BT — sterowanie BT). Gesture manager musi rozstrzygać gest tak, aby przytrzymanie zakończone jako `VERY_LONG` nie uruchomiło wcześniej akcji `LONG`. Wstępne przedziały do późniejszego dopracowania: `CLICK` < ok. 0,7 s, `LONG` ok. 0,7–2,5 s, `VERY_LONG` ≥ ok. 2,5 s; progi nie są jeszcze finalne. Po około 30 s bezczynności automatycznie wrócić do PLAYER, odrzucić working changes bez zapisu i bez ukrytych akcji. Każdy obrót, klik i przytrzymanie resetuje timeout. Wartość 30 s jest domyślna; jej późniejsza konfigurowalność może być oceniona osobno.
- [POST-STABLE] Dzielić implementację menu na małe etapy: `MENU-CORE` (wspólne drzewo, typy pozycji, working copy i stan dirty), `MENU-INPUT` (enkoder, fokus belki, CLICK/LONG/VERY_LONG i timeout), `MENU-RENDER` (renderery klas LCD korzystające wyłącznie ze wspólnego stanu), `MENU-CONFIG` (pozycje konfiguracji i kontrolowane zastosowanie/zapis) oraz `MENU-DSP` (pozycje DSP po dostępności odpowiednich capabilities). Każdy etap zachowuje jeden model logiki; różnice małych LCD dotyczą wyłącznie prezentacji i skrótów etykiet.
- [POST-STABLE] Przebudować PLAYER: podnieść PLAY/PAUSE/STOP oraz bitrate/audio info, a niżej dodać czytelną ramkę faktycznego trybu wyjścia 2.0/2.1/2.2 pochodzącego z konfiguracji audio/DSP.
- [POST-STABLE] Ustalić wspólną lub jawnie przypisaną szybkość przewijania dla stacji, artysty, utworu i list; usunąć przypadkowo różne timingi rendererów.
- [POST-STABLE] Dodać source-aware PLAYER, ekran TTS, przyszłą przeglądarkę DLNA oraz konfigurację DSP.
- [HARDWARE] Na M0/S0/DIN0 fizycznie sprawdzić dalszą stabilność OLED, input i audio po zmianie tożsamości buildów; wcześniejszy PHYSICAL PASS SSD1306 i SSD1309 na tym samym prototypie pozostaje baseline. VoxOneBT i MQTT/HA na tych profilach nie mają PHYSICAL PASS.
- [POST-STABLE] Przygotować wspólne `assets/branding` jako źródło logo WWW, splash/logo LCD i favicon; później użyć tych samych materiałów w README/GitHub.
- [POST-STABLE] Rozważyć opcjonalną skórkę YAMAHA AMBER: czarne tło i jeden bursztynowy kolor, punkt startowy `#FF9A1F` / RGB565 `0xFCC3`.
- [DECISION] Rozstrzygnąć zakres dalszego wsparcia ST7789 284×76 na rodzinie Wx.
- [POST-STABLE] Uporządkować duplikację `_charSize` w widgetach tylko przy pracy nad rendererem; nie robić osobnego refaktoru bez korzyści testowej.
- [HARDWARE] Ocenić opcjonalny czujnik światła dopiero po ustaleniu docelowych PCB i display.

## Power, startup i alarm

- [POST-STABLE] Zaprojektować włączanie/wyłączanie VoxOne z rozróżnieniem reboot, standby, audio mute i pełnego wyłączenia.
- [HARDWARE] Dodać sterowanie `AMP_POWER` z anti-pop: mute/fade przed wyłączeniem, opóźnienie po włączeniu i unmute/fade po stabilizacji.
- [POST-STABLE] Dodać auto standby/auto power-off po konfigurowanym czasie bez dźwięku, ustawienie czasu w WWW i możliwość OFF; zdefiniować aktywność osobno dla RADIO, BT, DLNA, AUX i TTS, tak aby komunikat TTS nie został zablokowany. Osobno dopracować screensaver, display ON/OFF, sleep timer i wybudzanie: określić ekran oraz zachowanie audio w stanach PLAY/STOP i podczas sleep; deep sleep tylko przy rzeczywistej potrzebie.
- [POST-STABLE] Skonsolidować configurable boot source, boot station, startup PLAY/STOP, LAST/FIXED startup volume, max physical volume i MUTE semantics. Określić bezpieczny fallback, gdy zapisane źródło nie istnieje na danym PCB, oraz przetestować cold boot. Zachować rozdział technicznego checkpointu od release version.
- [POST-STABLE] Dodać alarm: czas, dni, stacja, volume, ON/OFF oraz tryb „włącz radio” albo „graj przez X”; określić fade-in, czas odtwarzania i powiązanie ze sleep/wake.

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
- [STABLE] Zweryfikować eksport/import między W0 i L0: ID, kolejność, OVOL, A↔T, odświeżenie WWW oraz backup/restore przez Web Update.
- [STABLE] Fizycznie przetestować Radio Directory na L0 i W0: dodanie, odtwarzanie, restart, pamięć i brak zakłóceń audio.
- [POST-STABLE] Sprawdzić prezentację A↔T na LCD/WWW/MQTT oraz zachowanie po reorder i usunięciu stacji.

## Hardware targets

VoxOneBT jest osobnym projektem/repo i posiada własną rodzinę sprzętową Vx.

- [HARDWARE] Potwierdzić GPIO, rewizje PCB, opcjonalne LCD/BT/DSP oraz przyszłe warianty wyjścia PCM5102A, MAX98357A i DSPmini. Na nowych PCB zweryfikować GPIO, audio, OLED i EC11; wcześniejszy fizyczny baseline M0 obejmuje PCM5102A GPIO1/2/3, oba OLED I2C GPIO7/8 oraz EC11 GPIO4/5/6. Pozostałe OLED-y 128×64 wymagają driverów i testów.
- [HARDWARE] Zweryfikować na fizycznych prototypach S0 i DIN0 osobne profile i docelowe wyposażenie. Dzisiejsze buildy używają roboczego pinoutu M0, PCM5102A, OLED 128×64 i EC11; nie są dowodem działania przyszłych modułów.
- [POST-STABLE] Dodać runtime input activation, wybór EC11/Buttons3 i jawny `supportedInputs` mask. Driver 3×SW ma emitować wspólne zdarzenia `LEFT/OK/RIGHT` przez istniejący kontrakt `UiInputEvent`, używać tych samych logicznych linii A/B/KEY co EC11 oraz uwzględniać zewnętrzne podciąganie i filtrację; gesty CLICK/DOUBLE/LONG/VERY_LONG mają być rozstrzygane bez przedwczesnego wywołania krótszego gestu.
- [POST-STABLE] Dodać runtime audio activation; nowe driver/backendy włączać tylko na PCB z potwierdzonymi capabilities.
- [POST-STABLE] Dodać konfigurację sprzętu w WWW na podstawie `RuntimeHardwareConfig`: wybór display, wejścia EC11/Buttons3 i audio filtrowany przez `HardwareCapabilities`, kontrolowany zapis i restart tam, gdzie zmiana wymaga restartu. Później rozważyć osobne installed/enabled dla modułów opcjonalnych. Jeden profil PCB ma dostarczać jeden BIN; wybory użytkownika nie tworzą nowych env.
- [POST-STABLE] Dodać backendy wyświetlaczy z macierzy rodzin oraz layout `OLED_128X32` dla SSD1306_128X32. PLAYER: góra nazwa stacji, dół belka info jak w OLED_128X64, bez artist/title/dużego zegara. STATIONS: góra wybrana stacja, dół `GRA` po lewej i `x/N` po prawej, bez nagłówka. VOL: góra duże 0–100 lub MUTE, dół IP, bez nagłówka.
- [POST-STABLE] Wprowadzić jeden parametr `Jasność` 0–100% w WWW i menu lokalnym: TFT przez PWM podświetlenia, OLED przez contrast/current, 0% wygasza display.
- [DECISION] Ustalić, czy rzeczywista zmiana `userVolume` z WWW, HA lub BT wywołana przez użytkownika otwiera ekran VOL; snapshot, restore i synchronizacja nie mogą go otwierać przypadkowo.
- [DECISION] Ustalić, czy ekran VOL dostaje pasek 0–100; przy MUTE pasek ma pokazywać zapamiętane `userVolume`, ponieważ MUTE nie oznacza volume 0.
- [HARDWARE] Zaprojektować osobne docelowe PCB L1/M1/S1/DIN1; każda rewizja wymaga własnego pinoutu i capabilities bez współdzielonego BoardProfile. Potwierdzić GPIO i capabilities każdej płytki.
- [POST-STABLE] Aktualizację VoxOneBT przez MAIN dopuścić tylko przy obecnym module, capability BT firmware update i wystarczającym PSRAM/staging. Na przyszłym Wx z modułem BT aktualizację modułu prowadzić bezpośrednio przez USB, bez stagingu na MAIN.

### Architecture baseline

Obecny baseline: dane i widoki display są rozdzielone; `HardwareCapabilities` jest autorytatywnym opisem możliwości PCB; globalny `userVolume` ma zakres 0–100; wejście UI emituje znormalizowane zdarzenia, a `UiState` należy do core. `DisplayProfile` jest oddzielony od PCB, a `RuntimeHardwareConfig` od `UserSettings`. M0 ma jeden BIN z backendami SSD1306_128X64 i SSD1309_128X64; wybór przez `RuntimeHardwareConfig` i oba OLED mają wcześniejszy PHYSICAL PASS na tym samym prototypie. To stan architektury, nie backlog.

Nazwy wariantów display: `STEROWNIK_ROZDZIELCZOŚĆ`, np. `SSD1306_128X32`, `SSD1306_128X64`, `SSD1309_128X64`, `CH1116_128X64`, `SH1106_128X64`, `ST7789V_320X240`, `ST7789_284X76`, `SSD1322_256X64`, `ST7796S_480X320`, `GC9A01_240X240`. Dla modułu opisanego `ST7290_128X64` lub ST7920-compatible trzeba potwierdzić fizyczny kontroler i protokół przed nazwaniem backendu.

## Installer, first boot i release

- [POST-STABLE] Przygotować VoxOne Installer dla Windows jako portable EXE dla VoxOne: wybór rodziny L/M/S/DIN/W i właściwej rewizji PCB, embedded stable factory binaries, install, erase/install, auto COM, auto chip i brak ręcznych offsetów.
- [POST-STABLE] Zaprojektować first boot AP `VoxOne-XXXXXX` i captive wizard do Wi-Fi oraz podstawowego hardware/config; dalsza konfiguracja ma być dostępna przez WWW i LCD.
- [POST-STABLE] Docelowy przepływ produkcyjny ma po flashu nie wymagać VS Code ani edycji kodu.
- [STABLE] Przed release potwierdzić artefakty firmware/SPIFFS/full image, instrukcję instalacji i recovery oraz zasady zachowania NVS/config.
- [STABLE] Po przejściu wszystkich gate'ów przygotować stable release i merge `project-cleanup-1` → `main`; dopiero release otrzymuje decyzję o zmianie `VOXONE_VERSION`.

## Dystrybucja

- [DECISION] Przed dystrybucją rozstrzygnąć, czy VoxOne ma używać licencji offline powiązanej z konkretnym ESP. Omówiony, niezatwierdzony wariant: Installer zbiera dane urządzenia, użytkownik przekazuje je z adresem email autorowi, a autor odsyła aktywację; przy jej braku LCD pokazuje logo VoxOne, „Brak licencji” i kontakt `pimowo@voxone.pl`. Ocenić zgodność wariantu z licencją projektu i zależnościami przed ewentualnym projektem mechanizmu.

## Compatibility i małe decyzje techniczne

- [DECISION] Uporządkować compatibility API strefy czasowej: system używa stałej Europe/Warsaw, pola Config v7 pozostają zachowane, ale `getTimezoneOffset()` nie może pozostać nieopisanym `return 0`; ustalić kontrakt bez zmiany serialized layout.

## Kolejność

- P0 — STABLE HARDENING
- P1 — BUGFIXES znalezione podczas stable
- P2 — STABLE RELEASE + merge `project-cleanup-1` → `main`
- P3 — UX/polish
- P4 — integracja VoxOneBT
- P5 — DLNA / AUX
- P6 — native HA
- P7 — L1/M1/S1/DIN1 + capabilities
- P8 — DSPmini
- P9 — Installer + pełna konfiguracja LCD
- P10 — W5500 / dalsze rozszerzenia
