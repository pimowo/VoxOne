# Instalacja, aktualizacja i odzyskiwanie VoxOne

Ta instrukcja opisuje bieżący, zaimplementowany przepływ dla profili `w0`,
`m0`, `l0`, `s0` i `din0`. Stable 1.0.0 nie został jeszcze wydany, a kompletna
kwalifikacja wydania dotyczy W0, M0 i L0. S0 i DIN0 pozostają buildowalne i
zachowują tę samą procedurę instalacji, lecz ich HIL jest POST-STABLE. Aktualny
stan HIL znajduje się w [HARDWARE_TARGETS.md](HARDWARE_TARGETS.md) i
[STABLE_CONTRACT.md](STABLE_CONTRACT.md).

## Pliki instalacyjne

Artefakty zawsze muszą pochodzić z tej samej wersji i z właściwego profilu:

| Plik | Zastosowanie |
|---|---|
| `VoxOne-<wersja>-full.bin` | Pierwsze programowanie czystego ESP32 albo pełne odzyskiwanie przez USB od adresu `0x0`. |
| `VoxOne-<wersja>-firmware.bin` | Aktualizacja MAIN w normalnym panelu WWW. |
| `VoxOne-<wersja>-spiffs.bin` | Aktualizacja zasobów WWW/SPIFFS w normalnym panelu WWW. |

Plik `full.bin` zawiera bootloader, tablicę partycji, dane startowe OTA,
firmware oraz SPIFFS. Jest jedynym plikiem potrzebnym do pierwszego
programowania, ale zapisuje cały zadeklarowany obszar flash. Nie wolno używać
go w Web Update.

Artefakty można zbudować poleceniem:

```sh
pio run -e <profil> -t fullimage
```

gdzie `<profil>` to `w0`, `m0`, `l0`, `s0` albo `din0`. Target najpierw
buduje firmware i SPIFFS, następnie scala pełny obraz, sprawdza jego rozmiar
i porównuje każdy osadzony fragment bajt po bajcie. Wyniki znajdują się w
`.pio/build/<profil>/`.

Wszystkie profile używają tablicy `partitions_yoradio.csv`: NVS zaczyna się
pod `0x9000`, `otadata` pod `0xe000`, sloty `app0` i `app1` pod `0x10000` i
`0x1f0000`, a SPIFFS pod `0x3d0000`. Układ zajmuje pierwsze 4 MiB. Obrazy
M0/S0/DIN0 mają 4 MiB. L0 ma obraz 16 MiB, chociaż bieżące partycje zajmują
tylko pierwsze 4 MiB. W0 używa definicji płytki `esp32dev`; przed zapisem
trzeba potwierdzić, że fizyczny moduł ma co najmniej 4 MiB flash.

## Pierwsze programowanie przez USB

Pełne programowanie jest operacją destrukcyjną. Jeżeli urządzenie było już
używane, najpierw pobierz backup `.vxcf`; pamiętaj jednak, że nie jest on
pełnym obrazem flash i nie zawiera ustawienia hasła WWW.

1. Wybierz `full.bin` dokładnie dla profilu urządzenia.
2. Podłącz stabilne zasilanie i przewód USB z transmisją danych.
3. Wprowadź płytkę w tryb bootloadera zgodnie z instrukcją jej producenta.
4. Zapisz pełny obraz od adresu `0x0` narzędziem Espressif `esptool`.
5. Po poprawnym zapisie uruchom urządzenie normalnie i wykonaj FIRST BOOT.

Przykład dla W0:

```sh
python -m esptool --chip esp32 --port <PORT> --baud 921600 \
  --before default_reset --after hard_reset write_flash -z \
  0x0 .pio/build/w0/VoxOne-<wersja>-full.bin
```

Przykład dla profilu ESP32-S3 (`m0`, `s0`, `din0` albo `l0`):

```sh
python -m esptool --chip esp32s3 --port <PORT> --baud 921600 \
  --before default_reset --after hard_reset write_flash -z \
  0x0 .pio/build/<profil>/VoxOne-<wersja>-full.bin
```

Nie dopisuj osobnych offsetów firmware lub SPIFFS do polecenia zapisującego
`full.bin`. Nie używaj obrazu innego profilu, nawet gdy MCU wygląda podobnie.

## FIRST BOOT

Po pierwszym uruchomieniu czystego obrazu VoxOne tworzy otwartą sieć
`VoxOne-XXXXXX`. Połącz telefon lub komputer z tą siecią. Portal powinien
otworzyć się automatycznie; w przeciwnym razie wejdź na
`http://192.168.4.1/`.

W formularzu:

1. wybierz znalezioną sieć Wi-Fi albo wpisz ręcznie SSID sieci ukrytej;
2. podaj hasło Wi-Fi, jeżeli sieć go wymaga;
3. wybierz zainstalowany wyświetlacz;
4. wybierz sterowanie `EC11`, `Buttons3` albo `NONE`;
5. potwierdź dostępne wyjście audio;
6. wybierz **Zapisz i połącz**.

Lista sprzętu jest filtrowana przez `HardwareCapabilities` bieżącego profilu.
Firmware odrzuca także ręcznie wysłaną kombinację, której dany profil nie
obsługuje. Pierwszy zapis obejmuje wspólną transakcją konfigurację sprzętu,
Wi-Fi i początkowy stan SECURITY OPEN. Jeżeli zapis nie powiedzie się, FIRST
BOOT nie jest oznaczany jako ukończony i można powtórzyć formularz.

Po sukcesie urządzenie planuje restart, łączy się z wybraną siecią i
przechodzi do normalnej pracy. Czysty Config v7 zaczyna z bezpieczną
głośnością 20%. Wyświetlacz i tryb lokalnego sterowania można później zmienić
w normalnym WWW w sekcji **SPRZĘT**; panel pokaże tylko opcje obsługiwane przez
dany profil i poprosi o restart, gdy jest wymagany.

## Normalny panel WWW i hasło

Otwórz w przeglądarce adres IP pokazany przez urządzenie lub odczytany z
routera. Standardowe obrazy W0/M0/L0/S0/DIN0 mają włączoną produkcyjną
ochronę HTTP/API/WebSocket i obsługują dwa tryby:

- **Bez hasła (OPEN)** — panel, API i WebSocket działają bez logowania.
- **Chroniony hasłem (PROTECTED)** — panel wymaga poprawnego logowania, a
  chronione HTTP/API oraz WebSocket odrzucają klienta bez ważnej sesji.

W sekcji **SYSTEM → Ochrona panelu WWW** można włączyć hasło. Po zalogowaniu
można je zmienić albo wrócić do trybu bez hasła. Każda udana zmiana ochrony
unieważnia wcześniejsze sesje. Przycisk **Wyloguj** odbiera dostęp bieżącej
sesji i zamyka jej chroniony WebSocket. Sesja wygasa również po 8 godzinach
bezczynności lub najpóźniej po 24 godzinach.

Aktualizacja starszego urządzenia nie zmienia zapisanego OPEN ani PROTECTED.
Jeżeli stary firmware miał Wi-Fi, ale nie miał jeszcze rekordu SECURITY,
panel startowy poprosi o świadomy wybór trybu bez hasła albo ustawienie hasła;
ustawienia i dane użytkownika pozostają zachowane.

## Backup i restore konfiguracji

Przed aktualizacją lub pracą serwisową otwórz **SYSTEM**:

1. wybierz **Pobierz backup** i zachowaj plik
   `VoxOne-<wersja>-backup.vxcf`;
2. sprawdź, czy plik ma niezerowy rozmiar;
3. przechowuj go jak dane poufne — zawiera zapisane hasła Wi-Fi i MQTT.

Bieżący backup VXCF obejmuje Config v7, runtime hardware, MQTT, ustawienia
interfejsu i przewijania, profile Wi-Fi oraz listę stacji. Nie obejmuje hasła
WWW i rekordu SECURITY, aktywnych sesji, chwilowego MUTE/source/playback,
pełnego NVS, firmware, slotów OTA ani `otadata`.

Aby przywrócić konfigurację, w tej samej sekcji wybierz plik `.vxcf` i użyj
**Przywróć**. VoxOne przed zapisem sprawdza format, sumę kontrolną oraz
zgodność konfiguracji sprzętu z bieżącym profilem. Dopiero kompletny commit
kończy restore. Po komunikacie o powodzeniu wybierz **Uruchom ponownie**.
Nie próbuj wymuszać importu backupu zawierającego sprzęt nieobsługiwany przez
urządzenie docelowe.

## Aktualizacja MAIN i WWW/SPIFFS

Do zwykłej aktualizacji używaj sekcji **AKTUALIZACJA** w normalnym WWW:

1. wykonaj świeży backup `.vxcf`;
2. sprawdź profil i bieżący build pokazany w panelu;
3. dla MAIN wybierz `VoxOne-<wersja>-firmware.bin` i naciśnij
   **Aktualizuj firmware**;
4. poczekaj na zakończenie zapisu, restart i ponowne pojawienie się panelu;
5. jeżeli wydanie zawiera także nowe WWW, wybierz pasujący
   `VoxOne-<wersja>-spiffs.bin`, naciśnij **Aktualizuj WWW** i ponownie
   poczekaj na pełny powrót urządzenia.

Każde żądanie może zawierać dokładnie jeden obraz. Panel i serwer odrzucają
plik `full.bin`, pusty plik, niepasujący typ oraz obraz przekraczający rozmiar
partycji. MAIN jest zapisywany do nieaktywnego slotu OTA. SPIFFS jest jedną
współdzieloną partycją; przed jego zapisem firmware zabezpiecza w NVS profile
Wi-Fi i pliki stacji, a po starcie odtwarza je do nowego systemu plików.
Pozostała konfiguracja w NVS nie jest częścią zapisu SPIFFS.

Podczas aktualizacji:

- używaj wyłącznie pary artefaktów z tej samej wersji i profilu;
- zapewnij stabilne zasilanie i sieć;
- nie zamykaj strony, nie restartuj urządzenia i nie rozpoczynaj drugiej
  aktualizacji;
- po MAIN poczekaj, aż urządzenie zgłosi gotowość i zatwierdzony start, zanim
  rozpoczniesz SPIFFS;
- zachowaj poprzednie, sprawdzone artefakty oraz backup `.vxcf` do czasu
  potwierdzenia poprawnego działania.

MAIN i normalny zapis SPIFFS są zaprojektowane tak, aby zachować ustawienia,
ale backup jest nadal obowiązkowym zabezpieczeniem. Pełny obraz USB i factory
reset są operacjami destrukcyjnymi i nie mają tej własności.

## SECURITY RECOVERY

SECURITY Recovery służy do ustawienia nowego hasła WWW albo przywrócenia
ustawień fabrycznych, gdy normalny panel jest niedostępny.

1. Przed startem zewrzyj wejście **KEY** do GND albo naciśnij przycisk
   podłączony do KEY.
2. Uruchom lub zresetuj urządzenie.
3. Gdy pojawi się Recovery, zwolnij KEY.
4. Połącz się z otwartą siecią `VoxOne-Recovery-XXXXXX`.
5. Otwórz `http://192.168.4.1/`.

Pin KEY zależy od profilu i jest opisany w
[HARDWARE_TARGETS.md](HARDWARE_TARGETS.md). W Recovery normalne RADIO,
Bluetooth, API, OTA i WebSocket nie są uruchamiane. Panel udostępnia tylko
dwie operacje:

- **Zmiana hasła WWW** — wpisz nowe hasło spełniające wymagania formularza.
  Po bezpiecznym zapisie VoxOne automatycznie się restartuje i przechodzi do
  PROTECTED.
- **Przywróć ustawienia fabryczne** — usuwa konfigurację oraz dane
  użytkownika, a po weryfikacji zapisu automatycznie restartuje urządzenie.

Factory reset usuwa Wi-Fi, stacje, runtime hardware, MQTT, ustawienia UI,
scroll, SECURITY i dane transakcji użytkownika. Zachowuje bootloader,
firmware, tablicę partycji, `otadata`, oba sloty aplikacji, systemowy stan OTA
i fabryczne zasoby WWW.

## Powrót do FIRST BOOT po factory reset

Pierwszy start po poprawnym factory reset ma pierwszeństwo FIRST BOOT. Powinna
pojawić się otwarta sieć `VoxOne-XXXXXX`; połącz się z nią i wykonaj ponownie
procedurę z sekcji **FIRST BOOT**. Dla przejrzystości zwolnij KEY przed tym
startem. Kolejne uruchomienia znów sprawdzają KEY normalnie.

Jeżeli reset zgłosi błąd, nie zakładaj, że dane zostały skasowane i nie
wyłączaj zasilania w trakcie operacji. Brak potwierdzonego sukcesu nie jest
zakończonym factory resetem.

## Różnice profili

Poniższa tabela zawiera wyłącznie capability bieżących obrazów. Każdy profil
pozwala także wybrać `display=NONE` i `input=NONE`.

| Profil | MCU / pamięć builda | Wyświetlacz runtime | Sterowanie | Pozostałe potwierdzone capability bieżącego BIN |
|---|---|---|---|---|
| W0 | ESP32, bez PSRAM; wymagane co najmniej 4 MiB flash | ST7789 284×76 | EC11 / Buttons3 / NONE | PCM5102A, RADIO, WWW, MQTT, Web Update; bez BT, RTC i VU. |
| M0 | ESP32-S3 Zero, 4 MiB flash, 2 MiB PSRAM | SSD1306 lub SSD1309 128×64 | EC11 / Buttons3 / NONE | PCM5102A, RADIO, WWW, MQTT, Web Update; bez BT, RTC i VU. |
| S0 | ESP32-S3 Zero, 4 MiB flash, 2 MiB PSRAM | SSD1306 lub SSD1309 128×64 | EC11 / Buttons3 / NONE | Tymczasowy pinout rewizji 0 jak M0; przyszłe wyposażenie rodziny S nie jest dostępne. |
| DIN0 | ESP32-S3 Zero, 4 MiB flash, 2 MiB PSRAM | SSD1306 lub SSD1309 128×64 | EC11 / Buttons3 / NONE | Tymczasowy pinout rewizji 0 jak M0; przyszłe wyposażenie DIN nie jest dostępne. |
| L0 | ESP32-S3, build 16 MiB flash i PSRAM OPI | ST7796S 480×320 | EC11 / Buttons3 / NONE | PCM5102A, DS3231, VoxOneBT przez UART/I2S, VU, RADIO, WWW, MQTT i Web Update. |

Audio w bieżących pięciu profilach jest ograniczone do PCM5102A. Pozycja w
katalogu roadmapy nie oznacza, że dany układ można wybrać w aktualnym
firmware.

W0, M0 i L0 są profilami kwalifikowanymi do Stable 1.0.0. S0 i DIN0 zachowują
tymczasowy pinout M0 oraz wymagają własnego HIL w POST-STABLE; przyszłe
dedykowane PCB będą profilami S1 i DIN1. Po wydaniu 1.0.0 W0 pozostaje
dostępny do krytycznego utrzymania jako LEGACY, bez nowego rozwoju
funkcjonalnego i bez usuwania env lub artefaktów builda.

## Stan weryfikacji

Przepływy, walidacja obrazów, transakcyjny FIRST BOOT i restore, zachowanie
danych Web Update oraz produkcyjna ochrona HTTP/API/WebSocket mają software
PASS. Podstawowy FIRST BOOT oraz część poprawnych aktualizacji i persistence
mają fizyczne potwierdzenie na M0.

Przed wydaniem nadal trzeba wykonać końcowy HIL W0, M0 i L0. W szczególności
pozostają testy produkcyjnego SECURITY OPEN/PROTECTED i wygasania sesji,
fizyczny retest zapisu hasła w Recovery, destrukcyjny factory reset, negatywne
przerwania zapisu/power-loss, pełny restore `.vxcf`, auto-reload WWW oraz
osobna kwalifikacja tych trzech profili. S0 i DIN0 zachowują dotychczasowe
wyniki i wymagania jako POST-STABLE. Niniejsza instrukcja nie oznacza
gotowości Stable 1.0.0 ani RC1.
