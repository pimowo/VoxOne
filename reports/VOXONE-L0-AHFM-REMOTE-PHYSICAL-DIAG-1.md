# VOXONE-L0-AHFM-REMOTE-PHYSICAL-DIAG-1

Data: 2026-11-01 około 22:30 CET. Wynik: **PREFLIGHT ONLY / BLOCKED** —
fizyczny L0 nie odtwarzał AH.FM. Zgodnie z warunkiem zadania nie zmieniono
stacji i nie rozpoczęto 10-minutowego monitoringu.

## Read-only preflight

- `GET http://192.168.1.56/api/ready`: `ready=true`,
  `bootId=ca5d5657`, OTA `app0`/`valid`, `pending=false`, WWW
  `READY`/`verified`/`compatible`, `usable=true`.
- `GET /variables.js`: wersja `0.8.0/dev`, build `bf1d147`, profil `L0`.
- Jeden publiczny WebSocket `/ws`: handshake HTTP 101. Wyłącznie komendy
  odczytowe `getwebstatus=1` i `getsystem=1`; po snapshotach klient zamknięto.
  `webStatus`: `activeSource=radio`, stacja **RMF Dla dzieci**,
  `metadata="Miś i Margolcia - Zwierzaki cudaki"`, `codec=AAC`,
  `playback=PLAY`, `transport=playing`, `volume100=12`, `muted=false`,
  `btConnected=false`. Pole `source=WEB` w tej ramce nie zastępuje
  `activeSource=radio`.
- `systemInfo`: `rssi=-59 dBm`, `freeHeap=157812 B`,
  `minimumFreeHeap=92156 B`, `psramTotal=8385607 B`,
  `psramFree=7749167 B`, `uptimeSeconds=22013`, `abuff=7`.
  `abuff` jest ustawieniem bufora, **nie** bieżącym wypełnieniem `audioBuffer`.

Nie uzbrajano FT232R `L0_LOG_RX`, nie powstał log UART. Nie ma ciągłego
logu WS, 10-minutowych próbek READY/heap/PSRAM/RSSI ani odczytów
`audioBuffer`/`loopMaxUs`. Sam preflight nie pozwala ocenić anomalii
transportu HTTP/TLS/ICY/MP3, reconnectu ani ciągłości audio.

## Granica wniosku i następny warunek

Wcześniejszy `VOXONE-RADIO-AHFM-OFFLINE-REGRESSION-1` odtworzył poprawnie
krótką próbkę AH.FM w hostowym parserze chunked/ICY; identyczność dekodera
MP3 względem bazowego yoRadio i ten replay nie dowodzą pracy fizycznego
ESP32-S3. Obecny preflight dotyczy innej stacji i kodeka AAC, więc nie
może potwierdzić ani wykluczyć zgłoszonego zacinania AH.FM.

Do właściwej sesji potrzebne jest, by użytkownik **sam** ustawił
`https://nl.ah.fm/live` i potwierdził, że ma pozostać wybrana przez cały
10-minutowy pomiar. Dopiero świeży read-only preflight potwierdzający
AH.FM/RADIO/PLAY uzasadni uruchomienie pasywnego loggera i monitoringu.
Bez odsłuchu lub obiektywnego pomiaru sygnału wynik AUDIO pozostanie
**UNVERIFIED**, nawet jeśli READY, transport i bufor będą stabilne.

`docs/TODO.md` pozostawiono bez zmian: nie uzyskano nowych dowodów dla
AH.FM ani podstawy do zamknięcia zadania. Nie wysyłano komend zmieniających
stan, nie wykonywano OTA, flashowania, restartu, commita ani push.
