# VOXONE-L0-RC-CANDIDATE-PREP-1

Data: 2026-10-31. Przygotowanie offline, bez kontaktu z L0 i bez zmiany
kontraktu Stable. Kanoniczne bramki i ich status są tylko w `docs/TODO.md`.

## Decyzja i checkpoint

**L0 RC NOT READY; offline candidate PREPARED, physical qualification PENDING.**
Proponowana, jednoznaczna baza binarnego kandydata: czysty commit
`bf1d147958b6c4f6b66c29bf0e3bc416063a8814` (`main`, `0.8.0/dev`, L0).
Obrazy zbudowano w odizolowanym, czystym worktree z tej rewizji. Nie są
obrazami z obecnego brudnego worktree ani przyszłego, jeszcze niezatwierdzonego
commita. Spójny oczekujący checkpoint źródłowy obejmuje wyłącznie zachowane
zmiany `docs/TODO.md`, `test/mqtt_contract_native.cpp`, poprzedni raport
`VOXONE-L0-STABLE-FAST-CLOSURE-1.md`, niniejszy raport i manifest; bez zmian
produkcyjnego kodu. **Nie commitowano.** Jeśli użytkownik zatwierdzi nowy
checkpoint commit, to będzie to nowa tożsamość buildu; przed nazwaniem go
finalnym RC należy ponownie wygenerować obrazy z tego dokładnego czystego
commita i porównać SHA. Nie wolno przemetkować obecnych obrazów.

## Ostatni L0 a kandydat

Ostatni odczyt fizycznego L0: `b4b74cf-dirty`, `0.8.0/dev`, profil L0,
`bootId=e95a3cbe`, app1 VALID, WWW READY/verified z `assetSet`
`1d7762d8eac4570ba7bee2954ac86abb3d32af61842b681989a9219275915c61`.
Napis `-dirty` nie podaje zawartości roboczych zmian. Nie odnaleziono obrazu
ani walidacyjnego manifestu tego dokładnego wdrożenia. Dostępny zrzut 16 MiB
`~/voxone-hil/backups/l0/L0-before-update-20261031.bin` (16 MiB,
SHA-256 `4ac09b1cc68f4e49a73d2f6ee9fb5de22afa17a1c8440d2c4bcf76fbce4f2e1f`)
pochodzi *sprzed* aktualizacji i nie identyfikuje bieżącego L0. Tożsamość `b4b74cf-dirty`
pozostaje **UNVERIFIED**; nie można obliczyć różnicy bitowej wdrożony→nowy.

Znana różnica czystego `b4b74cf`→`bf1d147`:

| Warstwa | Śledzona różnica | Granica dowodu |
|---|---|---|
| MAIN runtime | `src/core/netserver.cpp`: `GET /api/bt/update` dodaje pole `confirmedVersion` po sukcesie sendera BT, z buforem JSON 220→320 B. | Nie ma dowodu, czy ta zmiana była już w dirty obrazie fizycznym. |
| WWW | Nowy stan dostępności i osobny postęp VoxOneBT w HTML/CSS/JS; zmienione cztery pliki `data/www` (manifest i trzy gzip). | AssetSet fizycznego L0 zgadza się z aktualnym repo, ale nie dowodzi pełnej tożsamości SPIFFS ani MAIN. |
| Build/profile/config | `platformio.ini`, `profiles/`, `src/core/version.h`, skrypty buildu — bez śledzonych zmian. | Nieznane dirty zmiany historycznego obrazu. Runtime Config v7/NVS L0 nie był odczytywany ani zmieniany. |
| Partycje | `partitions_yoradio.csv` bez śledzonych zmian: NVS `0x9000/0x5000`, otadata `0xe000/0x2000`, app0 `0x10000/0x1e0000`, app1 `0x1f0000/0x1e0000`, SPIFFS `0x3d0000/0x30000`. | Brak odczytu fizycznej tabeli partycji L0 w tym zadaniu. |

## Offline candidate i automatyczna weryfikacja

Manifest z pełnymi SHA, rozmiarami, kontraktem WWW i offsetami:
`reports/VOXONE-L0-RC-CANDIDATE-PREP-1.manifest.json`.
Komplet zachowanych obrazów: `~/voxone-hil-firmware/VOXONE-L0-RC-CANDIDATE-PREP-1-bf1d147-clean/`.
Ten zestaw jest **NOT DEPLOYED / NOT HIL PASS / NOT RC READY**.

Izolowany `pio run -e l0 -t buildfs` oraz `pio run -e l0 -t fullimage`
zakończyły się PASS; generator zwalidował wszystkie komponenty i pełny obraz
16 MiB. Obraz MAIN ma 1 764 432 B (89,7% partycji app0/app1 po 1 966 080 B)
i SHA-256 `f417e0f00435d45d389b42a518d95e7685355274975341f84a89cdf6a99b2446`.
SPIFFS ma 196 608 B i SHA-256
`058895a892ac93963000ad97e9c9712318a7ac3e1ffe0711a58e4d3b99ab4929`.
Pełny obraz ma SHA-256
`56a4d453286c49322840533e9753df12a5887ad9855c8a843caeb25606ed3e8e`.
Manifest WWW ma 7/7 zasobów i kontrakt 1; walidacja SHA/rozmiaru
skopiowanych siedmiu artefaktów ma PASS. Czysty worktree pozostał bez zmian
śledzonych ani nieśledzonych.

`tools/verify.ps1` wykrywa `test/mqtt_contract_native.cpp` przez glob
`test/*_native.cpp` w kroku „Native tests”, kompiluje go ze ścieżką `-I test`
i wykonuje. W bieżącym pełnym przebiegu uzyskano 158/158 native PASS,
w tym jawne `PASS mqtt_contract_native`, oraz RADIO transport strict 16/16.
Nowe asercje sprawdzają oba warianty `sessionPresent`, ponowną
subskrypcję `command` QoS 2 i retained `status`, `volume`, `playlist` QoS 0;
nie zastępują fizycznego MQTT reconnectu.

Osobny `mqtt_contract_native` pod ASan/UBSan: PASS.
Pełny `pwsh -NoProfile -File tools/verify.ps1` na bieżącym roboczym drzewie:
**RESULT: PASS** — 158 native, strict RADIO 16/16, native sanitizers,
headless WWW Update, diagnostyka mock-HIL, SPIFFS, WWW assety 7/7,
fullimage L0/M0/S0/DIN0/W0, regresja walidacji 8/8, L0 TX-only,
powtarzalność WWW i `git diff --check`. Obraz L0 wygenerowany przez ten
pełny runner miał oznaczenie `bf1d147-dirty` i 1 764 448 B; **nie jest**
opisanym wyżej czystym kandydatem 1 764 432 B. Przebieg nie kontaktował się
z żadnym fizycznym urządzeniem.

## Jeden minimalny HIL L0 — dopiero po osobnej zgodzie na wdrożenie

Nie jest to polecenie wykonania OTA. Po zatwierdzeniu dokładnego commita,
artefaktów, kopii/rollbacku i okna fizycznego użytkownika: odnotować stan
początkowy i obie sumy obrazu; uzbroić pasywny `L0_LOG_RX` na voxone-bridge,
jeden monitor WS/GETINDEX i read-only HTTP. Dopiero po osobnej autoryzacji
wdrożenia wykonać jedną kontrolowaną aktualizację; w tym zadaniu jej nie było.

| Ograniczony zakres po wdrożeniu | Dowód wymagany | Bez powtórzenia |
|---|---|---|
| Tożsamość i stabilność | `/api/ready` READY, nowy stały bootId, właściwa rewizja L0, OTA VALID, WWW READY/verified z oczekiwanym assetSet; heap/PSRAM i UART bez panic/assert/WDT. | Pełny RADIO endurance. |
| Zmieniony kontrakt BT update | Read-only `GET /api/bt/update` i WWW: `confirmedVersion`/stan dostępności, brak fałszywego sukcesu; bez uruchamiania update BT. | Normalny A2DP i volume/MUTE 20→15→20. |
| Bazowy tor użytkownika | Jedno krótkie okno RADIO/Italo4you: WS GETINDEX/webStatus, MUTE, volume, balance, stacja i bezpieczny odsłuch/obraz/VU przez użytkownika; bez zmian playlisty. | Zaliczony kontrolowany bazowy L/R/VU. |
| Otwarte release gates, tylko gdy przygotowane | Oddzielne kryteria i manifesty: TTS A/B/C po odzyskaniu oryginalnych fixture; `_lagging` i 1013 wyłącznie z bezpiecznym bounded harnessem; BALANCE audio/VU tylko z wiarygodną metodą; MQTT reconnect tylko z izolowanym bezpiecznym mechanizmem. | TTS D, WS 190 s/RST, MQTT HA volume. |

Warunek natychmiastowego STOP: zmiana bootId poza zaplanowanym restartem
wdrożenia, brak READY/WWW, OTA nie-VALID, panic/assert/WDT, pogarszający się
heap, błąd audio, nieoczekiwany autoplay/odciszenie lub zmiana stacji,
playlisty albo konfiguracji. Zatrzymać aktywne próby, zachować pasywny UART
i postcheck; nie improwizować reconnectu, fault injection ani kolejnego OTA.

## Nadal BLOCKED / decyzje

- P1 AsyncTCP `_lagging` i osobny NetServer close 1013: brak bezpiecznego,
  deterministycznego fizycznego harnessu.
- TTS A/B/C na dokładnym kandydacie: brak oryginalnych fixture/manifestów;
  historyczny PASS pozostaje historyczny. TTS D nie powtarzać.
- MQTT reconnect/availability/LWT: brak wspieranej izolowanej metody
  MQTT-only; retained `status` po zmianie metadanych i aktywny `playlist`
  nadal niepotwierdzone. HA volume i pasywne retained mają PASS w swoim zakresie.
- BALANCE endpoint L0 audio/VU: UNVERIFIED; brak wiarygodnej obserwacji.
- Negatywne update/recovery/Config persistence oraz osobny niewdrożony
  VoxOneBT memory-safety fix: wymagają asekuracji, decyzji i odrębnej zgody.

Decyzje przed **L0 RC READY**: zatwierdzić dokładny czysty commit i obrazy,
autoryzować bezpieczne wdrożenie wraz z rollbackiem, zapewnić fizyczną
kwalifikację zmienionego obrazu, a dla nierozstrzygniętych release-gating P1
i TTS wskazać dowód albo jawną decyzję o zakresie — nie dopisywać PASS.
Pełne Stable 1.0.0 dodatkowo wymaga obowiązkowego S0+VoxOneBT.
