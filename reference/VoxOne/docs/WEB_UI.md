# WEB UI

## Aktualny interfejs

- status firmware i Bluetooth/AVRCP,
- sterowanie odtwarzaniem i głośnością,
- konfiguracja Wi-Fi,
- restart.

VoxOne nie obsługuje OTA. Firmware aktualizuje się przez USB/serial.
WWW konfiguracyjne pozostaje dostępne; brak formularza aktualizacji firmware.

## Display runtime

The ST7789 layout, fonts, colors and viewports are rendered by a dedicated
DisplayTask pinned to core 0. App submits coalesced UI snapshots and never
performs LCD SPI rendering. DisplayTask is the sole ST7789/SPI owner.

## Docelowo

- Status / Now Playing
- Radio
- Bluetooth
- MQTT / HA
- Audio
- Network / System
- Diagnostics / Service
- Backup / Restore / Factory reset
