# DINaudio M2.1 — A2DP + Wi-Fi coexistence test

Firmware: `0.2.0-m2.1`

Dokument historyczny dawnego DINaudio (obecnie VoxOne): obecnie VoxOne nie obsługuje OTA i jest aktualizowane
przez USB/serial. Opis testów OTA poniżej dotyczy wcześniejszego milestone'u.

## Cel

Ten milestone ma odpowiedzieć na jedno pytanie:

Czy na sprzęcie referencyjnym DINaudio stabilnie współpracują jednocześnie:
- Bluetooth Classic A2DP Sink,
- PCM5102A / I2S,
- Wi-Fi,
- WWW,
- OTA?

M2.1 nie jest jeszcze finalnym modułem Bluetooth.

## Zakres

Zaimplementowane:
- A2DP Sink,
- nazwa `DINaudio-XXXXXX`,
- PCM5102A jako wyjście A2DP,
- stan BT w `StateStore`,
- status BT na TFT,
- status BT na WWW,
- lokalna głośność DINaudio 0–100 mapowana na A2DP 0–127.

Celowo jeszcze brak:
- AVRCP Play/Pause/Next/Prev,
- metadata,
- nazwy telefonu,
- pairing managera,
- ręcznego okna discoverable 120 s,
- finalnego auto reconnect,
- Source Manager RADIO/BT/PLAY_MEDIA.

## Test

1. Uruchom DINaudio.
2. Sprawdź TFT: `DINaudio M2.1`.
3. Sprawdź, czy urządzenie nadal łączy się z Wi-Fi.
4. Otwórz WWW.
5. W telefonie wyszukaj `DINaudio-XXXXXX`.
6. Połącz telefon.
7. Sprawdź na WWW `CONNECTED`.
8. Uruchom muzykę.
9. Sprawdź dźwięk stereo przez PCM5102A.
10. Sprawdź na TFT `BT PLAY`.
11. Podczas grania:
    - kilka razy odśwież WWW,
    - zmień głośność enkoderem,
    - zmień głośność z WWW,
    - zatrzymaj/wznów muzykę na telefonie.
12. Rozłącz i ponownie połącz telefon.
13. Sprawdź, czy Wi-Fi i WWW nadal działają.
14. Sprawdź, czy nie było restartu ESP32.

## Wynik

M2.1 uznajemy za zaliczony dopiero po stabilnym teście BT + Wi-Fi + WWW + I2S.
