# Pełny ROM backup i restore S0/L0

Ta procedura opisuje fail-closed workflow narzędzia
`scripts/rom_flash_recovery.py`. Obsługiwane są wyłącznie profile S0 i L0.
Narzędzie nie jest automatycznym flasherem: operator ręcznie wprowadza właściwy
ESP32-S3 do ROM Download Mode, a skrypt używa zawsze `--before no_reset`,
`--after no_reset` i `--no-stub`.

`no_reset` wyłącza sekwencję resetu esptool, ale samo otwarcie pyserial może
ustawić DTR/RTS. Każda komenda otwierająca port wymaga dlatego jawnego
`--confirm-port-open-may-reset`; nie jest to dowód, że fizyczny reset wystąpił
ani że nie może wystąpić.

Sama obecność USB, poprawna ścieżka `by-id` ani pomyślne IDENTIFY nie dowodzą,
że restore działa. Fizyczny PASS powstaje dopiero po zapisie uprzednio
zweryfikowanego backupu, pełnym readbacku, ręcznym uruchomieniu aplikacji i
POSTCHECK na tym samym urządzeniu.

## Kontrakt pamięci

S0 i L0 używają tej samej tablicy `partitions_yoradio.csv` w pierwszych
4 MiB:

| Obszar | Offset | Rozmiar |
|---|---:|---:|
| bootloader | `0x000000` | do tablicy partycji; rzeczywisty BIN walidowany osobno |
| partition table | `0x008000` | `0x0c00` w obrazie, odczyt kontrolny `0x1000` |
| NVS | `0x009000` | `0x005000` |
| `otadata` | `0x00e000` | `0x002000` |
| `app0` | `0x010000` | `0x1e0000` |
| `app1` | `0x1f0000` | `0x1e0000` |
| SPIFFS | `0x3d0000` | `0x030000` |

Pełny fizyczny Flash różni profile:

- S0: 4 MiB (`4194304` B), ESP32-S3 Zero;
- L0: 16 MiB (`16777216` B), ESP32-S3 N16R8.

Dlatego obraz S0 nie może przejść preflightu L0, a obraz L0 nie może przejść
preflightu S0. Dodatkowo każdy bootowalny slot musi zawierać dokładnie token
profilu oczekiwany przez plan (`S0` albo `L0`) i nie może zawierać tokenu
innego profilu.

## Stan zachowanych artefaktów

Zweryfikowany czysty zestaw S0 `81f984e` zawiera osobno bootloader,
`partitions.bin`, MAIN, SPIFFS, full image i manifest. Pakiet wdrożeniowy ma
także początkowe `otadata`. Zachowany dodatni MAIN-only OTA tego S0 pozostaje
PHYSICAL PASS w swoim zakresie; nie trzeba go powtarzać na potrzeby
kwalifikacji backupu/restore.

Zestaw L0 `4ef1e53` zawiera MAIN, SPIFFS, full image i manifest. Jego dodatni
MAIN OTA pozostaje PHYSICAL PASS. Pakiet checkpointu nie przechowuje osobno
bootloadera, tablicy i `otadata`, ale są one osadzone i zahashowane w
`full.bin`. Bieżące `.pio/build/*` zawierają osobne komponenty HEAD, lecz nie
są substytutem dokładnych komponentów wcześniej wdrożonego obrazu.

NVS nie jest artefaktem release. Jedynym dokładnym źródłem NVS, konfiguracji,
SECURITY, Wi-Fi/MQTT i stanu OTA konkretnego urządzenia jest jego pełny raw
backup. VXCF pozostaje obowiązkowym logicznym zabezpieczeniem, ale nie zawiera
SECURITY, sesji, slotów aplikacji ani `otadata`.

## Prywatny rejestr i magazyn

Każde użycie wymaga logicznej roli USB z rejestru JSON. Dla L0 istnieje
kanoniczne `L0_NATIVE` w `docs/DEV_USB_PORTS.json`. Nie używać `L0_UART` ani
`L0_LOG_RX` do ROM programowania.

Narzędzie rozwiązuje interpreter PlatformIO i `esptool.py` przed dostępem do
portu wyłącznie według `docs/ROM_FLASH_TOOLCHAIN.json`. Sprawdza SHA256 oraz
wersję obu składników, a także hash całego wykonywalnego drzewa pakietu
esptool. Python działa z `-E -s` w minimalnym środowisku bez możliwości
wstrzyknięcia przez `PYTHONPATH`, `LD_PRELOAD` lub PATH. Zmienne
`VOXONE_PIO_PYTHON` i `VOXONE_ESPTOOL` są w
trybie fizycznym jawnie odrzucane; brak dokładnie zatwierdzonego runtime jest
warunkiem STOP. Aktualizacja toolchainu wymaga osobnego przeglądu, aktualizacji
manifestu i ponownego Q0.

## Przypięty runtime

Manifest `docs/ROM_FLASH_TOOLCHAIN.json` rozdziela profile Linux x86_64 i
ARMv7 oraz przypina Python, esptool i wszystkie jego zależności. DEV-VM wybiera
profil x86_64 automatycznie. Izolowany runtime bridge wymaga jawnych parametrów
do każdej komendy fizycznej:

```sh
--runtime-profile linux-armv7l \
--runtime-root /prywatny/runtime-v2 \
--confirm-port-open-may-reset
```

Architektura hosta, wersje, hashe plików i drzew dystrybucji muszą zgadzać się
z manifestem. Niezgodność zatrzymuje operację przed otwarciem portu. Retry
synchronizacji ROM może wystąpić przed operacją. Każde uruchomienie esptool
przechodzi przez hash-pinned `scripts/esptool_write_once.py`, który po imporcie
dokładnie zatwierdzonego drzewa ustawia `WRITE_BLOCK_ATTEMPTS=1` oraz
`ESPLoader.WRITE_FLASH_ATTEMPTS=1` i kończy się fail-closed, jeżeli tych dwóch
kontroli nie da się wymusić. Oznacza to maksymalnie jedną fizyczną próbę bloku
i jedną próbę całej operacji `write_flash`; reconnect/retry zapisu jest
wyłączony.

S0 jest obecnie odłączony i nie ma kanonicznej roli USB w tym rejestrze.
Przed jego fizyczną kwalifikacją należy utworzyć poza repozytorium prywatny
rejestr o tym samym formacie, z jedną potwierdzoną rolą `S0_NATIVE`, pełną
ścieżką `/dev/serial/by-id`, VID, PID i numerem seryjnym. Nie wolno zgadywać
tych danych ani używać `/dev/ttyACM*` jako tożsamości.

Katalog roboczy i offsite zawierają hasła oraz inne dane poufne:

- lokalny katalog musi być poza repo i mieć tryb `0700`;
- pliki mają tryb `0600`;
- VERIFY wymaga pełnej kopii zestawu na trwałym, niezależnym failure domain,
  np. odrębnym zaszyfrowanym nośniku albo zdalnym hoście NFS/SSHFS;
- nie przesyłać raw dumpów do Gita, raportów ani publicznej chmury;
- dziennik zapisuje tylko metadane, SHA, etapy i powody STOP, bez kluczy NVS.

Skrypt wymusza położenie wszystkich swoich katalogów wyjściowych oraz plików
fingerprint poza repozytorium, tryb `0700` katalogów i `0600` plików. Próba
obejścia katalogu prywatnego ścieżką względną albo symlinkiem (również w
komponencie nadrzędnym) jest odrzucana. Sam różny `st_dev`/filesystem nie jest
dowodem niezależności: zapisywane i ponownie sprawdzane są mount source,
filesystem UUID, identyfikator fizycznego nośnika albo host zdalny oraz
failure domain. Operator dostarcza też prywatną, podpisaną organizacyjnie
atestację niezależnego nośnika i kopii poza stanowiskiem.

Pełny zestaw lokalny i offsite obejmuje oba niezależne odczyty Flash,
`backup-state.json`, manifest IDENTIFY, atestację offsite i identyczny
`verified-backup.json`. Każdy plik jest ponownie sprawdzany bezpośrednio przed
RESTORE. Brak któregokolwiek pliku, zmiana SHA/rozmiaru, zmiana mountu/nośnika
lub niespójność lineage zatrzymuje operację przed zapisem.

## Operacje

Poniższe polecenia są wzorami dla późniejszej, osobno zatwierdzonej sesji.
Nie uruchamiać ich jako części audytu lub przygotowania software.

### 1. IDENTIFY

Operator najpierw fizycznie wprowadza urządzenie do ROM Download Mode i
potwierdza, że aplikacja nie działa. Skrypt nie przełącza BOOT/EN.

```sh
python3 scripts/rom_flash_recovery.py identify \
  --profile L0 \
  --port-role L0_NATIVE \
  --expected-mac AA:BB:CC:DD:EE:FF \
  --usb-registry docs/DEV_USB_PORTS.json \
  --output /prywatny/nowy/L0-identify \
  --confirm-rom-loader
```

Dla S0 użyć `--profile S0`, `--port-role S0_NATIVE` i prywatnego rejestru.
IDENTIFY sprawdza USB, brak właściciela portu, chip ESP32-S3, MAC, Flash i
informacje bezpieczeństwa. Nie odczytuje Flash i niczego nie zapisuje.

### 2. BACKUP

Przed ROM mode, podczas normalnej pracy, zachować trzy logiczne snapshoty:

- `vxcf`: dwukrotnie zgodny backup Config;
- `hardware`: odpowiedź runtime hardware;
- `stations`: eksport listy stacji.

Wszystkie pliki przechowywać prywatnie. Pierwszy pełny odczyt:

```sh
python3 scripts/rom_flash_recovery.py backup \
  --identity /prywatny/nowy/L0-identify/device-identity.json \
  --usb-registry docs/DEV_USB_PORTS.json \
  --output /prywatny/nowy/L0-backup \
  --fingerprint vxcf=/prywatny/baseline/config.vxcf \
  --fingerprint hardware=/prywatny/baseline/hardware.json \
  --fingerprint stations=/prywatny/baseline/stations.tsv \
  --confirm-rom-loader
```

BACKUP wykonuje jeden pełny `read_flash` i kontroluje layout, NVS, oba sloty,
`otadata`, SPIFFS, rzeczywisty obraz bootloadera ESP32-S3 (nagłówek, segmenty,
granice, checksum i appended hash) oraz profile aplikacji. Region `0x0–0x7fff`
nie może być pusty ani wymazany. Wynik nadal ma status
`restoreAuthorized=false` i nie jest backupem zweryfikowanym.

### 3. VERIFY

VERIFY ponownie identyfikuje urządzenie i wykonuje drugi niezależny, pełny
odczyt. Bajty, rozmiary i SHA256 obu odczytów muszą być identyczne. Inspekcja
jest zawsze liczona ponownie z rzeczywistych bajtów; pole `inspection` z
manifestu nie jest źródłem prawdy. Rzeczywisty `PENDING_VERIFY`, niespójne
`otadata` lub uszkodzony slot są STOP. Następnie narzędzie kopiuje cały zestaw
na niezależny magazyn i ponownie go weryfikuje.

Przed sesją operator przygotowuje prywatny plik `0600` z atestacją. Wartości
failure domain muszą odpowiadać wynikowi `findmnt`/`lsblk` dla katalogu bench i
rodzica docelowego offsite; skrypt nie przyjmie deklaracji ogólnej:

```json
{
  "schema": "voxone-rom-offsite-attestation-v1",
  "operator": "IMIE-NAZWISKO-LUB-ID",
  "approvalId": "Q0-OFFSITE-UNIKALNY-ID",
  "confirmedAt": "2026-11-04T12:00:00Z",
  "independentPhysicalMediaConfirmed": true,
  "offBenchCopyConfirmed": true,
  "benchFailureDomain": "block:/dev/...:SERIAL-A",
  "offsiteFailureDomain": "network:backup-host:server:/private/..."
}
```

```sh
python3 scripts/rom_flash_recovery.py verify \
  --backup-dir /prywatny/nowy/L0-backup \
  --usb-registry docs/DEV_USB_PORTS.json \
  --offsite-output /mnt/prywatny-offsite/L0-backup-UNIKALNY \
  --offsite-attestation /prywatny/zgody/offsite-L0.json \
  --confirm-rom-loader
```

Po PASS urządzenie nadal pozostaje w ROM loaderze. Sam VERIFY nie jest
PHYSICAL PASS restore.

### 4. RESTORE-PREFLIGHT

Po osobnej decyzji o przygotowaniu restore operator ponownie wprowadza to samo
urządzenie w ROM loader. Preflight waliduje zweryfikowany i offsite backup,
profil, rozmiar, MAC, chip oraz dwa bieżące odczyty tablicy partycji. Źródło
restore musi mieć spójny aktywny obraz `VALID`.

```sh
python3 scripts/rom_flash_recovery.py restore-preflight \
  --verified-manifest /prywatny/nowy/L0-backup/verified-backup.json \
  --usb-registry docs/DEV_USB_PORTS.json \
  --output /prywatny/nowy/L0-restore-plan \
  --confirm-rom-loader
```

Plan zapisuje `writeAuthorized=false`, ma maksymalnie dwugodzinne okno
ważności i drukuje checksumę-token związaną z całą treścią planu. Token chroni
przed przypadkową zmianą planu, ale nie jest dowodem zgody. PASS preflightu nie
zezwala na zapis.

### 5. RESTORE — osobna jawna zgoda

RESTORE jest jedyną operacją zapisującą Flash. Wymaga planu, dokładnego tokenu,
trzech potwierdzeń oraz osobnego pliku jawnej zgody operatora związanego z MAC,
profilem, SHA obrazu i `planSha256`. Przed zapisem ponownie sprawdza cały
lokalny i offsite bundle, identyfikację oraz dwa odczyty
tablicy. Wykonuje dokładnie jeden `write_flash`, bez `erase_flash`, bez
automatycznego retry i bez resetu. Potem wykonuje `verify_flash` oraz pełny
readback i porównuje wszystkie bajty.

```sh
# TYLKO PO ODRĘBNEJ ZGODZIE DLA PLANU, MAC, SHA I URZĄDZENIA
python3 scripts/rom_flash_recovery.py restore \
  --plan /prywatny/nowy/L0-restore-plan/restore-plan.json \
  --usb-registry docs/DEV_USB_PORTS.json \
  --authorization-token WRITE-FULL-FLASH-... \
  --approval-file /prywatny/zgody/restore-APPROVAL-ID.json \
  --confirm-rom-loader \
  --confirm-separate-approval \
  --confirm-full-flash-overwrite
```

Minimalny plik zgody (`0600`, poza repo) ma schemat:

```json
{
  "schema": "voxone-rom-restore-approval-v1",
  "decision": "APPROVED",
  "separateOperatorApproval": true,
  "operator": "IMIE-NAZWISKO-LUB-ID",
  "approvalId": "RESTORE-UNIKALNY-ID",
  "planSha256": "64-znaki-z-restore-plan.json",
  "profile": "L0",
  "mac": "AA:BB:CC:DD:EE:FF",
  "imageSha256": "64-znaki-z-restore-plan.json",
  "approvedAt": "2026-11-04T13:00:00Z",
  "expiresAt": "2026-11-04T13:30:00Z"
}
```

Po PASS urządzenie nadal pozostaje w ROM loaderze. Wyjście z ROM i normalny
boot są osobną ręczną czynnością.

### 6. POSTCHECK

Po ręcznym uruchomieniu zapisać prywatnie `/api/ready`, snapshot systemowy z
profilem oraz ponownie trzy logiczne pliki. POSTCHECK porównuje pełny
readback, NVS/SPIFFS, slot wynikający z `otadata`, READY, terminalny VALID,
rollback i fingerprinty config/hardware/stations.

```sh
python3 scripts/rom_flash_recovery.py postcheck \
  --restore-result /prywatny/nowy/L0-restore-plan/restore-result.json \
  --verified-manifest /prywatny/nowy/L0-backup/verified-backup.json \
  --ready-json /prywatny/post/ready.json \
  --system-json /prywatny/post/system.json \
  --fingerprint vxcf=/prywatny/post/config.vxcf \
  --fingerprint hardware=/prywatny/post/hardware.json \
  --fingerprint stations=/prywatny/post/stations.tsv \
  --output /prywatny/nowy/L0-restore-plan/postcheck.json \
  --confirm-manual-app-boot
```

Dopiero POSTCHECK PASS umożliwia oznaczenie konkretnej procedury profilu jako
PHYSICAL ROM BACKUP/RESTORE PASS.

## Fizyczna kwalifikacja S0

Warunki wejścia:

1. S0 jest fizycznie dostępny, zidentyfikowany jako właściwy ESP32-S3 Zero
   4 MiB/2 MiB i ma prywatną rolę `S0_NATIVE` z CONFIRMED USB identity.
2. Operator zna fizyczną, ręczną sekwencję BOOT/RESET. Obecność native USB
   nie jest dowodem, że ta sekwencja działa.
3. Zasilanie MAIN i VoxOneBT jest stabilne; nie ma back-power. Połączenia
   UART1 RX44/TX43 oraz I2S1 BCLK11/WS12/DATA13 pozostają obserwowane. Sesja ROM nie
   otwiera UART ani I2S VoxOneBT; każda zmiana stanu modułu jest STOP.
4. Zachowany jest dodatni PHYSICAL PASS MAIN-only OTA `81f984e`; sesja
   recovery go nie powtarza.

Minimalna sesja kwalifikacyjna:

1. Normalny RO baseline i trzy logiczne fingerprinty.
2. Ręczny ROM entry; IDENTIFY, BACKUP i VERIFY.
3. Ręczny powrót bez zapisu; sprawdzenie READY i niezmienionego stanu.
4. Drugi ręczny ROM entry; RESTORE-PREFLIGHT.
5. Po osobnej zgodzie dokładnie jeden RESTORE własnego obrazu tego samego S0.
6. Ręczny boot; POSTCHECK z config, hardware, stations, OTA i UART/native USB.

Nie łączyć tej kwalifikacji z OTA, testami BT lifecycle ani zmianą firmware.

## Fizyczna kwalifikacja L0

Warunki wejścia:

1. Użyć wyłącznie `L0_NATIVE`, po fizycznym potwierdzeniu ręcznego ROM entry.
   Nie używać FT232R `L0_UART`; wpływ jego DTR/RTS na EN/GPIO0 jest
   niezakwalifikowany.
2. Zapewnić stabilne zasilanie dla pełnych dwóch odczytów 16 MiB, osobny
   pasywny `L0_LOG_RX` po potwierdzonej topologii oraz brak nowego
   undervoltage hosta.
3. Zachować dodatni PHYSICAL PASS MAIN OTA L0; nie powtarzać go w tej sesji.
4. Zweryfikowany raw backup musi być świeższy niż istniejący pojedynczy
   `L0-before-update-20261031.bin`; historyczny plik nie spełnia kryterium
   dwóch niezależnych odczytów ani sprawdzonego restore.

Minimalna sesja jest taka sama jak dla S0, lecz każdy BACKUP/VERIFY/readback
obejmuje pełne 16 MiB. Po restore wymagane są READY, zgodny slot/VALID,
fingerprinty Config v7 i brak zmian w normalnym stanie L0/VoxOneBT.

## Warunki STOP

Natychmiast zakończyć bez retry, resetu, erase lub kolejnego zapisu przy:

- braku ręcznie potwierdzonego ROM loadera;
- niezgodnym USB role, VID/PID/serial, MAC, chipie, profilu lub Flash size;
- właścicielu portu albo zmianie ścieżki `by-id`;
- krótkim odczycie, różnicy dwóch odczytów albo zmianie SHA obrazu/offsite;
- niepoprawnej tablicy, NVS, SPIFFS, aplikacji, `otadata` lub tokenie profilu;
- aktywnym obrazie innym niż spójny `VALID` podczas restore-preflight;
- błędzie `write_flash`, `verify_flash` lub pełnego readbacku;
- utracie logów, zasilania, undervoltage, nieoczekiwanym resecie, panic/WDT;
- niezgodnym bootId, slocie, READY, Config, hardware, stations lub stanie OTA;
- niezamierzonej zmianie stanu VoxOneBT albo urządzenia użytkownika.

Po STOP zachować katalog i dziennik w niezmienionej postaci. Narzędzie nie
wykonuje automatycznego retry ani próby „naprawy” niejednoznacznego stanu.
`SIGINT`, `SIGTERM` i `SIGHUP` kończą proces potomny oraz zachowują prywatny
log ostatniej fazy. Jeżeli `write_flash` już się rozpoczął, każdy błąd lub
przerwanie zapisuje `FLASH_STATE_UNKNOWN`; kolejne działanie wymaga ręcznej
oceny i nowej zgody.

## Granica kwalifikacji software

Test `test/rom_flash_recovery_test.py` używa syntetycznych pełnych obrazów S0
i L0. Sprawdza także uszkodzony/ucięty/wymazany bootloader, fałszywą deklarację
VALID wobec rzeczywistego PENDING_VERIFY, wymagane dwa odczyty i pełny offsite,
lineage konkretnego urządzenia, podmiany ścieżek/symlinków, hash-pinned runtime,
oddzielną zgodę, przerwania i prywatne uprawnienia częściowego dumpu. Nie
otwiera portów i nie dowodzi działania fizycznego ROM loadera, zapisu ani bootu.

## Warunki dopuszczenia Q0–Q3

- **Q0 — niezależny safety re-audit:** przegląd kodu, manifestu toolchainu,
  testów i tej instrukcji; brak kontaktu z urządzeniami. Dopiero niezależny
  `SAFETY REVIEW PASS` dopuszcza Q1.
- **Q1 — read-only IDENTIFY:** fizyczna asekuracja, ręcznie potwierdzony ROM
  loader, stabilne zasilanie, jednoznaczne USB by-id/MAC/VID/PID/serial. STOP
  przy dowolnej niezgodności; bez odczytu Flash.
- **Q2 — BACKUP/VERIFY:** dwa pełne odczyty, identyczne bajty/SHA, terminalny
  VALID, kompletna kopia na zatwierdzonym niezależnym magazynie oraz bezpieczny
  ręczny powrót urządzenia bez zapisu. Jest to nadal tylko backup PASS.
- **Q3 — RESTORE:** świeży preflight, osobna zgoda dla konkretnego planu/MAC/SHA,
  dokładnie jeden zapis, verify, pełny readback i ręczny POSTCHECK. Błąd po
  rozpoczęciu zapisu oznacza `FLASH_STATE_UNKNOWN`, bez retry.

Każdy profil (S0 i L0) przechodzi Q1–Q3 osobno. SOFTWARE PASS ani Q0 nie
uprawniają automatycznie do Q1–Q3.
