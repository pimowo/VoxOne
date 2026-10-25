# VOXONE-STABLE-BT-VU-CONTRACT-1

Data: 2026-10-24

Stan: **BT VU SOFTWARE PASS — HIL PENDING**

## Wynik kontraktu

Programowy przepływ UART VoxOneBT → `BtLinkProtocol` →
`SourceManagerState` → `sourceVuSelect()` → lokalny VU i stan WWW spełnia
kontrakt Stable po jednej minimalnej poprawce.

- `PLAYING` przyjmuje świeże próbki `VU L R`, skaluje kanały niezależnie i
  publikuje stan odtwarzania BT do wszystkich klientów WWW.
- `PAUSED` i `STOPPED` zerują poziomy oraz znacznik świeżości; VU wygasa bez
  pozostawienia poprzednich słupków.
- Disconnect i utrata sesji czyszczą playback, metadata, sample rate oraz VU;
  reconnect zaczyna z pustym VU i pokazuje poziomy dopiero po nowej próbce.
- Przełączenie RADIO ↔ BT zawsze wybiera miernik aktywnego źródła i nie
  przenosi wartości drugiego źródła.
- Powtarzane komunikaty nie generują zbędnych zmian WebStatus ani nie cofają
  stanu.
- `vumeter` zapisuje wspólne ustawienie i żąda `GETSYSTEM` z `clientId=0`, co
  prowadzi do autoryzowanego broadcastu WebSocket; nowy klient otrzymuje ten
  sam bieżący snapshot. UI obsługuje żądanie, stan oczekujący i potwierdzenie
  ON/OFF.

## Znaleziony i naprawiony defekt

Parser zerował poziomy przy `PAUSED` i `STOPPED`, lecz przyjmował późniejszą,
zakolejkowaną ramkę `VU`, jeżeli telefon pozostawał połączony. Następne
`PLAYING` mogło przez okno świeżości 150 ms pokazać tę starą próbkę.

Minimalna poprawka:

- zeruje również `rawVuLastMs` przy `PAUSED` i `STOPPED`;
- przyjmuje ramkę `VU` wyłącznie przy `connected == true` oraz
  `playback == Playing`.

Format protokołu VoxOneBT, routing audio i pozostałe zachowanie transportu nie
zostały zmienione.

## Zmienione pliki

- `src/core/bt_link_protocol.cpp`;
- `test/bt_link_native.cpp`;
- `test/bt_vu_contract_native.cpp`;
- `test/voxone_update_headless.py`;
- `tools/verify.ps1`;
- `docs/TODO.md`;
- ten raport.

Przygotowana poprawka AsyncTCP nie została zmieniona.

## Weryfikacja

PASS:

- nowy `bt_vu_contract_native` — build zoptymalizowany oraz ASan/UBSan z
  detekcją wycieków;
- 17 istniejących testów BT, VU, SourceManager i WebStatus;
- ASan/UBSan: `bt_link_native`, `bt_vu_native`, `source_manager_native` i
  `web_status_native`;
- pełny `voxone_update_headless.py`, w tym desktop, mobile, recovery, Wi-Fi
  recovery i first boot;
- PlatformIO `w0` — RAM 75 556 B, flash 1 642 293 B;
- PlatformIO `m0` — RAM 74 852 B, flash 1 638 497 B;
- PlatformIO `s0` — RAM 74 852 B, flash 1 636 445 B;
- PlatformIO `din0` — RAM 74 852 B, flash 1 636 445 B;
- PlatformIO `l0` — RAM 80 128 B, flash 1 716 841 B;
- `git diff --check`.

## Pozostały HIL

Software PASS nie potwierdza fizycznego sygnału audio ani wskazań ekranu.
Nadal trzeba sprawdzić telefon i LG TV podczas PLAY/PAUSE/STOP,
disconnect/reconnect i RADIO ↔ BT, oba kanały VU, wygaszanie słupków, brak
starego wskazania po zmianie źródła oraz zgodność ustawienia ON/OFF w kilku
realnych przeglądarkach i po restarcie.
