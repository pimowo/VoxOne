# VoxOne 1.0.0 — kontrakt techniczny

Stan dokumentu: zakres sprzętowy zamrożony decyzją
`VOXONE-STABLE-S0-L0-SCOPE-FREEZE-1`. Dokument opisuje bieżącą implementację,
zatwierdzony cel S0+VoxOneBT i granice pierwszego Stable. Kanoniczna lista
otwartych prac pozostaje wyłącznie w [TODO.md](TODO.md).

Status wydania: **RC1 12/12 PASS (2026-11-06)**. Finalny closeout obejmuje
końcowe fizyczne HIL L0 i S0 oraz kontrolowane testy OTA MAIN, OTA SPIFFS,
rollback, Recovery, trwałości NVS i Balance. Wcześniejsze opisy PENDING i
PARTIAL w sekcjach historycznych dokumentują stan pośrednich checkpointów;
nie zmieniają finalnego statusu bramek poniżej.

## Zakres wydania

Technical Stable 1.0.0 oznacza zweryfikowane firmware, dane WWW, formaty
persistence, instalację i recovery dla zatwierdzonych profili. Nie oznacza
automatycznie gotowości do sprzedaży ani publicznej dystrybucji.

Commercial Release jest osobnym etapem. W szczególności wymaga jawnej decyzji
o licencji projektu i o ewentualnej licencji offline powiązanej z ESP. Taki
mechanizm nie istnieje w bieżącym firmware i nie należy do zakresu Technical
Stable 1.0.0.

Zatwierdzony zakres Technical Stable 1.0.0 obejmuje obowiązkowo dwa profile
fizyczne: `s0` i `l0`, oba z VoxOneBT. Każdy musi uzyskać udokumentowany
fizyczny HIL PASS swojej bazowej konfiguracji z macierzy A przed wydaniem;
sam build nie spełnia tej bramki, a test innego profilu nie kwalifikuje S0
ani L0. Profile `w0`, `m0` i `din0` pozostają wspieranymi targetami
repozytorium, standardowych buildów, full-image i testów offline, lecz ich
fizyczny HIL nie blokuje pierwszego Stable. Wszystkie wcześniejsze wyniki HIL
i liczniki zachowują historyczny zakres i nie są przepisywane.

Po checkpointach macierzy audio i wyświetlaczy bieżący kod udostępnia więcej
wariantów runtime niż historycznie zatwierdzony baseline sprzętowy Stable.
Poniższe macierze rozdzielają dwa obowiązkowe profile fizyczne, zachowane
targety buildowe i rozszerzone capability obecne w BIN. Stable 1.0.0 wymaga
fizycznej kwalifikacji części release-gating macierzy A. Warianty ponad
baseline pozostają zaimplementowane i wybieralne w WWW oraz objęte testami
offline i buildami, lecz mają status `OPTIONAL /
PHYSICAL HIL NOT QUALIFIED`; nie blokują wydania i nie mogą być deklarowane
jako fizycznie zweryfikowane bez odpowiedniego dowodu. Istniejące fizyczne
PASS zachowują ważność wyłącznie w swoim udokumentowanym zakresie.

S0 opisuje własny, ręcznie okablowany ESP32-S3 Zero i zachowuje historyczną
mapę GPIO rewizji 0 dla OLED, local audio i input. Kandydat S0 deklaruje
`VOXONE_HAS_BT=1`: UART1 używa RX44/TX43, a I2S1 RX używa
BCLK11/WS12/DATA13. Logi aplikacji korzystają z native USB CDC, aby nie
uruchamiać UART0 na współdzielonych padach GPIO43/44. S0 zachowuje
VU wyłączone i aktualizuje VoxOneBT przez osobne USB; MAIN→BT update
pozostaje wyłączone. L0
zachowuje istniejącą PCB V-Tom, hardware, capability i GPIO. DIN0 zachowuje
mapę GPIO M0 jako własny pinout rewizji 0. Przyszłe dedykowane PCB otrzymają
nazwy S1 i DIN1 i nie nadpiszą historii rewizji 0.

Po wydaniu Stable 1.0.0 profil W0 przechodzi do statusu LEGACY. Pozostaje w
repozytorium i w standardowych buildach; dopuszczone jest utrzymanie poprawek
krytycznych dla wydanej linii 1.0.0, lecz W0 nie otrzymuje nowych funkcji w
następnej linii rozwojowej.

Dokładny pinout, pamięć, capability i stan testów opisuje
[HARDWARE_TARGETS.md](HARDWARE_TARGETS.md).

Historyczny czysty checkpoint
`3bad2c3d27c24888d2731510cf330f866bef4ac3`, oznaczony tagiem
`voxone-rc1-tts-stall-d-closure-1`, pozostaje bazą wcześniejszych dowodów.
Checkpoint `RC1-S0-BT-IMPLEMENT-1` dodaje minimalną capability S0+VoxOneBT;
jego pełny verify ma 160 native PASS, transport strict 16/16, ASan/UBSan
PASS, buildy i full-image W0/M0/L0/S0/DIN0 5/5 oraz production SECURITY
offline PASS. Wynik offline nie zastępuje fizycznych bramek S0 ani L0.

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
Discovery mDNS nie stanowi kanału sterowania ani autoryzacji. Integracja Home
Assistant używa chronionego WebSocketu i tej samej polityki SECURITY co WWW.

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
`reset` zostały usunięte z ogólnego dispatchera WS.

Kryterium P1 dla AsyncTCP OOM akceptuje deterministyczną iniekcję wszystkich
ścieżek alokacji w testach offline pod ASan/UBSan oraz nieinwazyjny HIL L0.
Nie wymaga fizycznego wymuszania OOM ani test-only fault-injection firmware na
produkcyjnym urządzeniu. Historyczny closeout na L0 `4ef1e53` potwierdził TCP
RST dla ówczesnej ścieżki `_lagging` oraz, oddzielnie, prawidłową ramkę CLOSE
1013 globalnego overloadu; zachowuje PASS wyłącznie w tym zakresie. Bieżący
checkpoint programowy kolejkuje per-client CLOSE 1013, czeka ograniczone 5 s
na dostarczenie i handshake, a potem dopuszcza wymuszone zamknięcie TCP jako
fail-safe. Ma SOFTWARE PASS, lecz fizyczny HIL musi jeszcze potwierdzić odbiór
i odpowiedź peer dla tej nowej ramki; sam fallback TCP nie jest 1013 PASS.

## Home Assistant

Integracja Home Assistant używa WebSocketu `GET /ws` do sterowania i TTS oraz
Zeroconf `_voxone._tcp.local.` do wykrywania. Po włączeniu
`haDiscoveryEnabled` urządzenie publikuje usługi `_http._tcp` i
`_voxone._tcp` na porcie 80. TXT zawiera `mac`, `name`, `model`, `version`,
`host`, `security` i `manufacturer`. Wyłączenie flagi zachowuje rozwiązywanie
hosta `<hostname>.local`, ale nie publikuje usług discovery.

Firmware nie zawiera klienta MQTT, konfiguracji brokera ani API MQTT. Stare
dane w historycznym namespace `voxmqtt` nie są odczytywane; factory reset
usuwa je wyłącznie jako bezpieczne czyszczenie po aktualizacji.

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
jego PCM przez I2S. S0 nie ma odrębnego forka tej logiki: używa wspólnego
CORE SourceManager/BtLink/BtAudioInput na UART1 RX44/TX43 i I2S1 RX
BCLK11/WS12/DATA13. Aplikacyjne logi S0 korzystają z natywnego USB CDC,
więc globalna logika `Serial` innych profili pozostaje bez zmian.
A2DP audio state
decyduje, czy BT może posiadać I2S0;
AVRCP służy do transportu i UI. Snapshot obejmuje online/connected, firmware,
nazwę, capabilities, peer, metadata, playback, sample rate i raw VU. Codec i
bitrate A2DP nie są publikowane bez wiarygodnych danych. L0 zachowuje
istniejącą aktualizację modułu jako osobny upload. S0 nie udostępnia
MAIN→BT update i aktualizuje VoxOneBT przez jego osobne USB. Żadna z tych
operacji nie jest MAIN OTA; szczegóły L0 zawiera
[BT_FIRMWARE_SENDER.md](BT_FIRMWARE_SENDER.md).

Fizyczny test LG TV potwierdził prawidłowe audio A2DP oraz rzeczywistą pauzę
obrazu, licznika i audio, ale MAIN nie zawsze odzwierciedla ten stan jako
`PAUSED`. Przyczyna nie została jednoznacznie ustalona i nie jest przypisywana
LG TV, VoxOneBT ani MAIN. To jawne ograniczenie jest zaakceptowane dla Stable
1.0.0 i samo nie blokuje wydania; nie wymaga kolejnej sesji LG TV bez nowej
poprawki lub konkretnej hipotezy. Nie zwalnia z pozostałych kryteriów BT
lifecycle, SourceManager ani TTS.

## TTS / temporary audio

Temporary audio przyjmuje pojedynczy URL; nowe żądanie zastępuje oczekujące.
Na czas komunikatu Player przejmuje wyjście, nie zmieniając bazowego źródła ani
RADIO PLAY/STOP intent. Po końcu, błędzie albo timeout przywrócenie jest
jednorazowe i zależy od aktualnego źródła, sieci i blokad systemowych. Jeżeli
BT zniknie podczas TTS, komunikat nie jest przerywany, a restore RADIO jest
stłumiony tak, aby po zakończeniu pozostało fizyczne STOP bez autoplay.
Timeout transportu temporary audio jest timeoutem postępu, a nie całkowitego
czasu komunikatu: każde odebranie bajtów HTTP rozpoczyna nowe okno 10 s, a
kompletne ramki już obecne w buforze pozwalają dokończyć odtwarzanie. Brak
postępu po opróżnieniu bufora albo rozłączenie transportu kończy bieżącą
generację przez tę samą jednorazową ścieżkę cleanup. Krótsze, istniejące limity
połączenia TCP i nagłówków HTTP pozostają bez zmian.
Zaawansowana kolejka, priorytety, ducking i własne tryby głośności pozostają
POST-STABLE.

Fizyczny `VOXONE-RC1-TTS-STALL-D-HIL-L0-RETEST-2` zamknął scenariusz D na
L0: pełne `3648044/3648044 B`, `watchdogTimeouts +0`, `transportErrors +0`,
`naturalEofs +1`, `cleanups +1`, `endReason=natural_eof`, `state=cleaned` i
READY 231/231. Dowód nie zamyka całej grupy TTS: regresje A/B/C na docelowym
kandydacie oraz pełny HIL TTS S0+BT pozostają otwarte.

## Web Update, recovery i OTA rollback

`POST /update` rozróżnia MAIN i SPIFFS, sprawdza nagłówek, deklarowany rozmiar,
limit partycji i pełny rozmiar SPIFFS. Podczas update obowiązuje jeden globalny
lock, audio jest zatrzymywane, a HTTP/WS są wygaszane przed restartem.
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
`nvs.net80211`, `voxhw`, legacy `voxmqtt`, `voxui`, `voxscroll`, `voxsecurity`,
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

Bieżący backup `.vxcf` schema 2 obejmuje Config v7, runtime hardware,
timeouty UI, ustawienia przewijania, `/data/wifi.csv` i
`/data/stations.tsv`; restore zachowuje zgodność ze starszym schema 1. Restore
najpierw waliduje całość, staging i marker transakcji, następnie commit,
weryfikację i dopiero wtedy restart. Backup nie zawiera aktywnych sesji,
transient MUTE, chwilowego źródła i playback, pełnego NVS, slotów aplikacji,
`otadata`, eFuse ani firmware. Rekord SECURITY nie jest częścią `.vxcf`.
Historyczny 388-bajtowy slot integracji pozostaje w formacie wyłącznie dla
zgodności offsetów: nowy backup zapisuje w nim zera, a restore ignoruje jego
zawartość i nie odtwarza poświadczeń MQTT.

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

Proponowana zasada do zatwierdzenia: w 1.0.0 jedyną aktywną strefą jest
`Europe/Warsaw`; pola legacy są opaque/reserved i nigdy nie sterują zegarem.
Przyszłe API może jawnie publikować `timezone="Europe/Warsaw"` oraz bieżący
lokalny offset UTC w minutach, liczony z reguł CET/CEST. Propozycja nie zmienia
serialized layout ani bieżącego firmware.

## Macierz kwalifikacji i zachowanych profili buildowych

To jest skondensowany opis zakresu, nie druga lista zadań; źródłem prawdy
pozostaje TODO. `NONE` oznacza zapisany wariant bez zainstalowanego lokalnego
modułu, a nie osobny fizyczny model urządzenia.

### A. Profile, baseline i rola w wydaniu

| Profil | Domyślny/bieżący sprzęt | Rola w Stable 1.0.0 | Minimalny fizyczny zakres |
|---|---|---|---|
| W0 | PCM5102A; ST7789 284×76; EC11 | `RETAINED BUILD / PHYSICAL HIL NON-BLOCKING` | Zachować build, full-image, testy offline i historyczne dowody. Uszkodzone USB nadal blokuje bezpieczny opcjonalny HIL/OTA W0, ale nie wydanie. |
| M0 | PCM5102A; SSD1306/SSD1309 128×64; EC11 | `RETAINED BUILD / PHYSICAL HIL NON-BLOCKING` | Zachować build, full-image, testy offline i wcześniejsze PASS; bez wymogu końcowego profilowego HIL. |
| L0 | istniejąca PCB V-Tom; ESP32-S3 N16R8; PCM5102A; ST7796S; EC11; DS3231; VoxOneBT; VU | **`RELEASE-GATING HIL PASS 2026-11-06`** | Końcowy HIL potwierdził domyślny TFT/audio, BT/SourceManager/TTS, Config, update/recovery i Balance. Hardware i GPIO pozostają bez zmian; XSMT nie należy do baseline. |
| S0 | ESP32-S3 Zero 4 MiB/2 MiB; MAX98357A MONO, SSD1306 128×32, EC11 i VoxOneBT | **`RELEASE-GATING HIL PASS 2026-11-06`** | Końcowy HIL potwierdził bazowy S0+VoxOneBT. Zachowane GPIO: I2S0 `BCLK5/DOUT6/WS4`, input `A9/B8/KEY7`, I2C `SDA1/SCL2`, UART1 `RX44/TX43` i I2S1 RX `BCLK11/WS12/DATA13`; VU i MAIN→BT update pozostają wyłączone. |
| DIN0 | PCM5102A; SSD1306/SSD1309 128×64; EC11; pinout M0 | `RETAINED BUILD / PHYSICAL HIL NON-BLOCKING` | Zachować build, full-image, testy offline, własną identity i historię rewizji 0; fizyczny HIL nie blokuje wydania. |

### B. Rozszerzone capability obecne w `3bad2c3`

| Profil | Dodatkowe display runtime ponad historyczny baseline | Dodatkowe audio runtime | Stan kwalifikacji |
|---|---|---|---|
| W0 | Brak nowych wariantów; ST7789/NONE bez zmian | Brak | Zachowany build; bez rozszerzenia funkcjonalnego. |
| M0 | SSD1306 128×32, SH1106 128×64, CH1116G 128×64 | MAX98357A MONO, MAX98357A STEREO, `NONE` | Implemented/offline PASS; fizyczny HIL nieblokujący. |
| L0 | SSD1306 128×32/128×64, SSD1309 128×64, SH1106 128×64, CH1116G 128×64, `NONE` | `NONE` | Implemented/offline PASS; domyślny ST7796 zachowany; warianty dodatkowe `OPTIONAL / PHYSICAL HIL NOT QUALIFIED`. |
| S0 | SSD1309 128×64, SH1106 128×64, CH1116G 128×64 | PCM5102A, MAX98357A STEREO, `NONE` | Implemented/offline PASS; minimalny BT audio ma software PASS, fizyczny HIL PENDING. Warianty dodatkowe `OPTIONAL / PHYSICAL HIL NOT QUALIFIED`. |
| DIN0 | SSD1306 128×32, SH1106 128×64, CH1116G 128×64 | Brak; tylko PCM5102A | Implemented/offline PASS; fizyczny HIL nieblokujący. |

Wszystkie profile, które w runtime wybierają `OLED128x64`, używają jednego
wspólnego renderera CORE niezależnie od profilu sprzętowego. STARTUP zachowuje
pasma `y=0..31` (logo VoxOne), `y=32..47` (animacja) i `y=48..63` (SSID bez
hasła). PLAYER zachowuje trzy przewijane wiersze stacja/artysta/utwór oraz
dolny pasek z rzeczywistym PLAY/PAUSE/STOP, wyśrodkowanym źródłem, ikoną
głośnika zgodną z OLED 128×32, głośnością i Wi-Fi. Napis `BT` jest biały, bez
tła i obramowania, wyśrodkowany pomiędzy głośnością a Wi-Fi oraz widoczny
wyłącznie zgodnie z istniejącym stanem i capability BT. VOL pokazuje `0–100%`
albo `MUTE`. Wspólny renderer nie ujednolica sterowników: SSD1306, SSD1309,
SH1106 i CH1116G zachowują odrębne backendy i wymagają własnej kwalifikacji
fizycznej. Kontrakt nie zmienia OLED 128×32 ani TFT.

Wszystkie profile, które w runtime wybierają `OLED128x32`, używają jednego
wspólnego renderera CORE. STARTUP zachowuje pasma `y=0..15` (wyśrodkowany
bitmapowe logo OLED32 75×16 px), `y=16..23` (animacja Wi-Fi) i `y=24..31`
(SSID bez hasła).
PLAYER używa stacji w komórce 12×8 px umieszczonej w paśmie `y=0..11`,
wspólnego tekstu `Artysta - Utwór` w komórce 6×8 px w `y=14..21` oraz
wspólnej belki `y=24..31`. Dwa odstępy mają dokładnie po 2 px. Belka zachowuje
PLAY/PAUSE/STOP, wyśrodkowane źródło, geometrię ikony głośnika zgodną z OLED
128×64, głośność, capability-gated biały napis BT bez tła i obramowania oraz
Wi-Fi. VOL pokazuje `0–100%` albo `MUTE`. Odrębny widok `BT_TRANSPORT` pokazuje
przewijany artysta w `y=0..11`, przewijany utwór w `y=14..21` i sterowanie
`< PLAY/PAUSE/STOP >` w `y=24..31`; korzysta z tej samej maszyny wejścia CORE
co OLED 128×64. Ten kontrakt nie zmienia renderera OLED 128×64 ani TFT.

Pełny `tools/verify.ps1` na integracyjnym SHA
`02285e26f315eefd8bc30dd17151351296416eb1` ma PASS: 161 testów native,
strict transport 16/16, ROM 26/26, ASan/UBSan, pięć standardowych profili
i pięć full-image, regresja full-image validation 10/10, L0 TX-only, WWW
repeatability oraz `git diff --check`.

### C. Optional HIL — dodatkowe warianty

Fizyczny HIL wariantów z macierzy B jest opcjonalny dla Stable 1.0.0. Brak
takiego testu nie blokuje profilowego PASS S0/L0, jeżeli ich zatwierdzony
baseline spełnił kryteria bramki. Obecność opcji w WWW, test offline albo
poprawny build nie są dowodem fizycznego działania. Raport wydania zachowuje
status `OPTIONAL / PHYSICAL HIL NOT QUALIFIED` oraz zakres każdego wcześniej
uzyskanego fizycznego PASS.

Formalna Definition of Stable dla RC1 liczy **12 głównych bramek**: dwa
profilowe HIL (S0 i L0) oraz dziesięć grup wspólnych: RADIO endurance; BT
lifecycle; SourceManager switching/reconnect; TTS restore; firmware i SPIFFS
update; recovery; Config v7 persistence; L/R i VU; brak otwartych P0/P1;
końcowy PASS na zamrożonym release commicie.

| Nr | Bramka RC1 | Status 2026-11-06 | Dowód closeoutu |
|---:|---|---|---|
| 1 | `RC1-PROFILE-S0` | **PASS** | Końcowy fizyczny HIL S0+VoxOneBT. |
| 2 | `RC1-PROFILE-L0` | **PASS** | Końcowy fizyczny HIL L0+VoxOneBT. |
| 3 | `RC1-RADIO-ENDURANCE` | **PASS** | Terminalny endurance i końcowe HIL profili. |
| 4 | `RC1-BT-LIFECYCLE` | **PASS** | Lifecycle BT w końcowym HIL L0/S0. |
| 5 | `RC1-SOURCE-MANAGER` | **PASS** | RADIO/BT, disconnect/reconnect i intencje źródła w końcowym HIL. |
| 6 | `RC1-TTS-RESTORE` | **PASS** | Restore TTS na wymaganych profilach; zachowane scenariusze A/B/C/D. |
| 7 | `RC1-UPDATE` | **PASS** | Kontrolowane OTA MAIN i OTA SPIFFS. |
| 8 | `RC1-RECOVERY` | **PASS** | Kontrolowany rollback i Recovery. |
| 9 | `RC1-CONFIG-V7` | **PASS** | Trwałość Config v7 i danych NVS. |
| 10 | `RC1-LR-VU` | **PASS** | Balance/L/R/VU na L0; VU S0 poza zakresem. |
| 11 | `RC1-NO-P0-P1` | **PASS** | Brak otwartych release-blocking P0/P1. |
| 12 | `RC1-FROZEN-RELEASE` | **PASS** | Zamrożony commit, finalne obrazy OTA MAIN i tag `v1.0.0`. |

Wynik końcowy: **RC1 12/12 PASS**. S0 ma VU wyłączone zgodnie z kontraktem.

Historyczny kontrakt 3 + 10 liczył 13 bramek łącznie. Późniejszy kontrakt
5 + 10 liczył 15 bramek, a zapis `13/15 OPEN` oznaczał 13 otwartych spośród
15. Wyników i liczników tych okresów nie zmieniamy. Kanoniczne identyfikatory,
bieżące stany i pełna mapa numeracji są utrzymywane w [TODO.md](TODO.md).

## Granica POST-STABLE

Pierwszego Stable nie rozszerzają: DLNA, AUX/PCM1808, Multiroom, native Home
Assistant, W5500, DSPmini/AutoEQ, menu lokalne, power/standby/alarm, Installer,
nowe PCB L1/M1/S1/DIN1 ani backendy display niewystępujące w macierzy
`3bad2c3`. Jedyną zatwierdzoną nową capability niezbędną przed Stable jest
VoxOneBT dla historycznego S0 rev. 0, w minimalnym zakresie UART/I2S1,
SourceManager i TTS, bez VU i bez MAIN→BT update. Już zaimplementowane
rozszerzenia z macierzy B pozostają
`OPTIONAL / PHYSICAL HIL NOT QUALIFIED` zgodnie z zatwierdzonym zakresem HIL.
`SSD1322_256X64` pozostaje zatwierdzonym zadaniem POST-STABLE z dokładnie
uzgodnionym PLAYER i dwoma poziomymi segmentowymi paskami VU L/R; katalog WWW
nie oznacza, że backend istnieje w bieżącym firmware.
