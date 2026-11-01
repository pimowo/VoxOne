# VOXONE-L0-RC-SAFE-GATE-SPRINT-1

Data: 2026-11-01. Repo `main` na `7d132e3`; fizyczny L0 `192.168.1.56`.
Zakres: jeden niedestrukcyjny podtest `SECURITY-AUTH` w bramce
`RC1-PROFILE-L0`. Bez zmiany konfiguracji, firmware, stacji, audio, HA i MQTT.

## Wybór i wynik

Po odfiltrowaniu zaliczonych TTS A/B/C, MQTT retained/live i software
AsyncTCP oraz prób odsłuchowych, S0 i operacji destrukcyjnych, pozostałym
wysoko priorytetowym kryterium L0 możliwym do sprawdzenia samym odczytem była
polityka SECURITY/WWW na bieżącym kandydacie. `STABLE_CONTRACT.md` wymaga
produkcyjnego enforcement, poprawnego Host/Origin i ochrony handshake WS.
Fizyczne `_lagging`/1013 wymagałoby ryzykownego przeciążenia bez
zatwierdzonego bezpiecznego harnessu, a Config/OTA/Recovery wymagają zapisu
lub restartu; nie były kandydatami do tego sprintu.
Trudniejsze stany PROTECTED/expiry wymagają zmiany SECURITY lub utworzenia
sesji i nie były tu wykonywane.

**PHYSICAL HIL PASS — zakres ograniczony do SECURITY OPEN i Host/Origin.**
`GET /variables.js` potwierdził `0.8.0/dev`, build `bf1d147`, profil `L0`.
`GET /api/security/state` zwrócił HTTP 200 i
`{"state":"open","setupAllowed":false,"loginAllowed":false,"authenticated":false,"protectionEnforced":true,"enforcementMode":"production"}`.
Ten sam odczyt z dozwolonym `Origin: http://192.168.1.56` zwrócił 200.
Obcy `Origin: http://example.invalid` i obcy `Host: example.invalid`
zwróciły osobno HTTP 403 `{"error":"request_rejected"}`. Pojedynczy
handshake `GET /ws` z poprawnymi nagłówkami WebSocket v13, lecz obcym Origin,
zwrócił HTTP 403 z tym samym błędem. Nie wysłano ramki WS ani komendy.

Przed i po próbie `GET /api/ready` zwrócił HTTP 200, `ready=true`, stały
`bootId=ca5d5657`, `app0` VALID, WWW `READY/verified/compatible` i ten sam
assetSet `1d7762d8eac4570ba7bee2954ac86abb3d32af61842b681989a9219275915c61`.
To krótkie kontrole, nie długi endurance. Nie zbierano UART ani heap.
Zgodność z kodem: `security_access_policy.cpp` odrzuca nieprawidłowy Host
i Origin przed przyznaniem dostępu; handshake WS wymaga Origin. Próba
wykorzystała wyłącznie GET i nie tworzyła sesji ani nie zapisywała danych.

## Granica dowodu

Nie znaleziono defektu w wykonanym podteście, więc nie ma uzasadnienia dla
zmiany firmware lub nowego testu native. Zyskano bieżący dowód fizyczny
produkcyjnego enforcement i odmowy obcego Host/Origin na L0 `bf1d147`.
Nie zaliczono całego `SECURITY-AUTH` ani `RC1-PROFILE-L0`; licznik głównych
bramek pozostaje **10/12 OPEN, 2/12 CLOSED**. Tryb PROTECTED, sesje, login,
logout, LRU i expiry pozostają bez fizycznego dowodu na tym L0. Inne otwarte
bramki L0 obejmują fizyczne `_lagging` i close 1013, pełny lifecycle BT,
trwałość Config oraz destrukcyjne OTA/Recovery; nie wykonywano ich w tym
sprincie. Poprzednich PASS nie powtarzano.

W `docs/TODO.md` dopisano tylko ten ograniczony dowód do istniejącego wpisu
SECURITY-AUTH; żadnego zadania wymagającego HIL nie usunięto.
