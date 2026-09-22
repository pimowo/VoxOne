# DINaudio M2.2 — AVRCP / metadata / volume sync

Firmware: `0.2.0` (milestone M2.2)

Dokument historyczny: DINaudio to dawna nazwa projektu VoxOne.

## Testy obowiązkowe

### 1. Bluetooth audio
- połącz telefon,
- uruchom muzykę,
- dźwięk przez PCM5102A,
- TFT: BT / BT PLAY.

### 2. Głośność — DINaudio -> telefon
- kręć enkoderem,
- VOL na TFT zmienia się,
- telefon pokazuje zmianę głośności.

### 3. Głośność — telefon -> DINaudio
- zmień głośność przyciskami/suwakiem telefonu,
- VOL na TFT musi się zmienić,
- po odświeżeniu WWW wartość musi być ta sama.

### 4. WWW -> telefon
- ustaw głośność z WWW,
- telefon ma zareagować,
- TFT ma pokazać tę samą wartość.

### 5. AVRCP
Sprawdź z WWW:
- PLAY
- PAUSE
- NEXT
- PREVIOUS

Sprawdź klik enkodera:
- gdy gra -> PAUSE
- gdy pauza -> PLAY

### 6. Metadata
WWW:
- nazwa telefonu, jeśli urządzenie ją udostępnia,
- artysta,
- tytuł.

TFT:
- dolny wiersz podczas BT pokazuje `artysta - tytuł`,
- gdy brak metadata, pozostaje IP/AP.

### 7. Stabilność
- kilka razy pause/play,
- kilka razy next/previous,
- zmiany volume z obu stron,
- odświeżanie WWW podczas grania,
- rozłącz / połącz BT,
- brak restartu ESP,
- Wi-Fi cały czas działa.

## Uwaga
Nie każdy telefon/aplikacja musi udostępniać peer name lub komplet metadata.
Brak nazwy telefonu sam w sobie nie oznacza awarii A2DP/AVRCP.
