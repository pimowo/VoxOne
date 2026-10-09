# Inventory odziedziczonego yoRadio — PROJECT-CLEANUP-1A

Stan odniesienia: commit 4436dce4d71ad1bb7d3d9d9b785c2825a33ee086. Ten dokument klasyfikuje obecne użycie, nie usuwa kodu. KEEP oznacza potrzebny bez istotnej zmiany architektury; REFACTOR oznacza potrzebny, ale sprzężony z dawnymi profilami lub nazwami; REMOVE_LATER oznacza kandydat do późniejszego usunięcia po osobnej weryfikacji; UNKNOWN oznacza brak wystarczających dowodów.

| Moduł / plik | Obecna rola | VoxOne używa? | Przyszłość | Akcja |
|---|---|---|---|---|
| src/audioI2S/Audio.cpp, AudioEx.h | Strumień i dekodowanie radia przez I2S, współpraca z Player | Tak, aktualny PCM5102A | Zachować tor i wydzielić wybór backendu sprzętowego w późniejszym etapie | REFACTOR |
| src/audioVS1053/ | Alternatywny dekoder/wyjście VS1053 | Nie na x0/b0/a0: VS1053_CS=255, Player wybiera I2S | Zweryfikować użycie poza trzema targetami i zależności przed usunięciem | UNKNOWN |
| src/displays/displayST7789.*, displayST7796.* | Aktywne sterowniki LCD X0 i A0 | Tak | Zachować zachowanie; później podłączyć do DisplayManager | REFACTOR |
| src/displays/dspcore.h, widgets/, conf/, fonts/ | Wybór DspCore według DSP_MODEL, widżety i układy LCD | Tak | Oddzielić driver od renderera; utrzymać układy X0/A0 | REFACTOR |
| src/displays/displaySSD1306.* | Sterownik przewidziany w kodzie yoRadio | Nie w obecnych profilach | Kandydat do przyszłego runtime display po sprawdzeniu PCB | UNKNOWN |
| Pozostałe src/displays/display*.cpp i src/SSD1322/, src/ST7920/, src/LiquidCrystalI2C/, src/ILI9488/ | Warianty wyświetlaczy zależne od DSP_MODEL | Nie wybrane przez x0/b0/a0; część może być potrzebna do przyszłych LCD | Sprawdzić zależności i koszt buildów, nie usuwać na podstawie nazwy | UNKNOWN |
| src/displays/nextion.* | Legacy Nextion, kompilowane przy USE_NEXTION | Nie w obecnych profilach; Config i CLI nadal mają ślady kompatybilności | Ustalić, czy istnieje wspierane urządzenie/kontrakt | UNKNOWN |
| src/yoEncoder/ | Odczyt enkodera | Tak, przez controls.cpp | Zachować zachowanie gestów; później przekazać piny z deskryptora PCB | REFACTOR |
| src/OneButton/ | Obsługa przycisków/gestów | Tak, przez warstwę controls | Zachować timing i testy sterowania | KEEP |
| profiles/, myoptions.h | Wybór x0/b0/a0 i piny; myoptions.h ładuje profile.h | Tak, każdy build | Oddzielić PCB od runtime/capabilities w CLEANUP-1B i później | REFACTOR |
| src/core/options.h, DSP_MODEL, stare define | Domyślne piny i ścieżki wariantów yoRadio | Tak, szeroko w core i display | Zastępować stopniowo deskryptorem; DSP_MODEL oznacza LCD, nie audio DSP | REFACTOR |
| src/core/options.h: YOVERSION | Informacja o bazie yoRadio 0.9.720 | Tak: bootlog, Serial CLI, Nextion i zmienna yoRadioVersion WWW | Zachować atrybucję; później oddzielić ją od wersji produktu | REFACTOR |
| src/core/version.h: VOXONE_VERSION | Wersja produktu 0.2.0 | Tak: WWW, bootlog, User-Agent i nazwy artefaktów | Pozostawić osobnym źródłem prawdy VoxOne | KEEP |
| src/core/config.*, playlist_store.*, station_* | EEPROM, SPIFFS, stacje i migracje | Tak | Zachować format Config oraz dane użytkownika; refaktor niezależnie od relokacji | REFACTOR |
| src/core/netserver.*, data/www/, web-src/ | API, WS, Web Update, legacy i nowa strona WWW | Tak | Zachować kontrakty; osobno ocenić zbędne endpointy | REFACTOR |
| GET /legacy.html, stare statyczne strony w data/www | Kompatybilny interfejs yoRadio | Tak: trasy i zasoby są nadal serwowane | Osobny audyt klientów i recovery przed ewentualnym usunięciem | UNKNOWN |
| POST /upload | Dawny import playlist bez ID; kod odmawia nadpisania formatu v1 | Trasa nadal istnieje, lecz legacy import jest zablokowany | Kandydat do usunięcia dopiero po potwierdzeniu braku klientów | REMOVE_LATER |
| src/IRremoteESP8266/ | Duża biblioteka IR odziedziczona z yoRadio | Źródła wyklucza build_src_filter; controls.cpp nadal ma warunkowe include dla IR_PIN | Sprawdzić plan obsługi IR, zależności i licencję przed decyzją o usunięciu | UNKNOWN |
| src/GT911_Touchscreen/ i src/core/touchscreen.* | Opcjonalny dotyk | Nie w obecnych profilach: TS_MODEL_UNDEFINED | Ustalić przyszłe LCD/touch przed decyzją | UNKNOWN |
| src/pluginsManager/, src/plugins/ | Hooki rozszerzeń setup/runtime | Tak: main.cpp wywołuje pm.on_setup/on_end_setup | Zachować hooki; później ocenić API i potrzebne pluginy | REFACTOR |
| src/async-mqtt-client/ | Klient MQTT używany przez mqtt.cpp | Tak | Zachować do czasu osobnego audytu zależności | KEEP |
| Nazwy e2002/yoRadio w komentarzach i docs | Atrybucja i ślady pochodzenia kodu | Tak, dokumentacyjne | Zachować atrybucję; nie traktować komentarzy jako zbędnego kodu | KEEP |

Dowody użycia: platformio.ini wybiera trzy działające profile i wyklucza IRremoteESP8266; src/core/player.cpp wybiera AudioI2S, gdy VS1053_CS=255; src/core/display.cpp, src/displays/dspcore.h i widgets używają DSP_MODEL; src/core/controls.cpp używa enkodera; src/main.cpp wywołuje pluginsManager; src/core/netserver.cpp obsługuje /legacy.html oraz /upload; src/core/options.h zawiera YOVERSION. Żadna pozycja UNKNOWN ani REMOVE_LATER nie jest zgodą na usunięcie w tym etapie.

Wersje: VoxOne używa src/core/version.h; yoRadio ma YOVERSION w src/core/options.h; osobne repo VoxOneBT ma include/Version.h i własną wersję. CONFIG_VERSION i wersja protokołu UART są odrębne.

\n
