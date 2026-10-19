# Selektywne odzyskiwanie WWW/SPIFFS przez USB

Ta procedura służy wyłącznie do serwisowego odtworzenia partycji `spiffs`
zawierającej WWW. Nie zapisuje bootloadera, tablicy partycji, `otadata`, NVS,
`app0` ani `app1`. Nie zastępuje normalnej aktualizacji OTA i nie naprawia
konfiguracji Wi-Fi.

Narzędzie `scripts/spiffs_usb_recovery.py` działa fail-closed i rozdziela
operację na cztery tryby:

1. `manifest` — pełna walidacja obrazu offline;
2. `prepare` — dwa odczyty tablicy partycji i dwa odczyty backupu SPIFFS,
   bez zapisu flash;
3. `show-plan` — ponowna walidacja planu i backupu offline;
4. `execute` — dokładnie jeden zapis wyłącznie zatwierdzonego zakresu SPIFFS,
   dostępny dopiero po osobnej zgodzie użytkownika.

## Warunki wstępne

- repozytorium i obraz muszą pochodzić z zatwierdzonego checkpointu;
- operator musi znać profil (`L0`, `M0`, `S0`, `DIN0` albo `W0`), MAC oraz
  oczekiwany SHA256 obrazu;
- port musi być wskazany stabilną ścieżką `/dev/serial/by-id/...`;
- ESP32 musi zostać świadomie wprowadzony do ROM loadera; narzędzie nie
  przełącza linii reset/boot;
- logger lub inny właściciel portu musi zostać jawnie zwolniony;
- należy zachować pełny backup urządzenia oraz sprawdzony sposób powrotu.

Wszystkie obecne profile używają wersjonowanego `partitions_yoradio.csv`.
Narzędzie mimo to odczytuje rzeczywistą tablicę urządzenia dwa razy i wymaga
jej dokładnej zgodności. Dla bieżącego layoutu jedynym dozwolonym celem jest:

```text
spiffs: offset 0x3d0000, size 0x30000, end-exclusive 0x400000
```

Nie wolno przenosić tych liczb ręcznie do polecenia `esptool`. Plan wyznacza
je z tablicy partycji i blokuje operację przy każdej rozbieżności.

## 1. Manifest obrazu — tylko offline

Przykład dla zweryfikowanego obrazu L0 z checkpointu `4c56820b`:

```bash
python3 scripts/spiffs_usb_recovery.py manifest \
  --profile L0 \
  --image .pio/build/l0/spiffs.bin \
  --expected-sha256 f6266950c2995afc378b63a84daab9a719f8bc875247f08f2f86794a57cb3fe6 \
  --www-build 4c56820b \
  --output /prywatny/katalog/L0-spiffs-image-manifest.json
```

Tryb sprawdza dokładny rozmiar, integralność SPIFFS, siedem wymaganych plików
WWW, poprawność każdego gzipa oraz SHA256 zawartości skompresowanej i
rozpakowanej. Obecny `wwwBuild` jest metadanymi serwisowymi, a nie podpisem
kryptograficznym ani identyfikatorem osadzonym wewnątrz assetów. To ograniczenie
ma usunąć osobny etap `WWW-ASSET-INTEGRITY-1`.

## 2. Przygotowanie planu — odczyt urządzenia, bez zapisu

Po osobnej zgodzie na dostęp odczytowy i ręcznym wejściu do ROM loadera:

```bash
python3 scripts/spiffs_usb_recovery.py prepare \
  --profile L0 \
  --port /dev/serial/by-id/usb-... \
  --expected-mac 02:00:00:00:00:01 \
  --image .pio/build/l0/spiffs.bin \
  --image-manifest /prywatny/katalog/L0-spiffs-image-manifest.json \
  --expected-sha256 f6266950c2995afc378b63a84daab9a719f8bc875247f08f2f86794a57cb3fe6 \
  --expected-www-build 4c56820b \
  --output /prywatny/nowy-katalog/planu \
  --confirm-rom-loader
```

`prepare` potwierdza chip, MAC, rozmiar flash, stabilną tożsamość USB, brak
właściciela portu, tablicę partycji, zakres SPIFFS i źródłowy obraz. Zapisuje
dwa identyczne odczyty tablicy, dwa identyczne backupy SPIFFS, plan JSON oraz
sidecar SHA256. Katalog wyjściowy musi być nowy. Wynik zawiera
`writeAuthorized=false` i jednorazowy token związany z całą treścią planu.

Plan można bez urządzenia ponownie sprawdzić:

```bash
python3 scripts/spiffs_usb_recovery.py show-plan \
  --plan /prywatny/nowy-katalog/planu/restore-plan.json
```

PASS tego etapu nie oznacza zgody na zapis.

## 3. Zapis — wymaga osobnej, wyraźnej zgody

Poniższe polecenie jest destrukcyjne dla aktualnej zawartości SPIFFS. Nie wolno
go uruchamiać w ramach preflightu ani na podstawie samego PASS `prepare`.
Wymaga oddzielnego zatwierdzenia użytkownika dla konkretnego planu, portu, MAC,
obrazu, SHA256 i zakresu.

```bash
# URUCHOM WYŁĄCZNIE PO ODRĘBNEJ ZGODZIE NA ZAPIS
python3 scripts/spiffs_usb_recovery.py execute \
  --plan /prywatny/nowy-katalog/planu/restore-plan.json \
  --port /dev/serial/by-id/usb-... \
  --expected-mac 02:00:00:00:00:01 \
  --authorization-token WRITE-SPIFFS-... \
  --confirm-rom-loader \
  --confirm-separate-approval
```

Przed zapisem narzędzie powtarza identyfikację, dwa odczyty tablicy i dwa
odczyty SPIFFS. Aktualny SPIFFS musi nadal być identyczny z backupem planu.
Następnie wykonuje dokładnie jeden zapis, `verify_flash` i pełny readback.
SHA256 readbacku musi być identyczny z obrazem. Narzędzie nie wykonuje
automatycznego retry, resetu, `erase_flash`, `erase_region` ani pełnego restore.

## 4. Kontrolowany start i walidacja

Po PASS zapisu urządzenie pozostaje w ROM loaderze. Wyjście z niego i jeden
kontrolowany start wymagają kolejnej świadomej czynności. Po uruchomieniu należy
sprawdzić co najmniej:

- brak panic, watchdog i pętli restartów;
- poprawny mount SPIFFS;
- `/api/ready`, podstawowe WWW i WebSocket;
- stronę Emergency Update oraz wymagane assety;
- zgodność wersji WWW z zatwierdzonym manifestem;
- brak zmian NVS, konfiguracji, playlisty i aplikacji.

Pełny PASS sprzętowy wymaga zachowania logu UART, planu, obu backupów,
`execution-result.json`, readbacku i wszystkich SHA256.

## Postępowanie przy błędzie

Przy braku komunikacji, krótkim odczycie, rozbieżnych odczytach, niezgodnym
MAC/chip/layout, zmianie źródła, błędzie zapisu, `verify_flash` albo readbacku:

1. zatrzymać procedurę;
2. nie ponawiać zapisu i nie resetować automatycznie;
3. zachować wszystkie logi i częściowe dowody;
4. ustalić rzeczywisty stan flash przed kolejną decyzją;
5. użyć zweryfikowanego pełnego backupu tylko w osobno zatwierdzonej procedurze.

Narzędzie nie zgaduje layoutu, nie obsługuje nieznanych profili i nie pozwala
ominąć walidacji. Recovery USB przywraca tylko WWW/SPIFFS; nie jest dowodem
poprawności bootloadera, OTA rollbacku, SECURITY ani sieci urządzenia.
