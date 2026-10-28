# VoxOne 1.0.0 — kontrakt techniczny

Stan dokumentu: baseline `STABLE-DOCS-CONTRACT-1`, zakres sprzętowy pięciu
profili potwierdzony w `VOXONE-RC1-S0-CONTRACT-AND-CHECKPOINT-1`. Dokument
opisuje bieżącą implementację i granice pierwszego Stable. Kanoniczna lista
otwartych prac pozostaje wyłącznie w [TODO.md](TODO.md).

## Zakres wydania

Technical Stable 1.0.0 oznacza zweryfikowane firmware, dane WWW, formaty
persistence, instalację i recovery dla zatwierdzonych profili. Nie oznacza
automatycznie gotowości do sprzedaży ani publicznej dystrybucji.

Commercial Release jest osobnym etapem. W szczególności wymaga jawnej decyzji
o licencji projektu i o ewentualnej licencji offline powiązanej z ESP. Taki
mechanizm nie istnieje w bieżącym firmware i nie należy do zakresu Technical
Stable 1.0.0.

Zatwierdzony zakres Technical Stable 1.0.0 obejmuje obowiązkowo pięć profili:
`w0`, `m0`, `l0`, `s0` i `din0`. Każdy z nich musi uzyskać udokumentowany
fizyczny HIL PASS swojej bazowej konfiguracji z macierzy A przed wydaniem;
sam build nie spełnia tej bramki, a test firmware `m0` nie kwalifikuje obrazu
`s0` ani `din0`. Dopuszczenie stanowiska M0 do HIL obrazu DIN0 pozostaje
osobną decyzją. Elementy wymienione wyłącznie w roadmapie rodziny nie stają
się przez tę decyzję wymaganiami ani działającymi funkcjami.

Po checkpointach macierzy audio i wyświetlaczy bieżący kod udostępnia więcej
wariantów runtime niż historycznie zatwierdzony baseline sprzętowy Stable.
Poniższe macierze rozdzielają obowiązkowe minimum profilu od rozszerzonych
capability już obecnych w BIN. Stable 1.0.0 wymaga fizycznej kwalifikacji
macierzy A. Warianty ponad baseline pozostają zaimplementowane i wybieralne w
WWW oraz objęte testami offline i buildami, lecz mają status `OPTIONAL /
PHYSICAL HIL NOT QUALIFIED`; nie blokują wydania i nie mogą być deklarowane
jako fizycznie zweryfikowane bez odpowiedniego dowodu. Istniejące fizyczne
PASS zachowują ważność wyłącznie w swoim udokumentowanym zakresie.

S0 opisuje własny, ręcznie okablowany ESP32-S3 Zero z zatwierdzoną mapą GPIO
rewizji S0. DIN0 zachowuje mapę GPIO M0 jako pinout rewizji DIN0. Przyszłe
dedykowane PCB otrzymają nazwy S1 i DIN1, własne definicje pinoutu i capability
oraz nie nadpiszą S0/DIN0. Projekt i HIL S1/DIN1 są POST-STABLE i nie blokują
wydania zatwierdzonych rewizji 0.

Po wydaniu Stable 1.0.0 profil W0 przechodzi do statusu LEGACY. Pozostaje w
repozytorium i w standardowych buildach; dopuszczone jest utrzymanie poprawek
krytycznych dla wydanej linii 1.0.0, lecz W0 nie otrzymuje nowych funkcji w
następnej linii rozwojowej.

Dokładny pinout, pamięć, capability i stan testów opisuje
[HARDWARE_TARGETS.md](HARDWARE_TARGETS.md).

Aktualny czysty checkpoint dokumentacyjnego punktu odniesienia to
`e1c746e3295e9db0682d23f30cd99b33dbb1a64a`; produkcyjna implementacja jest
niezmieniona względem `d6896005e10dfb0098b1e8c831b1b73506ef5b3b`, a nowszy
commit dodaje wyłącznie regresje WebSocket. Niezależny clean verify z
2026-10-28 ma 156/156 native PASS, ASan/UBSan PASS, RADIO strict 16/16,
buildy W0/M0/L0/S0/DIN0 5/5, full-image i WWW PASS, L0 TX-only PASS oraz
production SECURITY offline PASS. Żaden obraz `e1c746e` nie został fizycznie
wdrożony; wynik offline nie zastępuje żadnej bramki HIL.

## HTTP i API

Serwer działa przez HTTP. HTTPS nie jest obecnie terminowany przez urządzenie.
Odpowiedzi API bezpieczeństwa, gotowości i konfiguracji używają `no-store`.
Po włączeniu enforcement centralna polityka rozpoznaje wyłącznie poniższe
trasy i metody; nieznana trasa kończy się `404`, znana trasa ze złą metodą
`405`.

| Metoda i trasa | Kontrakt |
|---|---|
| `GET /`, zasoby `voxone.*`, `advanced-audio.js`, `dsp-client.js`, logo i favicon | Bootstrap i aplikacja WWW. Bezpośrednie żądania plików `*.gz` są odrzucane; kompresję obsługuje statyczny handler. |
| `GET /variables.js` | Tożsamość wersji, kanału, buildu, profilu i trybu WWW. Trasa chroniona po enforcement. |
| `GET /api/ready` | Publiczna diagnostyka: `ready`, losowy dla bootu `bootId` oraz obiekt `ota`. Sam HTTP 200 nie oznacza akceptacji obrazu `PENDING_VERIFY`, jeżeli `ready=false`. |
| `GET /api/security/state` | Publiczny stan bootstrapu SECURITY; zawiera `state`, `setupAllowed`, `loginAllowed`, `authenticated`, rzeczywiste `protectionEnforced` oraz `enforcementMode` (`disabled`, `test`, `production`). |
| `POST /api/security/setup`, `/login`, `/logout`, `/manage` | Formularze security setup/login/logout oraz zarządzania ochroną; Host i same-origin są walidowane. Setup jest dozwolony tylko dla `legacy-unclaimed`; `manage` włącza hasło w OPEN, a jego zmiana lub wyłączenie w PROTECTED wymaga ważnej sesji. |
| `GET /api/stations`, `/api/directory/search`, `/api/stations/export` | Odczyt stacji, Radio Directory i eksport. |
| `POST /api/stations/import`, `/add`, `/edit`, `/delete`, `/reorder`, `/metadata` | Mutacje listy stacji. Import jest uploadem multipart i ma osobną walidację formatu. |
| `GET/POST /api/mqtt` | Odczyt i zapis bieżącej konfiguracji MQTT. API nigdy nie zwraca zapisanego hasła; raportuje jedynie, czy hasło istnieje. |
| `GET/POST /api/hardware` | Bieżąca konfiguracja runtime oraz zapis wspieranych wartości. Element `planned` w katalogu roadmapy nie staje się przez to selectable ani obsługiwany. Zapis wymagający zastosowania zwraca informację o restarcie. |
| `GET /api/config/backup`, `POST /api/config/restore` | Strumieniowy backup `.vxcf` oraz transakcyjny restore z walidacją CRC, capability i kontrolowanym restartem dopiero po commit. |
| `GET /api/time`, `POST /api/time/sync` | Lokalny czas systemu/RTC, interwały synchronizacji i ręczne żądanie NTP. Na profilu z RTC sync zwraca `202`, a zakończenie potwierdza wzrost `syncCount`; bez capability RTC żądanie zwraca `404 rtc_unsupported`. |
| `GET /api/bt/update`, `POST /update/bt` | Tylko profil z capability aktualizacji VoxOneBT, obecnie L0. Status i osobny upload obrazu modułu. |
| `GET /api/dsp/state` | Tylko L0; bieżący stan demonstracyjnego transportu DSP, nie kontrakt przyszłego DSPmini. |
| `POST /update` | Upload dokładnie jednego obrazu MAIN albo SPIFFS. Obraz jest walidowany przed zapisem; sukces planuje kontrolowany restart. |
| `GET /update.html`, `GET /emergency` | Recovery/update UI, także przy niedostępnym normalnym WWW. |
| `GET /data/stations.tsv` | Bieżąca lista stacji/playlist URL, chroniona po enforcement. |
| `GET /ws` | Jeden WebSocket runtime i sterowania opisany niżej. |

Provisioning Wi-Fi używa `GET/POST /` wyłącznie w stanie `unconfigured` i w
trybie AP/recovery. Zapis poświadczeń planuje restart. Legacy `/legacy.html`
i `POST /upload` nie są trasami bieżącego kontraktu.

## SECURITY i sesje administratora

Stany trwałe to `unconfigured`, `legacy-unclaimed`, `protected`, `open` i
`locked`. Rekord SECURITY ma własny namespace NVS, wersję, generation i CRC.
Hasło nie jest przechowywane jawnie: verifier ma PBKDF2-HMAC-SHA256, sól 16 B,
wynik 32 B i obecnie 10 000 iteracji. Hasło administratora ma minimum cztery
znaki Unicode i maksimum 64 bajty UTF-8.

Sesje są wyłącznie w RAM i znikają po restarcie. Istnieją maksymalnie dwie
sesje, z timeoutem bezczynności 8 h i absolutnym 24 h. Po pięciu błędach w
oknie 5 min logowanie jest blokowane na 60 s. Cookie `voxone_session` ma 128
bitów losowych zakodowanych jako 32 znaki hex oraz atrybuty `Path=/`,
`HttpOnly`, `SameSite=Strict`, `Max-Age=86400`. Nie ma `Secure`, ponieważ
urządzenie obsługuje obecnie HTTP, nie HTTPS.

Host jest ograniczony do lokalnego IP urządzenia, skonfigurowanego hostname
albo `hostname.local`, opcjonalnie z portem 80. Mutacje i handshake WebSocket
wymagają zgodnego `Origin: http://<Host>`. Zmiana trybu/hasła lub generation,
logout i wygaśnięcie unieważniają autoryzację; aktywny chroniony WebSocket jest
zamykany kodem 1008.

Normalny panel SYSTEM udostępnia włączenie opcjonalnego hasła, jego zmianę i
wyłączenie. Przejście OPEN → PROTECTED nie wymaga wcześniejszej sesji; każda
mutacja stanu PROTECTED wymaga ważnej sesji administratora. Udana operacja
unieważnia wszystkie sesje RAM i wymusza ponowne ustalenie dostępu.

Akceptacja `hostname.local` przez politykę Host nie gwarantuje obecnie
rozwiązywania tej nazwy: publikowanie mDNS nie jest aktywne w bieżącej ścieżce
startowej firmware.

Standardowe profile W0, M0, L0, S0 i DIN0 włączają
`VOXONE_SECURITY_HTTP_ENFORCEMENT=1`, dlatego API raportuje
`protectionEnforced=true` oraz `enforcementMode=production`. Odrębny
`m0-security-test` włącza wyłącznie flagę testową i raportuje
`protectionEnforced=true`, `enforcementMode=test`; nie jest to ochrona
produkcyjna. Jednoczesne włączenie flag production i test oraz próba wyłączenia
production w standardowym profilu są błędami kompilacji. Flagi są wyłącznie
build-time i nie mogą być zmieniane przez klienta.

Aktualizacja istniejącego urządzenia nie modyfikuje rekordu SECURITY,
konfiguracji ani danych użytkownika. Zapisane stany OPEN i PROTECTED pozostają
odpowiednio otwarte lub chronione. Starsze urządzenie z Wi-Fi, ale bez rekordu
SECURITY, przechodzi do `legacy-unclaimed`: publiczny bootstrap WWW pozwala
jawnie wybrać OPEN albo ustawić hasło, a dotychczasowa trasa Web Update
pozostaje dostępna; chronione API i WebSocket nie są w tym stanie otwierane.
MQTT zachowuje osobną politykę i nie korzysta z cookie HTTP.

## WebSocket

Jeden endpoint `GET /ws` obsługuje WWW. Klient wysyła kompletne tekstowe ramki
w formacie `command=value`; binarne, fragmentowane lub niekompletne ramki są
odrzucane. `ping` odpowiada `{"pong":1}`. Snapshoty są JSON i obejmują m.in.
`webStatus`, bieżące źródło, playback, metadata, codec/bitrate/sample rate,
stan VoxOneBT, dostępne źródła, `volume100` i `muted`. Osobne snapshoty
publikują system/build/profile/capabilities, sieć, ekran, RSSI, volume i listę
stacji.

Przy enforcement handshake musi być poprawnym WebSocket v13 do `/ws`, mieć
pojedyncze wymagane nagłówki, poprawny klucz, dozwolony Host/Origin i ważną
sesję w trybie `protected`. Subprotocol może być pominięty; jeśli klient go
wysyła, jedyną dopuszczalną wartością jest `voxone.v1`. Komendy przechodzą
przez jawną allowlistę. Legacy komendy destrukcyjne `format`, `clearspiffs` i
`reset` nie należą do allowlisty chronionego WS.

Kryterium P1 dla AsyncTCP OOM akceptuje deterministyczną iniekcję wszystkich
ścieżek alokacji w testach offline pod ASan/UBSan oraz nieinwazyjny HIL L0.
Nie wymaga fizycznego wymuszania OOM ani test-only fault-injection firmware na
produkcyjnym urządzeniu. HIL nadal musi rzeczywiście potwierdzić `_lagging`,
osobne zamknięcie przeciążonego klienta kodem 1013, disconnect/RST podczas
broadcastu, reconnect z pełnym GETINDEX oraz stabilne READY, bootId i heap bez
panic, WDT ani restartu. Do wykonania tego HIL wynik pozostaje PENDING.

## MQTT i Home Assistant

MQTT jest opcjonalnym bieżącym kontraktem pierwszego Stable. Konfiguracja
obejmuje enabled, host, port, username, hasło oraz root topic. Root może być
ustawiony jawnie albo automatycznie na podstawie pełnego MAC. Po uruchomieniu
klient używa czterech tematów:

| Sufiks | Kierunek i dane |
|---|---|
| `command` | Subscribe QoS 2. Przyjmuje krótkie komendy sterujące, `volm`, `volp`, `turnon`, `turnoff`, `vol <n>`, `play <n>` oraz dłuższy URL jako temporary audio/TTS. |
| `status` | Retained JSON: `status=1` wyłącznie dla rzeczywistego `PLAYING`, poza nim `status=0`; numer/nazwa stacji, metadata title oraz niezależne legacy `on=config.store.dspon`. `on` nie jest synonimem odtwarzania. |
| `volume` | Retained poziom w kompatybilnej skali MQTT. |
| `playlist` | Retained URL HTTP do `/data/stations.tsv`. |

MQTT przestaje reconnectować i zamyka TCP przed restartem po update. Bieżąca
integracja Home Assistant to odziedziczony kontrakt MQTT `ha_yoradio`; native
HA, `_voxone._tcp`, config flow i wersjonowany model encji pozostają
POST-STABLE. Legacy `turnoff` nadal wyłącza DSP/display, zatrzymuje player i
zachowuje `smartstart`; `turnon` włącza DSP/display i uruchamia PLAY tylko przy
`smartstart=1`. Fizyczna regresja MQTT/HA pozostaje wymagana przed Stable.

## RADIO, SourceManager i Bluetooth

RADIO istnieje na wszystkich profilach i odtwarza przez wybrane lokalne
wyjście I2S. Domyślnie W0/M0/L0/DIN0 używają PCM5102A, a S0 MAX98357A MONO;
runtime `NONE` wyłącza lokalne I2S tam, gdzie profil je udostępnia.
Obsługiwane ścieżki obejmują MP3, AAC/AAC+, FLAC i M4A/AAC,
HTTP/HTTPS, ICY metadata, M3U/PLS i redirect; pełna macierz realnych streamów
pozostaje HIL.

SourceManager ma dwa źródła bazowe: RADIO oraz Bluetooth, przy czym Bluetooth
jest dostępny tylko wtedy, gdy capability i runtime VoxOneBT są dostępne.
Wybór ręczny RADIO nie jest cofany przez już istniejące połączenie BT; nowy
edge połączenia BT może wybrać BT. Utrata połączenia lub runtime VoxOneBT
przy aktywnym BT przełącza na RADIO. Zachowany RADIO PLAY intent pozwala
wznowić stację, a RADIO STOP intent nie powoduje autoplay.

L0 komunikuje się z osobnym modułem VoxOneBT przez UART protocol 2 i odbiera
jego PCM przez I2S. A2DP audio state decyduje, czy BT może posiadać I2S0;
AVRCP służy do transportu i UI. Snapshot obejmuje online/connected, firmware,
nazwę, capabilities, peer, metadata, playback, sample rate i raw VU. Codec i
bitrate A2DP nie są publikowane bez wiarygodnych danych. Aktualizacja modułu
jest osobnym uploadem i nie jest MAIN OTA; szczegóły zawiera
[BT_FIRMWARE_SENDER.md](BT_FIRMWARE_SENDER.md).

## TTS / temporary audio

Temporary audio przyjmuje pojedynczy URL; nowe żądanie zastępuje oczekujące.
Na czas komunikatu Player przejmuje wyjście, nie zmieniając bazowego źródła ani
RADIO PLAY/STOP intent. Po końcu, błędzie albo timeout przywrócenie jest
jednorazowe i zależy od aktualnego źródła, sieci i blokad systemowych. Jeżeli
BT zniknie podczas TTS, komunikat nie jest przerywany, a restore RADIO jest
stłumiony tak, aby po zakończeniu pozostało fizyczne STOP bez autoplay.
Zaawansowana kolejka, priorytety, ducking i własne tryby głośności pozostają
POST-STABLE.

## Web Update, recovery i OTA rollback

`POST /update` rozróżnia MAIN i SPIFFS, sprawdza nagłówek, deklarowany rozmiar,
limit partycji i pełny rozmiar SPIFFS. Podczas update obowiązuje jeden globalny
lock, audio jest zatrzymywane, a MQTT/HTTP/WS są wygaszane przed restartem.
Błąd lub abort wykonuje cleanup, nie uruchamia automatycznie audio i próbuje
odzyskać filesystem. `update.html` przekierowuje do normalnego WWW, gdy jest
ono dostępne; w przeciwnym razie używa osadzonego formularza emergency.

SECURITY Recovery jest wybierane wyłącznie przy starcie: aktywny LOW na
profilowym wejściu KEY po 50 ms debounce uruchamia tryb Recovery, a HIGH
kontynuuje zwykły start. Stan jest próbkowany jednokrotnie przy starcie.
Przycisk enkodera i zworka do GND mają tę samą semantykę tego samego wejścia;
pin i wymagany zewnętrzny pull-up wynikają z capability profilu (M0 używa
GPIO4).

Recovery uruchamia ekskluzywny, otwarty AP `VoxOne-Recovery-XXXXXX` i panel
`http://192.168.4.1/`. Nie uruchamia RADIO, Bluetooth ani normalnego API, OTA i
WebSocket. Dopuszczone są tylko `GET /`, `POST /password` i
`POST /factory-reset`; pozostałe trasy są odrzucane. Host, Origin, metoda,
Content-Type, rozmiar i tempo żądań są sprawdzane, a mutacje mają ukryty token
CSRF. AP jest otwarty, a panel nie wymaga dodatkowego poświadczenia Recovery.
Ekran, jeśli działa, pokazuje jedynie nazwę produktu, tryb, SSID i adres
panelu, a jego awaria nie blokuje AP.

`POST /password` ustawia nowe hasło (4–64 według kontraktu SECURITY), także
przełączając OPEN → PROTECTED, używa istniejącego hashowanego rekordu i
unieważnia sesje; po trwałym zapisie urządzenie automatycznie się restartuje.
`POST /factory-reset` kasuje dane użytkownika z namespaces `eeprom`,
`nvs.net80211`, `voxhw`, `voxmqtt`, `voxui`, `voxscroll`, `voxsecurity`,
`voxrestore`, `voxfirstboot` i `voxupdate` oraz pliki Wi-Fi, playlist/stacji i
stagingu restore w SPIFFS.
Weryfikacja wymaga pustych danych użytkownika i zachowanego
`/www/assets.vxwm`; firmware, bootloader, tablica partycji, `otadata`, oba sloty
OTA, namespace systemowy `vox_ota` i fabryczne WWW nie są kasowane ani
formatowane. Dopiero po udanej weryfikacji zapisywany jest jednorazowy marker
FIRST BOOT i następuje restart. Marker jest konsumowany przed odczytem KEY,
więc pierwszy start po resecie omija aktywny KEY; kolejne starty znów go
respektują. Błąd kasowania, weryfikacji lub markera nie jest sukcesem i nie
planuje restartu.

MAIN OTA używa dwóch równych slotów `app0`/`app1` i `otadata`. Obraz
`PENDING_VERIFY` nie jest automatycznie akceptowany przez Arduino: projektowy
health gate autonomicznie wymaga poprawnej konfiguracji i danych, wymaganych
usług, żywej pętli głównej oraz lokalnego control plane: uruchomionego HTTP z
potwierdzonym STA albo działającym Recovery AP. Wszystkie warunki muszą być
stabilne przez pełne 5 s. Utrata lokalnego control plane zeruje to okno i
pozwala czekać na jego powrót do limitu 180 s; Internet, dostępność stacji
radiowej, klient WWW i wywołanie `/api/ready` nie są warunkami zatwierdzenia.
Błąd krytyczny powoduje żądanie rollbacku, a brak zdrowego control plane do
końca limitu powoduje rollback z przyczyną `timeout`; powód jest zapisywany w
NVS. `/api/ready` pozostaje diagnostyką, a jego obiekt `ota` zawiera
`runningSlot`, `bootSlot`, `imageState`, `pending`, `rollbackAvailable`,
`health`, `reason`, `lastFailure` i `elapsedMs`. Wyłącznie izolowany,
destrukcyjny profil `m0-ota-rollback-test` celowo czeka na pierwszy probe
`/api/ready`, aby następnie wstrzyknąć testową awarię `runtime_service`.

Software checkpoint OTA ma PASS, lecz całość pozostaje HIL PENDING. Obecny
zachowany runtime M0 pochodzi sprzed tego checkpointu i nie jest dowodem
działania health gate. Full image, jego przeznaczenie i granice opisuje
[FULL_IMAGE.md](FULL_IMAGE.md).

## Config v7 i persistence

Config v7 jest jawnym 255-bajtowym formatem little-endian zakończonym CRC-32;
nie jest surowym dumpem struktury C++. Migracje v5 i v6 zachowują historyczny
layout, normalizują dane w RAM i zapisują v7 kontrolowanie. `lastUserVolume`,
maximum/startup volume oraz `btEnabled` należą do v7. Runtime hardware ma
osobny 10-bajtowy rekord z wersją i CRC. SECURITY używa osobnego rekordu NVS.

Bieżący backup `.vxcf` schema 2 obejmuje Config v7, runtime hardware, MQTT,
timeouty UI, ustawienia przewijania, `/data/wifi.csv` i
`/data/stations.tsv`; restore zachowuje zgodność ze starszym schema 1. Restore
najpierw waliduje całość, staging i marker transakcji, następnie commit,
weryfikację i dopiero wtedy restart. Backup nie zawiera aktywnych sesji,
transient MUTE, chwilowego źródła i playback, pełnego NVS, slotów aplikacji,
`otadata`, eFuse ani firmware. Rekord SECURITY nie jest częścią `.vxcf`.

## Kontrakt strefy czasowej — stan i propozycja

Stan bieżący:

- firmware zawsze wywołuje `configTzTime()` z
  `CET-1CEST,M3.5.0/2,M10.5.0/3`, czyli Europe/Warsaw z automatycznym DST;
- WWW jawnie pokazuje `Europe/Warsaw` i nie oferuje wyboru strefy;
- `/api/time` zwraca lokalne `systemTime` i `rtcTime`, ale bez pola identyfikującego
  strefę lub offset;
- `tzHour`, `tzMin` i `timezoneOffset` pozostają w Config v7 wyłącznie dla
  zgodności layoutu; stare komendy CLI/WS mogą je zapisać, ale nie zmieniają
  aktywnej strefy;
- `Config::getTimezoneOffset()` zwraca obecnie stałe `0` bez opisanego znaczenia.

Proponowana zasada do zatwierdzenia: w 1.0.0 jedyną aktywną strefą jest
`Europe/Warsaw`; pola legacy są opaque/reserved i nigdy nie sterują zegarem.
`getTimezoneOffset()` powinno po późniejszej, osobnej zmianie oznaczać bieżący
lokalny offset UTC w minutach, liczony z reguł CET/CEST, a API powinno jawnie
publikować `timezone="Europe/Warsaw"` oraz ten offset. Do chwili wdrożenia kod
nie powinien być używany jako źródło offsetu. Propozycja nie zmienia
serialized layout ani bieżącego firmware.

## Macierze kwalifikacji pięciu profili Stable

To jest skondensowany opis zakresu, nie druga lista zadań; źródłem prawdy
pozostaje TODO. `NONE` oznacza zapisany wariant bez zainstalowanego lokalnego
modułu, a nie osobny fizyczny model urządzenia.

### A. Baseline Stable — obowiązkowe minimum kwalifikacji

| Profil | Domyślny sprzęt | Pozostały wcześniej zatwierdzony baseline runtime | Minimalny fizyczny zakres bramki profilu |
|---|---|---|---|
| W0 | PCM5102A; ST7789 284×76; EC11 | display/input `NONE`, Buttons3; RADIO/WWW/MQTT/update | Full image/first boot, RADIO/audio/display/input, PBKDF2 bez PSRAM, Config v7, MAIN/SPIFFS, recovery/power-loss i migracja. W0 pozostaje obowiązkowy; uszkodzone USB i brak recovery są blokerem. |
| M0 | PCM5102A; SSD1306 128×64; EC11 | SSD1309 128×64 i display `NONE`; Buttons3/input `NONE` | Finalny build, RADIO/audio/display/input, SECURITY, Config v7, update/rollback i recovery. Zachowane PASS M0 pozostają ważne w swoim zakresie. |
| L0 | PCM5102A; ST7796S 480×320; EC11; DS3231; VoxOneBT/VU | Buttons3/input `NONE` | Domyślny TFT/audio, BT/SourceManager/TTS, SECURITY, Config, update/recovery i otwarte P1. XSMT ma status `DECISION REQUIRED`. |
| S0 | MAX98357A MONO; SSD1306 128×32; EC11 | SSD1306 128×64 i display `NONE`; Buttons3/input `NONE` | First boot, RADIO/audio, oba baseline OLED, wejścia, WWW, Config, update/recovery na fizycznym S0. GPIO: I2S `5/6/4`, input `9/8/7`, I2C `1/2`. |
| DIN0 | PCM5102A; SSD1306 128×64; EC11 | SSD1309 128×64 i display `NONE`; Buttons3/input `NONE` | Obraz DIN0, tożsamość profilu, first boot, RADIO/audio/OLED/input, Config, SECURITY, update/recovery. GPIO współdzielone z M0: I2S `1/2/3`, input `5/6/4`, I2C `7/8`. |

### B. Rozszerzone capability obecne w `e1c746e`

| Profil | Dodatkowe display runtime ponad baseline A | Dodatkowe audio runtime ponad baseline A | Stan kwalifikacji |
|---|---|---|---|
| W0 | Brak nowych wariantów; ST7789/NONE bez zmian | Brak | W0 nie otrzymał rozszerzenia funkcjonalnego. |
| M0 | SSD1306 128×32, SH1106 128×64, CH1116G 128×64 | MAX98357A MONO, MAX98357A STEREO, `NONE` | Implemented/offline PASS; `OPTIONAL / PHYSICAL HIL NOT QUALIFIED`. |
| L0 | SSD1306 128×32/128×64, SSD1309 128×64, SH1106 128×64, CH1116G 128×64, `NONE` | `NONE` | Implemented/offline PASS; domyślny ST7796 zachowany; `OPTIONAL / PHYSICAL HIL NOT QUALIFIED`. |
| S0 | SSD1309 128×64, SH1106 128×64, CH1116G 128×64 | PCM5102A, MAX98357A STEREO, `NONE` | Implemented/offline PASS; fizyczny S0 potwierdza tylko wcześniejszy baseline 128×32/MAX98357A MONO/EC11; rozszerzenia `OPTIONAL / PHYSICAL HIL NOT QUALIFIED`. |
| DIN0 | SSD1306 128×32, SH1106 128×64, CH1116G 128×64 | Brak; tylko PCM5102A | Implemented/offline PASS; rozszerzenia `OPTIONAL / PHYSICAL HIL NOT QUALIFIED`; profilowy baseline HIL DIN0 pozostaje wymagany. |

### C. Optional HIL — dodatkowe warianty

Fizyczny HIL wariantów z macierzy B jest opcjonalny dla Stable 1.0.0. Może
być wykonywany i dokumentowany osobno, ale brak takiego testu nie blokuje
profilowego PASS, jeżeli kompletna macierz A danego profilu spełniła kryteria
bramki. Obecność opcji w WWW, test offline albo poprawny build nie są dowodem
fizycznego działania. Raport wydania musi zachować jawny status `OPTIONAL /
PHYSICAL HIL NOT QUALIFIED` dla każdego nieprzetestowanego wariantu oraz
zachować zakres każdego wcześniej uzyskanego fizycznego PASS.

**DECISION REQUIRED — DIN0:** bieżący zapis wymaga osobnego fizycznego HIL
DIN0 i mówi, że samo uruchomienie obrazu na M0 nie zamyka bramki. Ponieważ
DIN0 ma identyczne GPIO, flash/PSRAM i bazowe komponenty jak M0, możliwe jest
technicznie wykonanie HIL obrazu `din0` na wspólnym stanowisku M0 z jawnym
potwierdzeniem identity `DIN0`. Uznanie takiego przebiegu za profilowy PASS
wymaga zmiany interpretacji kontraktu i zgody użytkownika; test firmware M0
nigdy nie zastępuje testu firmware DIN0.

Formalna Definition of Stable dla RC1 liczy **15 głównych bramek**: pięć
profilowych HIL (W0, M0, L0, S0, DIN0) oraz dziesięć niezmienionych grup wspólnych:
RADIO endurance; BT telefon/LG TV; SourceManager switching/reconnect; TTS
restore; firmware i SPIFFS update; recovery; Config v7 persistence; L/R i VU;
brak otwartych P0/P1; końcowy PASS na zamrożonym release commicie. Częściowy
test nie zmniejsza licznika bramek.

Kanoniczne stabilne identyfikatory, bieżące stany oraz mapa historycznych
numerów Gate 5/6 są utrzymywane wyłącznie w [TODO.md](TODO.md). Numery nie są
częścią kontraktu API i nie powinny być używane jako jedyny identyfikator w
nowych raportach.

S0 i DIN0 wymagają własnego, udokumentowanego fizycznego HIL PASS w ramach
Stable 1.0.0. Dodatkowe capability niewłączone w bieżących profilach oraz
późniejsze S1/DIN1 pozostają POST-STABLE.

## Granica POST-STABLE

Pierwszego Stable nie rozszerzają: DLNA, AUX/PCM1808, Multiroom, native Home
Assistant, W5500, DSPmini/AutoEQ, menu lokalne, power/standby/alarm, Installer,
nowe PCB L1/M1/S1/DIN1 ani backendy display niewystępujące w macierzy
`e1c746e`. Już zaimplementowane rozszerzenia z macierzy B pozostają
`OPTIONAL / PHYSICAL HIL NOT QUALIFIED` zgodnie z zatwierdzonym zakresem HIL.
`SSD1322_256X64` pozostaje zatwierdzonym zadaniem POST-STABLE z dokładnie
uzgodnionym PLAYER i dwoma poziomymi segmentowymi paskami VU L/R; katalog WWW
nie oznacza, że backend istnieje w bieżącym firmware.
