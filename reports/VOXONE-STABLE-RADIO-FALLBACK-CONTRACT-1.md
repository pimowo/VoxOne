# VOXONE-STABLE-RADIO-FALLBACK-CONTRACT-1

Data: 2026-10-24

Stan: **RADIO FALLBACK SOFTWARE PASS — HIL PENDING**

## Wynik kontraktu

Nie znaleziono defektu wymagającego zmiany produkcyjnej logiki SourceManager
ani transportu RADIO.

- RADIO z aktywnym intencją PLAY jest zawieszane przy automatycznym przejściu
  do BT i wraca do PLAY tej samej stacji po disconnect.
- Intencja RADIO STOP pozostaje STOP i fallback nie generuje zbędnego STOP ani
  autoplay. Oczekujący jawny STOP jest zachowywany przy wymianie kolejki i
  wykonywany dokładnie raz.
- Powtarzane snapshoty i zdarzenia bez nowej krawędzi stanu nie generują
  dodatkowych PLAY, STOP, SUSPEND ani RESUME.
- Ręcznie wybrane RADIO pozostaje aktywne mimo istniejącego połączenia BT;
  dopiero nowa krawędź połączenia może ponownie wybrać BT.
- Utrata BT podczas TTS nie przerywa komunikatu i nie uruchamia RADIO po jego
  zakończeniu. Blokada restore jest zużywana jednokrotnie, a intencja RADIO
  PLAY pozostaje zachowana dla późniejszego zwykłego restore.
- Opóźniony RESUME zakolejkowany przed późniejszym STOP nie uruchamia RADIO,
  ponieważ Player ponownie sprawdza aktualną intencję przed odtworzeniem.

## Zmienione pliki

- `test/radio_fallback_contract_native.cpp` — celowana regresja integrująca
  rzeczywiste `BtLinkProtocol`, `SourceManagerState`, `TemporaryAudioState` i
  politykę transportu RADIO oraz kontrolująca produkcyjne spięcie z Playerem;
- `tools/verify.ps1` — włączenie testu do natywnego gate'u;
- `docs/TODO.md` — programowe części kontraktu oznaczone PASS, zachowane
  wyłącznie fizyczne bramki HIL;
- ten raport.

Nie zmieniono produkcyjnej logiki firmware ani przygotowanej poprawki
AsyncTCP.

## Weryfikacja

PASS:

- `radio_fallback_contract_native` — build zoptymalizowany;
- `radio_fallback_contract_native` — ASan/UBSan z detekcją wycieków;
- 17 powiązanych testów RADIO, BT, SourceManager, temporary audio i statusu
  WWW;
- `radio_transport_native.py --strict` — 16/16 kontroli;
- PlatformIO `w0` — RAM 75 556 B, flash 1 642 293 B;
- PlatformIO `m0` — RAM 74 852 B, flash 1 638 497 B;
- PlatformIO `s0` — RAM 74 852 B, flash 1 636 445 B;
- PlatformIO `din0` — RAM 74 852 B, flash 1 636 445 B;
- PlatformIO `l0` — RAM 80 128 B, flash 1 716 833 B;
- `git diff --check`.

## Pozostały HIL

SOFTWARE PASS nie jest fizycznym HIL PASS. Nadal trzeba na rzeczywistym
profilu z RADIO i VoxOneBT potwierdzić słyszalne PLAY/STOP, routing i wyciszenie
przy RADIO ↔ BT, disconnect/offline/reconnect, zakończenie i timeout TTS oraz
priorytet ręcznego wyboru. Pełna macierz telefonu/LG TV, VU, I2S/XSMT i utrata
runtime modułu pozostaje osobno otwarta w `docs/TODO.md`.
