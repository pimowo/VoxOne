# VOXONE-L0-SECURITY-RELEASE-CLOSURE-1

Data: 2026-11-01. Repo `main`, HEAD
`7f25c2c91f8eb24b7b56cec71b022084d7489031`; fizyczny L0 nadal
zgłasza `bf1d147`. Zakres tej pracy: konsolidacja istniejących dowodów i
celowane testy **wyłącznie offline**. Nie kontaktowano się ponownie z L0,
nie zmieniano jego trybu OPEN ani konfiguracji i nie uruchamiano Recovery,
resetu, OTA ani flashowania.

## Kontrakt i nowa weryfikacja

`docs/STABLE_CONTRACT.md` wymaga stanów OPEN/PROTECTED/LOCKED, ochrony
HTTP/WS z kontrolą Host/Origin, logowania i wylogowania, maksymalnie dwóch
sesji RAM, wygasania po 8 h bezczynności i 24 h bezwzględnie, limitu pięciu
błędów w 5 min z blokadą na 60 s oraz Recovery i resetu hasła. Normalne
zarządzanie hasłem obejmuje włączenie, zmianę i wyłączenie ochrony.

Istniejące testy mają pokrycie tych reguł; nie dodano duplikatów. Na bieżącym
HEAD przebudowano i uruchomiono 6 testów normalnych oraz te same 6 pod
ASan/UBSan: `security_auth`, `security_session`, `security_api`,
`security_access_policy`, `security_http_adapter` z profilem L0 i
`security_runtime` — **12/12 PASS**. Dodatkowo pod ASan/UBSan z profilem L0
przeszły `security_recovery_key`, `security_recovery_state`,
`security_recovery_session`, `security_recovery_ap`,
`security_recovery_display`; `factory_reset` pod ASan/UBSan także przeszedł
— **6/6 PASS**. Łącznie 18/18 świeżych uruchomień, bez defektu. Są to testy
hostowe, nie fizyczny HIL ani pełny `tools/verify.ps1` w tej pracy.
`git diff bf1d147..HEAD` dla źródeł `security*`, `netserver.cpp`,
`profiles/l0.h` i `platformio.ini` jest pusty; nie jest to jednak bitowa
atestacja uruchomionego obrazu.

## Dowody i warunki zamknięcia L0

„M0 PASS” oznacza wyłącznie historyczny wynik odpowiedniego podscenariusza,
nie przenosi go na L0. W kolumnie L0 PASS ma zawsze wskazany wąski zakres.

| Kryterium | Software | M0 fizycznie | L0 fizycznie i dokładny warunek zamknięcia |
|---|---|---|---|
| OPEN, produkcyjny enforcement, Host/Origin | PASS | PASS na produkcyjnym M0 | **PASS ograniczony** na `bf1d147`: `enforcementMode=production`, dozwolone GET 200, obcy Host/Origin HTTP 403 i obcy Origin WS 403; `bootId` i READY stałe w krótkiej próbie. Ten podzakres jest zamknięty; nie dowodzi PROTECTED. |
| PROTECTED: anonimowe HTTP/WWW/WS, autoryzowany HTTP/WS, WS 1008 po unieważnieniu | PASS | PASS funkcjonalny w produkcyjnym HIL M0; cały przebieg pozostał PARTIAL przez osobną anomalię handshake | **UNVERIFIED / obecnie BLOCKED przez zastany OPEN.** Zamknąć po osobno zatwierdzonym OPEN→PROTECTED: odrzucenie anonimowego chronionego HTTP i WS, poprawny login i cookie, autoryzowany GET/GETINDEX/ping, zamknięcie aktywnego WS 1008 po logout/zmianie ochrony, końcowy powrót OPEN i stały bootId/READY/UART. |
| Logowanie, wylogowanie, normalna zmiana i wyłączenie hasła, rate limit | PASS, w tym 5 błędów/5 min i 60 s blokady | Produkcyjny M0 potwierdził login/logout i zarządzanie hasłem; limit błędów nie ma tu odrębnego fizycznego PASS | **UNVERIFIED / obecnie BLOCKED.** W autoryzowanej sesji PROTECTED potwierdzić pojedyncze włączenie, zmianę i wyłączenie hasła, unieważnienie starych sesji oraz kontrolowany limit błędów bez przekraczania zatwierdzonej liczby prób; zakończyć OPEN bez logowania sekretów. |
| Dwie sesje RAM, LRU, binding HTTP/WS | PASS | M0 ma funkcjonalny PASS LRU A/B/C i późniejszy produkcyjny HIL bez nawrotu korupcji; historyczny testowy retry miał panic | **UNVERIFIED.** W tej samej kontrolowanej sesji L0 utworzyć najwyżej A/B/C, potwierdzić wyparcie tylko najstarszej, zamknięcie jej WS 1008 i dalszy dostęp pozostałych; zachować UART, READY, bootId i heap. |
| Idle 8 h i absolute 24 h, HTTP i WS | PASS, także wrap licznika | M0 ma PASS fizycznego idle 8 h dla HTTP; WS expiry i 24 h nie są potwierdzone | **UNVERIFIED.** Na L0 wykazać odmowę nieaktywnej sesji po 8 h oraz odmowę aktywnej sesji po 24 h mimo dozwolonej aktywności przed limitem; sprawdzić HTTP i WS/1008, bez manipulacji zegarem. Wymaga długiej nadzorowanej sesji. |
| LOCKED po nieprawidłowym rekordzie SECURITY | PASS fail-closed | Brak odrębnego fizycznego PASS | **BLOCKED.** Zamknąć wyłącznie w osobno zatwierdzonym, odtwarzalnym HIL z kontrolowanym nieprawidłowym rekordem, pełnym backupem Flash i sprawdzoną drogą odzyskania; nie prowokować tego na pracującym L0 w OPEN. |
| Recovery przez KEY, izolowany AP i reset hasła | PASS dla L0-native, parsera, CSRF, atomowego zapisu i błędów | M0 ma dodatni PASS KEY/Recovery AP/zmiany hasła, lecz rozszerzone negatywne próby są PENDING | **BLOCKED w tym zadaniu.** Osobno zatwierdzony fizyczny L0 HIL musi wykazać KEY HIGH/LOW, wyłączność AP i dozwolone trasy, zapis hasła, automatyczny restart, powrót usług i zachowanie danych; wymaga resetu i fizycznego dostępu. |
| Factory reset i błędy Recovery | PASS granic offline | M0 ma dodatni PASS factory reset; negatywne/power-loss pozostają PENDING | **BLOCKED.** Dopiero po zweryfikowanym pełnym backupie Flash 16 MiB i procedurze przywrócenia: kontrolowany reset, FIRST BOOT, zachowanie firmware/OTA/WWW, usunięcie danych użytkownika oraz osobne błędy zapisu/kasowania. Nie wykonano na L0. |

Źródła fizycznych dowodów: `reports/VOXONE-L0-RC-SAFE-GATE-SPRINT-1.md`
oraz historyczne wpisy `SECURITY-AUTH HIL` i
`SECURITY-RECOVERY-SIMPLIFY-1` w `docs/TODO.md`. Poprzedni pełny verify i
inne kwalifikacje zachowują swoje daty oraz zakres; nie przypisano im
bieżącego PASS. Wewnętrzna bitowa tożsamość MAIN/SPIFFS L0 pozostaje
UNVERIFIED zgodnie z raportem kandydata.

## Jeden zbiorczy program fizyczny — nieuruchomiony

Przed jakąkolwiek zmianą potrzeba osobnej zgody, bezpiecznej drogi odzyskania,
pełnego backupu, pasywnego `L0_LOG_RX`, jednej sesji sterującej oraz
fingerprintów Config/playlisty/SECURITY. W pierwszym kontrolowanym przebiegu
OPEN→PROTECTED→OPEN należy wykonać tylko brakujące HTTP/WS auth, login/logout,
zmianę hasła, ograniczoną próbę LRU i rate limit, z odczytem READY, bootId,
heap, OTA, WWW i UART po każdym podetapie oraz STOP przy pierwszej anomalii.
Osobna, ciągła faza czasowa może równolegle użyć sesji idle i aktywnej dla
8 h/24 h bez skracania zegara. LOCKED, Recovery, reset hasła przez KEY i
factory reset wymagają odrębnej zgody na reset/zapis lub testowego L0 z
odtwarzalnym backupem; nie wolno ich dołączyć automatycznie do pierwszego
przebiegu. Aktualny tryb OPEN pozostaje nietknięty.

Nie znaleziono nowego defektu w testach offline, więc nie zmieniono firmware.
`RC1-PROFILE-L0`, `RC1-RECOVERY` i całe SECURITY pozostają OPEN/PARTIAL.
Pozostałych głównych bramek RC1 nie można bezpiecznie zamknąć samą
konsolidacją: RADIO endurance i L/R+VU są już CLOSED; pozostałe wymagają
fizycznego L0/S0, destrukcyjnego update/Config/Recovery, fizycznego
`_lagging`/1013 albo finalnego zamrożonego release commita. Licznik pozostaje
**10/12 OPEN, 2/12 CLOSED**.

W `docs/TODO.md` dodano jedynie świeże wyniki offline i odsyłacz do tej
tabeli; żadnego fizycznego PENDING/BLOCKED nie usunięto.
