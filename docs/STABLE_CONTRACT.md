# VoxOne 1.0.0 — kontrakt techniczny

Stan dokumentu: baseline `STABLE-DOCS-CONTRACT-1`, zakres sprzętowy
zaktualizowany w `STABLE-SCOPE-5TARGETS-1` na bazie commita
`deeb6961814bd8a0477873d6e420a359891b888a`. Dokument opisuje bieżącą
implementację i granice pierwszego Stable. Kanoniczna lista otwartych prac
pozostaje wyłącznie w [TODO.md](TODO.md).

## Zakres wydania

Technical Stable 1.0.0 oznacza zweryfikowane firmware, dane WWW, formaty
persistence, instalację i recovery dla zatwierdzonych profili. Nie oznacza
automatycznie gotowości do sprzedaży ani publicznej dystrybucji.

Commercial Release jest osobnym etapem. W szczególności wymaga jawnej decyzji
o licencji projektu i o ewentualnej licencji offline powiązanej z ESP. Taki
mechanizm nie istnieje w bieżącym firmware i nie należy do zakresu Technical
Stable 1.0.0.

Zatwierdzony zakres Technical Stable 1.0.0 obejmuje obowiązkowo pięć profili:
`w0`, `m0`, `l0`, `s0` i `din0`. Każdy z nich musi uzyskać osobny fizyczny
HIL PASS przed wydaniem; sam build ani uruchomienie obrazu S0/DIN0 na sprzęcie
M0 nie spełnia tej bramki. Zakres Stable obejmuje capability faktycznie
zadeklarowane przez bieżący profil. Elementy wymienione wyłącznie w roadmapie
rodziny nie stają się przez tę decyzję wymaganiami ani działającymi funkcjami.

S0 i DIN0 świadomie używają w rewizji 0 tej samej mapy GPIO co M0. Jest to
zatwierdzony kontrakt tych historycznych profili, a nie tymczasowy błąd do
poprawienia przed Stable. Przyszłe dedykowane PCB otrzymają nazwy S1 i DIN1,
własne definicje pinoutu i capability oraz nie nadpiszą S0/DIN0. Projekt i HIL
S1/DIN1 są POST-STABLE i nie blokują wydania zatwierdzonych rewizji 0.

Dokładny pinout, pamięć, capability i stan testów opisuje
[HARDWARE_TARGETS.md](HARDWARE_TARGETS.md).

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
| `GET /api/security/state` | Publiczny stan bootstrapu SECURITY; zawiera `state`, `setupAllowed`, `loginAllowed`, `authenticated`, `protectionEnforced`. |
| `POST /api/security/setup`, `/login`, `/logout` | Formularz security setup/login/logout; Host i same-origin są walidowane. Setup jest dozwolony tylko dla `legacy-unclaimed`. |
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

Akceptacja `hostname.local` przez politykę Host nie gwarantuje obecnie
rozwiązywania tej nazwy: publikowanie mDNS nie jest aktywne w bieżącej ścieżce
startowej firmware.

Bieżący stan produkcyjny pozostaje observe-only: zwykłe profile mają
`VOXONE_SECURITY_HTTP_ENFORCEMENT=0`, a API raportuje
`protectionEnforced=false`. Tylko `m0-security-test` włącza testową bramkę.
Produkcji nie wolno włączyć przed HIL PBKDF2, HTTP/WS, migracji i recovery.
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

## MQTT i Home Assistant

MQTT jest opcjonalnym bieżącym kontraktem pierwszego Stable. Konfiguracja
obejmuje enabled, host, port, username, hasło oraz root topic. Root może być
ustawiony jawnie albo automatycznie na podstawie pełnego MAC. Po uruchomieniu
klient używa czterech tematów:

| Sufiks | Kierunek i dane |
|---|---|
| `command` | Subscribe QoS 2. Przyjmuje krótkie komendy sterujące, `volm`, `volp`, `turnon`, `turnoff`, `vol <n>`, `play <n>` oraz dłuższy URL jako temporary audio/TTS. |
| `status` | Retained JSON: stan play, numer/nazwa stacji, metadata title oraz legacy `on=config.store.dspon`. |
| `volume` | Retained poziom w kompatybilnej skali MQTT. |
| `playlist` | Retained URL HTTP do `/data/stations.tsv`. |

MQTT przestaje reconnectować i zamyka TCP przed restartem po update. Bieżąca
integracja Home Assistant to odziedziczony kontrakt MQTT `ha_yoradio`; native
HA, `_voxone._tcp`, config flow i wersjonowany model encji pozostają
POST-STABLE. Znaczenie legacy pola `status.on` wymaga HIL i nie jest w tym
checkpointcie zmieniane.

## RADIO, SourceManager i Bluetooth

RADIO istnieje na wszystkich profilach i odtwarza przez I2S/PCM5102A w
bieżących BIN. Obsługiwane ścieżki obejmują MP3, AAC/AAC+, FLAC i M4A/AAC,
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

Backup `.vxcf` schema 1 obejmuje Config v7, runtime hardware, MQTT, timeouty
UI, `/data/wifi.csv` i `/data/stations.tsv`. Restore najpierw waliduje całość,
staging i marker transakcji, następnie commit, weryfikację i dopiero wtedy
restart. Backup nie zawiera aktywnych sesji, transient MUTE, chwilowego źródła
i playback, pełnego NVS, slotów aplikacji, `otadata`, eFuse ani firmware.
Rekord SECURITY nie jest częścią `.vxcf`.

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

## Macierz kwalifikacji pięciu profili

To jest skondensowany widok bramki wydania, nie druga lista zadań; źródłem
prawdy pozostaje TODO.

| Profil | Kwalifikowany baseline bieżącego BIN | Rzeczywisty brak / wymagany dowód | Bramka wydania |
|---|---|---|---|
| W0 | ESP32 bez PSRAM, PCM5102A, ST7789 284×76, EC11/Buttons3/NONE, RADIO/WWW/MQTT/update | Full image/first boot/AP/Wi-Fi/WWW/RADIO/audio/display/input/restart; PBKDF2 bez PSRAM; migracja 0.2.0/0.8.0, OTA/config, MAIN/SPIFFS/recovery/power-loss, Config v7, playlisty i ScrollWidget. OTA rollback wyłącznie po osobnej zgodzie i asekuracji. | Fizyczny W0 PASS; brak otwartych regresji profilu. |
| M0 | ESP32-S3 4 MiB + 2 MiB PSRAM, PCM5102A, SSD1306/SSD1309 128×64/NONE, EC11/Buttons3/NONE | Aktualny baseline na urządzeniu; UART uzbrojony przed restartem; OTA health/rollback z `/api/ready`, bootId i UART; SECURITY OPEN/PROTECTED; Config v7; RADIO oraz wszystkie wspierane warianty display/input. | Fizyczny M0 PASS; OTA i SECURITY HIL PASS. |
| L0 | ESP32-S3 16 MiB + PSRAM, PCM5102A, ST7796S, DS3231, VoxOneBT UART/I2S, VU, EC11/Buttons3/NONE | RADIO endurance i kodeki/playlisty; telefon/LG TV, A2DP/AVRCP, metadata/VU/poziomy; SourceManager/TTS, L/R, MQTT/HA, LCD/input, XSMT po przypisaniu GPIO, update/recovery/config i negatywne ścieżki VoxOneBT. | Fizyczny L0 PASS; endurance i pełna macierz BT/audio PASS. |
| S0 | ESP32-S3 4 MiB + 2 MiB PSRAM; zatwierdzony pinout rewizji 0 współdzielony z M0: PCM5102A `1/2/3`, OLED I2C `7/8`, EC11/Buttons3 `5/6/4`, display/input NONE | Brakuje fizycznego PASS profilu S0 na odpowiednim sprzęcie rewizji 0: first boot, RADIO/audio, oba OLED, wejścia, WWW, config, update/recovery i OTA. MAX98357A, OLED 128×32, bateria, BT i RTC nie są capability bieżącego BIN. | Fizyczny S0 PASS; test obrazu na urządzeniu oznaczonym jako M0 nie jest dowodem kwalifikacji S0. S1 nie blokuje tej bramki. |
| DIN0 | ESP32-S3 4 MiB + 2 MiB PSRAM; zatwierdzony pinout rewizji 0 współdzielony z M0: PCM5102A `1/2/3`, OLED I2C `7/8`, EC11/Buttons3 `5/6/4`, display/input NONE | Brakuje fizycznego PASS profilu DIN0 na odpowiednim sprzęcie rewizji 0: first boot, RADIO/audio, oba OLED, wejścia, WWW, config, update/recovery i OTA. OLED 128×32 i VoxOneBT nie są capability bieżącego BIN. | Fizyczny DIN0 PASS; test obrazu na urządzeniu oznaczonym jako M0 nie jest dowodem kwalifikacji DIN0. DIN1 nie blokuje tej bramki. |

Każdy z pięciu profili wymaga własnego, udokumentowanego fizycznego HIL PASS.
Głębokie scenariusze wspólne — walidacja błędnego obrazu, abort, power-loss,
recovery, rollback i persistence — można wykonać pełną macierzą na
reprezentatywnym profilu danej klasy MCU, a na pozostałych potwierdzić ścieżkę
podstawową oraz różnice MCU, pamięci, partycji i capability, chyba że TODO
wymaga jawnie pełnego przebiegu na każdym profilu.

## Granica POST-STABLE

Pierwszego Stable nie rozszerzają: DLNA, AUX/PCM1808, Multiroom, native Home
Assistant, W5500, DSPmini/AutoEQ, menu lokalne, power/standby/alarm, Installer,
nowe PCB L1/M1/S1/DIN1 ani dodatkowe backendy display. `SSD1322_256X64`
pozostaje zatwierdzonym zadaniem POST-STABLE z dokładnie uzgodnionym PLAYER i
dwoma poziomymi segmentowymi paskami VU L/R; katalog WWW nie oznacza, że
backend istnieje w bieżącym firmware.
