# VoxOne — układ partycji 4 MB bez OTA

VoxOne nie obsługuje OTA. Firmware aktualizuje się wyłącznie przez USB/serial.
Urządzenie wykorzystuje jeden duży slot aplikacji; zmiana tabeli partycji wymaga
pierwszego wgrania przewodowego całego zestawu obrazów PlatformIO.

## Układ

```csv
# Name, Type, SubType, Offset, Size, Flags
nvs, data, nvs, 0x9000, 0x5000,
app, app, factory, 0x10000, 0x3E0000,
coredump, data, coredump, 0x3F0000, 0x10000,
```

- NVS: 20 KiB, dotychczasowy offset i rozmiar zachowane.
- Aplikacja: 4 063 232 B, zakres 0x10000 .. 0x3EFFFF.
- Coredump: 64 KiB, zakres 0x3F0000 .. 0x3FFFFF.
- Brak otadata i partycji ota_0/ota_1. Subtype factory jest domyślną aplikacją bootloadera.
- Obszar 0xE000 .. 0xFFFF pozostaje poza partycjami danych.
- Brak filesystemu; aktualna konfiguracja pozostaje w NVS/Preferences.

## Pierwsze wgranie i kolejne aktualizacje

1. Podłącz ESP32 przez USB i ustal port COM.
2. Zamknij monitor serial, który może zajmować port.
3. Wykonaj build: `pio run -e voxone`.
4. Po SUCCESS wykonaj `pio run -e voxone -t upload --upload-port COMx`,
   zastępując COMx rzeczywistym portem. Nie wgrywaj wyłącznie firmware.bin:
   pierwsze wgranie musi zapisać także bootloader i partitions.bin.
5. Standardowy upload PlatformIO zapisuje obrazy przy właściwych offsetach.
   Nie uruchamiaj erase/erase_flash: pełne kasowanie usuwa konfigurację NVS.
   Układ zachowuje NVS, a standardowy upload nie zapisuje zakresu 0x9000 .. 0xDFFF.
6. Jeśli automatyczne wejście w bootloader nie działa, użyj przycisków BOOT/EN
   podczas łączenia narzędzia upload.
7. Sprawdź konfigurację Wi-Fi, WWW, mDNS, TFT, Bluetooth A2DP/AVRCP,
   metadata, głośność i reconnect grace period.
8. Potwierdź brak przycisku OTA i odpowiedź 404 dla GET/POST /update.

Kolejne aktualizacje również wymagają USB/serial. Nie ma aktualizacji przez WWW
ani rollbacku OTA. Nie wykonano jeszcze sprzętowego testu nowego układu.
