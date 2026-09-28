# VoxOne — aktualne zadania

> Jedyna kanoniczna lista bieżących prac projektu. Zakończone i zweryfikowane zadania usuwamy; nie prowadzimy sekcji DONE.

## 1. Zachowanie uruchomieniowe urządzenia

**Priorytet: przed SALON. Wspólne dla DESK, SALON i DIN.**

- Ustalić konfigurację startową: `GRAJ` albo `POZOSTAŁ STOP`.
- Ustalić źródło startowe: ostatnie źródło, RADIO, a później inne źródła według capabilities.
- Dla RADIO obsłużyć ostatnią stację albo konkretną skonfigurowaną stację.
- Określić zachowanie, gdy zapisane źródło nie istnieje w profilu.
- Dodać konfigurację w WWW i persistence.
- Wykonać test cold boot.
- Nie łączyć tej funkcji ze Startup Volume.

## 2. Krótki audit DESK

**Priorytet: przed przeniesieniem głównych prac na SALON.**

- Sprawdzić dłuższe odtwarzanie 128/256/320 kbps i kilka restartów.
- Sprawdzić szybki enkoder, dwóch klientów WWW i Wi-Fi reconnect.
- Sprawdzić Web Update firmware oraz SPIFFS.
- Sprawdzić trzaski i heap po dłuższym działaniu.
- Nie rozpoczynać kolejnego dużego refaktoru DESK bez konkretnego problemu.

## 3. Audio — `Truncated MP3 frame`

**Priorytet: jeśli problem nadal występuje; stabilne audio nie blokuje SALON.**

- Nie traktować niepełnej ramki MP3 od razu jako fatal error; zachować jej początek, doczytać dane i dopiero wtedy dekodować.
- Sprawdzić ICY stripping, `memmove`, MP3 sync, frame length i Helix.
- Testować szczególnie Muzyczne Radio DAB+ oraz reconnecty około 66–71 s.

## 4. Audio — mikroprzycięcia i wydajność

**Priorytet: diagnostyka; nie optymalizować bez pomiarów.**

- Po finalnym MQTT wykonać ponownie `voxone_debug`.
- Sprawdzić `loopMax`, `mqtt_us`, UI, logger, web, starvation, `need`, buffer minimum i I2S maximum.
- Na podstawie pomiarów zdecydować, czy optymalizować MQTT, logger, UI, WebServer lub audio task/core.
- Nie przenosić audio do osobnego FreeRTOS task/core bez pomiarów uzasadniających zmianę.

## 5. SALON-HW-1 — pierwszy SALON bez TDA

**Priorytet: następny duży etap.**

### Sprzęt

- ESP32-S3 DevKitC-1 N16R8, ST7796S 480×320, PCM5102A, główny enkoder i VoxOneBT jako osobny klasyczny ESP32.
- Pierwszy etap bez TDA7719, AUX i SPDIF.

### Połączenia

- LCD: DC GPIO9, CS GPIO10, MOSI GPIO11, SCK GPIO12, BL GPIO14, RST `-1`; MISO GPIO13 nie podłączać.
- PCM5102A: DOUT GPIO4, BCLK GPIO5, LRCK GPIO6.
- Enkoder na `ENCODER_2`: S2 GPIO47, S1 GPIO48, KEY GPIO21.
- Ustawić `USE_BUILTIN_LED = false`.
- VoxOneBT przez `NEXTION`: SALON RX GPIO15, SALON TX GPIO16.
- VoxOneBT audio I2S przez `ENCODER_1`: BCLK GPIO41, WS/LRCLK GPIO40, DATA IN GPIO39.
- W przyszłym SALON_DSP współdzielić I²C GPIO7/8 przez jedną instancję magistrali z DS3231 i TDA7719.

### Profil i uruchomienie

- Zweryfikować współdzielenie GPIO48 z RGB LED.
- Sprawdzić PCM5102A i tor audio podczas odtwarzania na SALON.
- Sprawdzić MQTT/HA na SALON.

## 6. LCD SALON

**Priorytet: po uruchomieniu sprzętu; najpierw funkcje, potem grafika.**

- Sprawdzić synchronizację VU meter ON/OFF między klientami WWW i zachowanie ustawienia po restarcie.
- Sprawdzić obciążenie SPI względem audio i unikać ciężkich pełnych redrawów.
- W razie potrzeby wykorzystać stabilne podejście DisplayTask z DESK.

## 7. VoxOneBT — integracja

**Priorytet: główne drugie źródło SALON.**

- Połączyć UART MAIN ↔ VoxOneBT oraz I2S PCM VoxOneBT → MAIN; uruchomić I2S RX w SALON i osobny I2S TX SALON → PCM5102A.
- Sprawdzić stany READY, CONNECTED, DISCONNECTED, PLAYING, PAUSED oraz metadata ARTIST, TITLE, ALBUM, SAMPLE_RATE i VOLUME.
- Sprawdzić PLAY, PAUSE, NEXT, PREV, GET_STATUS i SET_VOLUME.
- Sprawdzić wielokrotne connect/disconnect, reconnect, wcześniejszy HCI allocation assert, synchronizację Volume telefonu z VoxOne oraz metadata peer/AVRCP.

## 8. Source Manager

**Priorytet: razem z BT. Pierwszy zakres RADIO | BT; docelowo RADIO | BT | DLNA | AUX | SPDIF.**

- Zapewnić dokładnie jedno aktywne źródło i jawny active source.
- Obsłużyć RADIO ↔ BT, wspólne PLAY/STOP i Volume, poprzednie źródło oraz tymczasowe PLAY_MEDIA/TTS.
- Po PLAY_MEDIA przywracać źródło; obsłużyć pending source podczas PLAY_MEDIA.
- Podłączyć double-click enkodera do `cycleNextSource()` po uruchomieniu Source Managera; zasilić wiersze LCD stanem aktywnego źródła i nazwą peer BT.
- Przełączać dostępne źródła bezpośrednio double-clickiem w pętli według capabilities i dostępności runtime; bez osobnego ekranu wyboru źródła.
- Oprzeć zachowanie startowe urządzenia na Source Managerze.

## 9. PLAY_MEDIA / TTS

**Priorytet: po RADIO+BT.**

- Sprawdzić sekwencje RADIO → TTS → RADIO, STOP → TTS → STOP, BT → TTS → BT i kilka TTS pod rząd.
- Sprawdzić długi i przerwany TTS, błędny URL, timeout i brak Wi-Fi.
- Zapewnić deterministyczny cleanup i restore bez heap leaków.
- Później dodać HTTPS, redirecty i opcjonalne Volume `CURRENT/FIXED`.

## 10. AAC / AAC+

**Priorytet: poprawa zgodności z rzeczywistymi stacjami.**

- Obsłużyć AAC, AAC+ i `audio/aacp` z poprawną detekcją kodeka.
- Nie przekazywać AAC do dekodera MP3.
- Przetestować RMF DLA DZIECI i inne rzeczywiste stacje AAC+.

## 11. M3U / PLS i kolejne formaty playlist streamów

**Priorytet: po AAC.**

- Obsłużyć `.m3u`, `.pls`, rozwiązywanie końcowego URL streamu i redirect playlist.
- Dodać limit rekurencji i ochronę przed pętlą.
- Później rozważyć ASX i M3U8.

## 12. Radio compatibility audit

**Priorytet: przed release; bez osobnego dużego refaktoru.**

- Przetestować starsze SHOUTcast/Icecast, `ICY 200 OK`, nietypowe MIME, HTTP/1.0 i HTTP/1.1.
- Sprawdzić przejścia HTTP → HTTPS, HTTPS → HTTPS i HTTPS → HTTP.
- Sprawdzić metadata, brak metadata, reconnect, dead stream, server close i stall.
- Zweryfikować semantykę `status.on = config.store.dspon` w `ha_yoradio`.

## 13. VoxOne Stations v1 i import/export

**Priorytet: fizyczna weryfikacja po wdrożeniu formatu.**

- Sprawdzić migrację przy Web Update firmware i SPIFFS, w tym odtworzenie starego `playlist.csv` z kopii NVS oraz ponowny start po przerwaniu zapisu.
- Zweryfikować eksport/import VoxOne między DESK i SALON, zachowanie ID, kolejności, OVOL i A↔T oraz podgląd przed zapisem.
- Po imporcie sprawdzić natychmiastowe odświeżenie WWW bez „Ponów”, zachowanie `current=0` bez autoplay oraz ID po restarcie.
- Ponownie sprawdzić cold boot SALON pod kątem pojedynczego panic `Stack canary watchpoint triggered (ipc1)`; zebrać pełny log i pomiary wolnego stosu z firmware diagnostycznego.
- Dodać raport błędnych rekordów przy imporcie natywnego pliku VoxOne.
- Przetestować duże listy 50 / 100 / 250 stacji.
- Rozważyć strumieniową odpowiedź `GET /api/stations` po zapewnieniu spójnego snapshotu przy równoległych mutacjach; obecny GET buduje odpowiedź z wektora.
- Sprawdzić power-loss recovery, brak miejsca SPIFFS, `current/lastStation` i politykę po usunięciu aktualnej stacji.
- Sprawdzić prezentację reguły A↔T na LCD DESK/SALON, WWW, Nextion i MQTT oraz zmianę podczas PLAY bez restartu streamu.
- Zbadać lekkie pstryknięcie audio przy reorder/mutacji.
- Sprawdzić wyłączenie legacy `/upload` na urządzeniu.

## 13a. Wyszukiwarka stacji internetowych

**Priorytet: przyszły etap, po stabilizacji VoxOne Stations v1.**

- Zintegrować wyszukiwarkę Radio Browser; wynik dodawać przez zwykłe ADD, z ID generowanym przez VoxOne.

## 14. WWW — auto-reload po restartach

**Priorytet: mały koszt, duża poprawa UX.**

- Po `ZAPISZ` pokazać restarting, odpytywać urządzenie co około 1 s przez 20–30 s i automatycznie przeładować stronę po jego powrocie.
- Dodać fallback, gdy urządzenie nie wróci.

## 15. WWW — Wi-Fi

**Priorytet: nie blokuje SALON.**

- Domyślnie zwijać profile; rozwijać pojedynczy profil.
- Obsłużyć enabled, SSID, password, clear i priority; opcjonalnie oznaczyć aktywny profil.

## 16. WWW — System

**Priorytet: przed stable.**

- Pokazać firmware version, hardware profile, IP, RSSI, uptime, free heap i MAC.
- Dodać restart, backup, restore i factory reset.

## 17. Web Update — UX

**Priorytet: poprawić bez niepotrzebnej przebudowy działającego mechanizmu.**

- Fizycznie zweryfikować Web Update firmware na SALON oraz firmware/SPIFFS na DESK, postęp wysyłania, błędy backendu, nieudany backup i automatyczny powrót WWW.
- Fizycznie sprawdzić przekierowanie `/update.html` i awaryjny `/emergency` także przy niedostępnym SPIFFS.
- Później rozważyć LCD `AKTUALIZACJA` i rollback, jeśli będzie potrzebny.
- Nie przebudowywać partition table wyłącznie dla porządku.
- Przetestować odrzucanie błędnych danych formularza AP, odzyskiwanie przez Serial CLI i wygląd ekranu AP na telefonie.
- Fizycznie potwierdzić nową paletę kolorów wbudowanego ekranu AP na SALON.

## 18. LCD DESK — ekran AKTUALIZACJA

**Priorytet: później; nie blokuje SALON/stable.**

- Ustalić przyczynę wcześniejszej regresji.
- Pokazać czarne tło i wycentrowany czerwony napis `* AKTUALIZACJA *`, bez RSSI/IP/footer/Volume, bez wpływu na audio.
- Nie ruszać stabilnego ScrollWidget/HOLD bez konkretnego powodu.

## 19. Dodatkowe wyświetlacze

**Priorytet: później.**

- SSD1306 128×32: stacja, PLAY/STOP, ikona głośnika, Volume i RSSI.
- SSD1306 128×64: stacja, artysta, utwór i dolna belka.
- ST7789 320×240: osobny layout wykorzystujący większy ekran.
- Zaprojektować wspólny Display interface, `NoDisplay` i capabilities; nie kopiować UI między ekranami 1:1.

## 20. Light sensor / autobrightness

**Priorytet: opcjonalnie, szczególnie dla SALON.**

- Dodać czujnik światła, filtrację i histerezę.
- Dodać auto brightness ON/OFF, minimum, maximum i ewentualny tryb dzień/noc.

## 21. Display / screensaver / sleep

**Priorytet: screensaver i sleep timer; deep sleep tylko przy realnej potrzebie.**

- Obsłużyć display ON/OFF, screensaver, idle przy STOP i PLAY, blank/nonblank, sleep timer, wake oraz audio podczas sleep.
- Deep sleep wdrożyć tylko, jeśli będzie rzeczywiście potrzebny.

## 22. MUTE / AMP_POWER

**Priorytet: szczególnie dla SALON.**

- Ustalić mute dla STOP, Volume 0 i sleep oraz unmute dla PLAY przy Volume > 0.
- Wprowadzić wspólny `AudioOutputState` i przyszły `AMP_POWER`.
- Ustalić kolejność AMP POWER/MUTE i opóźnienia eliminujące pyknięcia.

## 23. AUX

**Priorytet: później, jeśli wejście RCA będzie używane.**

- Rozważyć ADC I2S (np. PCM1808), I2S input, Source Manager, `has_aux` i gain/input level.
- Sprawdzić szumy.

## 24. SPDIF

**Priorytet: później, po BT.**

- Dodać SPDIF receiver, I2S input, source detection, Source Manager i `has_spdif`.

## 25. DLNA

**Priorytet: zdecydowanie później.**

- Przeanalizować istniejące `USE_DLNA`, SSDP/UPnP, ContentDirectory, wykrywanie serwera, browsing katalogów i pagination.
- Bibliotekę pobierać on-demand, nie przechowywać całości w RAM.
- Odtwarzać przez istniejący audio output; dodać WWW, LCD i Source Manager.

## 26. SALON_DSP / TDA7719

**Priorytet: po stabilnym SALON bez TDA.**

- Dodać profil `salon_dsp`, tor PCM5102A → TDA7719 → wzmacniacz, driver TDA7719 przez I²C GPIO7/8 i capability `has_tda`.
- Obsłużyć master Volume, EQ, Loudness, Balance, Fader, Subwoofer ON/OFF i level, tryb 2.0/2.1, presety i storage.
- Dodać WWW i LCD oraz przetestować analogowy tor audio.

## 27. DIN

**Priorytet: po SALON.**

- Ustalić finalny MCU i pinout; odblokować profil.
- Uruchomić PCM5102A, VoxOneBT, WWW, MQTT/HA i NoDisplay bez enkodera.
- Zbudować fizyczny prototyp.

## 28. Capabilities / architektura profili

**Priorytet: zasada obowiązująca w projekcie.**

- Utrzymywać jeden wspólny projekt dla DESK, DIN, SALON i SALON_DSP.
- Uzależniać UI i runtime od capabilities: display, encoder, VU, BT, AUX, SPDIF, TDA i local UI.
- Nie dodawać warunków `if profile == salon`, gdy decyzję można oprzeć na capability.

## 29. Backup / restore / recovery

**Priorytet: przed stabilnym/publicznym release.**

- Dodać backup i restore całej konfiguracji, walidację schema/version i recovery po nieudanym restore.
- Uwzględnić główny config, MQTT NVS, `stations.tsv` i `stations.idx` oraz bezpiecznie Wi-Fi.
- Zapewnić czytelny UX.

## 30. Bezpieczeństwo WWW

**Priorytet: przed publicznym stable.**

- Dodać admin authentication i zabezpieczyć Web Update, restart, Wi-Fi, factory reset, backup/restore oraz mutujące REST i WS.
- Sprawdzić CSRF, ekspozycję `wifi.csv`, legacy routes, `/upload`, emergency/WebBoard i logowanie haseł/API/WS.
- Nie wystawiać VoxOne bezpośrednio do Internetu.

## 31. Diagnostyka

**Priorytet: szczególnie przed diagnozowaniem SALON.**

- Dodać lekką diagnostykę na żądanie: reset reason, uptime, heap, RSSI, audio buffer, codec, bitrate, reconnect/underflow counters, decoder errors, Git SHA development build, profile i capabilities.
- Nie uruchamiać ciężkiego debugowania stale.

## 32. Alarm

**Priorytet: późniejsza funkcja użytkowa.**

- Dodać ON/OFF, HH:MM, dni tygodnia, wybór stacji, osobną Volume, fade-in, czas odtwarzania i integrację sleep/wake.

## 33. Cleanup yoRadio

**Priorytet: po osiągnięciu stabilności; bez agresywnego cleanupu.**

- Przeanalizować, czy niepotrzebne są: Adafruit seesaw, IR, VS1053, Nextion, SD, nieużywane sterowniki LCD, stare WWW/routes, legacy config fields i inne elementy yoRadio.
- Usuwać tylko elementy rzeczywiście niepotrzebne; zachować stabilne używane komponenty.

## 34. Dokumentacja

**Priorytet: sukcesywnie po większych etapach.**

- Aktualizować README, hardware profiles, pinout, capabilities, MQTT/HA, Web/API, Source Manager, VoxOneBT protocol, audio architecture, SALON, SALON_DSP oraz update/recovery.
- Nie organizować osobnego ogromnego documentation sprint bez potrzeby.

## 35. Branding / logo

**Priorytet: po uruchomieniu SALON i przed finalnym `v0.3.0`.**

- Zaprojektować finalne logo VoxOne w wersji pełnej, uproszczonej/ikony i monochromatycznej.
- Przygotować źródłowe SVG oraz PNG w kilku rozmiarach.
- Przygotować favicon/ikonę WWW i logo do README/GitHub.
- Sprawdzić logo w WWW, na ekranie startowym LCD, na SALON 480×320 i ewentualnie innych wyświetlaczach.
- Nie obciążać renderowania LCD dużą grafiką.
- Przechowywać wszystkie źródła brandingu w jednym miejscu, np. `assets/branding/`.

## 36. Release / stable

**Priorytet: przed właściwym `v0.3.0`.**

- Wykonać regression DESK, długie testy audio, MQTT/HA, playlisty, restarty, Wi-Fi i Web Update.
- Zweryfikować backup/restore, security, wspólny core także na SALON oraz czysty Git i aktualną dokumentację.
- Dopiero po tych kontrolach zmienić `VOXONE_VERSION` i przygotować `v0.3.0`.

## Poza bieżącym planem

- Native HA Discovery i własna integracja HA.
- SD card, SD-WEB, IR remote, pogoda i Telnet tylko dlatego, że występuje w yoRadio.
- Kopiowanie yoRadio 1:1 oraz agresywny cleanup przed stabilnością.
- Przenoszenie audio na osobny task/core bez pomiarów.
- TDA7719 w pierwszym SALON; AUX/SPDIF przed działającym RADIO+BT; DLNA przed Source Managerem.
- Deep sleep bez konkretnego zastosowania.
- Refaktor ScrollWidget/HOLD bez problemu, powrót do starego redesignu WWW ani przebudowa działającego Web Update dla porządku.

## Kolejność prac

1. Ustalić zachowanie startowe: źródło, stacja oraz GRAJ/STOP.
2. Wykonać krótki audit DESK.
3. Zrealizować SALON-HW-1.
4. Uruchomić RADIO, LCD, enkoder i PCM5102A na SALON.
5. Zintegrować VoxOneBT.
6. Dodać Source Manager RADIO/BT.
7. Dodać PLAY_MEDIA/TTS z BT.
8. Dodać AAC/AAC+.
9. Dodać M3U/PLS.
10. Dodać dalsze źródła według potrzeby: AUX/SPDIF/DLNA.
11. Dodać SALON_DSP/TDA7719.
12. Wykonać radio compatibility audit.
13. Zrealizować backup/security/hardening.
14. Przygotować branding po uruchomieniu SALON.
15. Przygotować finalny release.
