# VOXONE — RC1-ROM-TOOLING-SAFETY-FIX-1

Data: 2026-11-04
Baza MAIN: `81f984e459ab590cc2765651eaa986b1398747aa`
Zakres: wyłącznie przygotowanie i walidacja software; bez kontaktu z S0/L0/VoxOneBT.

## Werdykt

**SOFTWARE FIX PASS.** Wszystkie blokery wskazane przez
`RC1-ROM-TOOLING-SAFETY-REVIEW-1` zostały zamknięte w kodzie, testach i
instrukcji. Ten werdykt nie dopuszcza automatycznie operacji fizycznych.
Narzędzie wymaga niezależnego ponownego audytu bezpieczeństwa Q0; Q1–Q3
pozostają PENDING.

## Status po późniejszych etapach portability i write-retry

Powyższy werdykt oraz wyniki 22/22 opisują historyczny stan tego raportu i
pozostają niezmienione. W późniejszym, ograniczonym etapie dodano przypięte
profile runtime Python 3.13.5 / esptool 4.11.0 dla x86_64 oraz ARMv7,
walidację pełnych drzew zależności i wrapper wymuszający najwyżej jedną próbę
zapisu bloku oraz jedną próbę całej operacji `write_flash`. Końcowy zestaw
regresji ROM zawiera 26 przypadków, w tym błąd bloku i utratę połączenia po
rozpoczęciu zapisu; oba kończą się bez retry i ze stanem
`FLASH_STATE_UNKNOWN`.

Punktowa, niezależna kontrola późniejszej poprawki zakończyła Q0 wynikiem
**SAFETY REVIEW PASS**. Jest to późniejszy status software, a nie korekta
historycznego wyniku tej sesji. Q1 IDENTIFY, Q2 dwa pełne readbacki oraz Q3
kwalifikacja restore nadal pozostają **PENDING** i wymagają osobnych zgód;
nie wykonano tu żadnej komunikacji z fizycznym MCU.

## Rozliczenie problemów

| Problem | Poprawka | Dowód software |
|---|---|---|
| Wymazany lub niepoprawny bootloader mógł przejść inspekcję | Parser rzeczywistego obrazu ESP32-S3 sprawdza magic, segmenty, granice, checksum, appended hash, chip ID i brak zaprogramowanych bajtów poza logicznym obrazem | Negatywne testy obrazu wymazanego `0x0–0x7fff`, uciętego i uszkodzonego |
| `manifest.inspection` był traktowany jak źródło prawdy | Każda walidacja ponownie analizuje rzeczywiste bajty; wynik musi być identyczny z manifestem i pierwotnym `backup-state`; aktywny stan musi być spójnym `VALID` | Test rzeczywistego `PENDING_VERIFY` z podrobioną inspekcją `VALID` |
| Flagi mogły zastąpić dwa kompletne odczyty i offsite | Obowiązkowe są dwa pełne pliki o rozmiarze profilu, zgodnym SHA i identycznych bajtach oraz kompletny zestaw offsite | Testy braku drugiego odczytu i brakującego pliku offsite |
| Backup nie był dostatecznie związany z jednym urządzeniem | Schemat v2 wiąże profil, MCU, Flash, MAC, rolę USB, VID/PID/serial, `usbById`, SHA rejestru USB, SHA security-info i SHA manifestu toolchainu; weryfikowany jest lineage `backup-state`/IDENTIFY | Test innego MAC, portu, security-info, rejestru, toolchainu i zmienionego lineage |
| Symlink mógł podmienić artefakt lub katalog | Odrzucane są symlinki w końcowym i nadrzędnych komponentach prywatnych ścieżek; nazwy z manifestu muszą być bezpiecznym basename i pozostać w katalogu | Test podmiany obrazu i pliku prywatnego przez symlink |
| Runtime mógł pochodzić ze zmiennych środowiskowych | Tryb fizyczny odrzuca `VOXONE_PIO_PYTHON`/`VOXONE_ESPTOOL`; interpreter, wrapper i całe runtime tree esptool muszą odpowiadać SHA256 i wersjom z `docs/ROM_FLASH_TOOLCHAIN.json`; Python działa z `-E -s` i minimalnym env | Test env override, błędnego SHA oraz pozytywna walidacja zatwierdzonej pary |
| Częściowy dump mógł mieć zbyt szerokie prawa | Dump jest tworzony atomowo jako `0600` przed esptool; `finally` utrzymuje `0600` po błędzie i przerwaniu; katalogi są `0700` | Test błędu esptool i `KeyboardInterrupt` po utworzeniu częściowego pliku |
| Przerwanie nie zachowywało jednoznacznego stanu | Obsłużono SIGINT/SIGTERM/SIGHUP, terminację procesu potomnego, prywatny log fazy i brak retry; po rozpoczęciu zapisu stan to `FLASH_STATE_UNKNOWN` | Test atrapą przerwanego procesu oraz klasyfikacji przed/po `WRITE_START` |
| Różny filesystem był utożsamiany z niezależnym magazynem | Zapisywane i ponownie sprawdzane są mount source, UUID, identyfikator fizycznego nośnika/hosta i failure domain; wymagana jest prywatna atestacja operatora oraz kompletna kopia poza stanowiskiem | Test wspólnego failure domain i brakującego manifestu offsite |
| Token SHA planu był traktowany jak zgoda | Token pozostaje wyłącznie checksumą. RESTORE wymaga osobnego, czasowego pliku `APPROVED` związanego z `planSha256`, profilem, MAC i SHA obrazu oraz trzech flag CLI | Test poprawnej zgody i odmowy po zmianie MAC |

## Charakterystyka operacji

- IDENTIFY, BACKUP, VERIFY i RESTORE-PREFLIGHT nie wywołują `write_flash`,
  `erase_flash` ani `erase_region`; tworzą tylko prywatne artefakty hosta.
- RESTORE jest jedyną ścieżką zapisu Flash. Generuje wyłącznie pełny
  `write_flash --flash_size keep 0x0 IMAGE`, bez stubu, resetu, szerokiego
  erase i automatycznego retry.
- RESTORE ponownie sprawdza źródło lokalne i offsite, tożsamość, tablicę
  partycji, plan oraz niezależną zgodę bezpośrednio przed zapisem.
- Surowy output esptool trafia wyłącznie do prywatnego pliku `0600`; publiczny
  log operacji zawiera fazę, status, kod i hash outputu, bez zawartości NVS.

## Walidacja

- `python3 -m py_compile scripts/rom_flash_recovery.py
  test/rom_flash_recovery_test.py` — PASS;
- `test/rom_flash_recovery_test.py` — **22/22 PASS**;
- `test/m0_flash_backup_test.py` — **17/17 PASS**;
- `test/spiffs_usb_recovery_test.py` — **17/17 PASS**;
- pełne `pwsh -NoProfile -File tools/verify.ps1` po finalnym hardeningu —
  **RESULT: PASS**;
- w pełnym verify: 160 testów natywnych PASS, Native sanitizers (ASan/UBSan)
  PASS, Web Update headless PASS, HIL diagnostics na mockach PASS, SPIFFS i WWW
  PASS, L0/M0/S0/DIN0/W0 PASS, full-image regression 8/8 PASS, L0 TX-only
  PASS, WWW repeatability PASS i `git diff --check` PASS.

Zmiany dotyczą Python, ale wymagane natywne ASan/UBSan zostały uruchomione
przez pełne `tools/verify.ps1` i przeszły. Nie uruchamiano żadnego firmware na
urządzeniu; „HIL diagnostics” w verify używały wyłącznie atrap/PTY.

## Pozostałe ryzyka

1. Software nie dowodzi możliwości wejścia konkretnego S0/L0 w ROM Download
   Mode ani poprawności fizycznej topologii USB/BOOT/EN.
2. Hash-pinned runtime chroni przed przypadkowym lub jawnym podstawieniem
   innej wersji, ale host kwalifikacyjny nadal musi być zaufany i nie może być
   modyfikowany równolegle podczas operacji.
3. Atestacja offsite potwierdza decyzję operatora i rozpoznany failure domain;
   nie zastępuje organizacyjnej kontroli szyfrowania, dostępu i retencji.
4. Przerwanie zasilania lub procesu w trakcie zapisu może pozostawić Flash w
   dowolnym stanie; narzędzie celowo nie wykonuje automatycznej naprawy.
5. Backup/VERIFY nie jest dowodem restore. Dopiero pełny Q3 z readbackiem,
   bootem i POSTCHECK kwalifikuje przywracanie.

## Warunki fizycznego Q0–Q3

- **Q0:** niezależny re-audit kodu, testów, instrukcji i przypiętego runtime;
  bez urządzeń. Wymagany werdykt `SAFETY REVIEW PASS`.
- **Q1:** osobno dla S0/L0, asekuracja fizyczna, stabilne zasilanie, ręcznie
  potwierdzony ROM loader i jednoznaczna tożsamość USB/MAC; tylko IDENTIFY.
- **Q2:** dwa kompletne odczyty, identyczne SHA/bajty, rzeczywisty terminalny
  VALID, pełna niezależna kopia offsite, następnie bezpieczny ręczny powrót bez
  zapisu i kontrola niezmienionego stanu urządzenia.
- **Q3:** świeży RESTORE-PREFLIGHT i oddzielna zgoda dla planu/MAC/SHA;
  dokładnie jeden restore własnego obrazu tego samego urządzenia, verify,
  pełny readback, ręczny boot oraz POSTCHECK READY/OTA/config.

Kryterium STOP stanowi każda niezgodność tożsamości, runtime, obrazu, layoutu,
VALID, magazynu, planu, zgody, zasilania lub logów. Po `WRITE_START` każdy błąd
oznacza `FLASH_STATE_UNKNOWN` i zakaz automatycznego retry.

## TODO

`docs/TODO.md` nie został zmieniony. Żadne zadanie sprzętowe nie zostało
usunięte ani uznane za wykonane.
