# Inventory odziedziczonego yoRadio — PROJECT-CLEANUP-1A

Stan odniesienia: commit 4436dce4d71ad1bb7d3d9d9b785c2825a33ee086. Ten dokument klasyfikuje obecne użycie, nie usuwa kodu. KEEP oznacza potrzebny bez istotnej zmiany architektury; REFACTOR oznacza potrzebny, ale sprzężony z dawnymi profilami lub nazwami; REMOVE_LATER oznacza kandydat do późniejszego usunięcia po osobnej weryfikacji; UNKNOWN oznacza brak wystarczających dowodów.

Aktualizacja audytu 2026-10-17: poniższa tabela zachowuje historyczny stan
checkpointu PROJECT-CLEANUP-1A. W aktualnym drzewie nie ma już katalogów
`src/audioVS1053`, `src/IRremoteESP8266`, `src/GT911_Touchscreen`,
`src/LiquidCrystalI2C`, `src/ILI9488`, `src/pluginsManager` ani `src/plugins`.
Nie należy na tej podstawie usuwać kolejnych bibliotek: bieżące `SSD1322` i
`ST7920` pozostają kodem odziedziczonym/przyszłym, niewłączonym w pięciu
profilach, a ich ewentualne usunięcie nadal wymaga osobnego audytu zależności.
Aktywne profile i capabilities opisuje teraz
[HARDWARE_TARGETS.md](HARDWARE_TARGETS.md), a otwarte decyzje wyłącznie
[TODO.md](TODO.md).

| Moduł / plik | Obecna rola | VoxOne używa? | Przyszłość | Akcja |
|---|---|---|---|---|
| src/audioI2S/Audio.cpp, AudioEx.h | Strumień i dekodowanie radia przez I2S, współpraca z Player | Tak, aktualny PCM5102A | Zachować tor i wydzielić wybór backendu sprzętowego w późniejszym etapie | REFACTOR |
| src/audioVS1053/ | Alternatywny dekoder/wyjście VS1053 | Nie na l0/m0/s0/din0/w0: VS1053_CS=255, Player wybiera I2S | Zweryfikować użycie poza aktywnymi targetami i zależności przed usunięciem | UNKNOWN |
| src/displays/displayST7789.*, displayST7796.* | Aktywne sterowniki LCD W0 i L0 | Tak | Zachować zachowanie; później podłączyć do DisplayManager | REFACTOR |
| src/displays/dspcore.h, widgets/, conf/, fonts/ | Wybór DspCore według DSP_MODEL, widżety i układy LCD | Tak | Oddzielić driver od renderera; utrzymać układy W0/L0 | REFACTOR |
| src/displays/displaySSD1306.* | Dawny sterownik jednego backendu OLED | Nieaktywny w M0/S0/DIN0: wybór SSD1306/SSD1309 obsługuje `displayOLED128x64Runtime.*` | Zweryfikować zależności przed decyzją o usunięciu dawnego sterownika | UNKNOWN |
| Pozostałe src/displays/display*.cpp i src/SSD1322/, src/ST7920/, src/LiquidCrystalI2C/, src/ILI9488/ | Warianty wyświetlaczy zależne od DSP_MODEL | Nie wybrane przez l0/m0/s0/din0/w0; część może być potrzebna do przyszłych LCD | Sprawdzić zależności i koszt buildów, nie usuwać na podstawie nazwy | UNKNOWN |
| src/displays/nextion.* | Legacy Nextion, kompilowane przy USE_NEXTION | Nie w obecnych profilach; Config i CLI nadal mają ślady kompatybilności | Ustalić, czy istnieje wspierane urządzenie/kontrakt | UNKNOWN |
| src/yoEncoder/ | Odczyt enkodera | Tak, przez controls.cpp | Zachować zachowanie gestów; później przekazać piny z deskryptora PCB | REFACTOR |
| src/OneButton/ | Obsługa przycisków/gestów | Tak, przez warstwę controls | Zachować timing i testy sterowania | KEEP |
| profiles/, myoptions.h | Wybór l0/m0/s0/din0/w0 i piny; myoptions.h ładuje profile.h | Tak, każdy build | Oddzielić PCB od runtime/capabilities w CLEANUP-1B i później | REFACTOR |
| src/core/options.h, DSP_MODEL, stare define | Domyślne piny i ścieżki wariantów yoRadio | Tak, szeroko w core i display | Zastępować stopniowo deskryptorem; DSP_MODEL oznacza LCD, nie audio DSP | REFACTOR |
| src/core/options.h: YOVERSION | Informacja o bazie yoRadio 0.9.720 | Tak: bootlog, Serial CLI, Nextion i zmienna yoRadioVersion WWW | Zachować atrybucję; później oddzielić ją od wersji produktu | REFACTOR |
| src/core/version.h: VOXONE_VERSION | Wersja produktu 0.8.0 | Tak: WWW, bootlog, User-Agent i nazwy artefaktów | Pozostawić osobnym źródłem prawdy VoxOne | KEEP |
| src/core/config.*, playlist_store.*, station_* | EEPROM, SPIFFS, stacje i migracje | Tak | Zachować format Config oraz dane użytkownika; refaktor niezależnie od relokacji | REFACTOR |
| src/core/netserver.*, data/www/, web-src/ | API, WS, Web Update, legacy i nowa strona WWW | Tak | Zachować kontrakty; osobno ocenić zbędne endpointy | REFACTOR |
| GET /legacy.html, stare statyczne strony yoRadio | Historyczny interfejs yoRadio | Nie: bieżące `data/www` nie zawiera tych zasobów, a serwer i polityka SECURITY nie rejestrują `/legacy.html` | Zachować opis wyłącznie jako historię migracji | RETIRED |
| POST /upload | Historyczny import playlist bez ID | Nie: bieżący kontrakt używa `POST /api/stations/import`; `/upload` nie jest zarejestrowaną trasą | Zachować informację dla analizy dawnych klientów | RETIRED |
| src/IRremoteESP8266/ | Duża biblioteka IR odziedziczona z yoRadio | Źródła wyklucza build_src_filter; controls.cpp nadal ma warunkowe include dla IR_PIN | Sprawdzić plan obsługi IR, zależności i licencję przed decyzją o usunięciu | UNKNOWN |
| src/GT911_Touchscreen/ i src/core/touchscreen.* | Opcjonalny dotyk | Nie w obecnych profilach: TS_MODEL_UNDEFINED | Ustalić przyszłe LCD/touch przed decyzją | UNKNOWN |
| src/pluginsManager/, src/plugins/ | Hooki rozszerzeń setup/runtime | Tak: main.cpp wywołuje pm.on_setup/on_end_setup | Zachować hooki; później ocenić API i potrzebne pluginy | REFACTOR |
| src/async-mqtt-client/ | Klient MQTT używany przez mqtt.cpp | Tak | Zachować do czasu osobnego audytu zależności | KEEP |
| Nazwy e2002/yoRadio w komentarzach i docs | Atrybucja i ślady pochodzenia kodu | Tak, dokumentacyjne | Zachować atrybucję; nie traktować komentarzy jako zbędnego kodu | KEEP |

Dowody użycia: platformio.ini definiuje pięć aktywnych profili i wyklucza IRremoteESP8266; src/core/player.cpp wybiera AudioI2S, gdy VS1053_CS=255; src/core/display.cpp, src/displays/dspcore.h i widgets używają DSP_MODEL; src/core/controls.cpp używa enkodera; src/main.cpp wywołuje pluginsManager; src/core/netserver.cpp i security_access_policy.cpp definiują bieżący HTTP/API; src/core/options.h zawiera YOVERSION. Żadna pozycja UNKNOWN ani REMOVE_LATER nie jest zgodą na usunięcie w tym etapie.

Wersje: VoxOne używa src/core/version.h; yoRadio ma YOVERSION w src/core/options.h; osobne repo VoxOneBT ma include/Version.h i własną wersję. CONFIG_VERSION i wersja protokołu UART są odrębne.

\n
