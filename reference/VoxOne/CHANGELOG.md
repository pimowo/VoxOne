# Changelog`r`n`r`n## 0.4.0`r`n- Bluetooth ownership przeniesiony do App; reconnect grace period wynosi 10 s.`r`n- BT ownership/reconnect, ekran BT RECONNECT oraz powrót telefonu w grace period zostały przetestowane sprzętowo.`r`n- Config schema 3; migracja 1 → 2 → 3 zachowuje istniejącą konfigurację i ustawia reconnectDelayMs = 10000.

## 0.3.0

## Minimal MP3 RadioService checkpoint

- Minimal RadioService is code-complete for HTTP MP3, Helix MP3, start/stop/loop and PCM through AudioOutputManager.
- Helix MP3 is confirmed at build/link level; AAC is not linked.
- Bluetooth and Radio have exclusive I2S ownership and never write PCM5102A concurrently.
- Hardware audio test and new partition-table test are pending.
- Runtime BT -> Radio -> BT still requires hardware testing.
- ICY metadata, stream reconnect, station list and AAC are not implemented.
- Przebudowany ekran ST7789 284×76 w układzie inspirowanym yoRadio DSP_ST7789_76.
- Dodano TimeService z NTP i strefą Europe/Warsaw (CET/CEST).
- Ekran i zegar 0.3.0 zostały przetestowane sprzętowo.

## 0.2.1
- Wewnętrzny fundament konfiguracji runtime: ConfigModel, ConfigManager i schemaVersion 1.
- Migracja istniejącej konfiguracji NVS (wifi_ssid, wifi_pass, volume) została przetestowana sprzętowo.

## 0.2.0
- M2.2 — aktualna wersja firmware po sprzętowym zaliczeniu milestone'u
- sprzętowo potwierdzone AVRCP Play/Pause/Next/Previous
- dwukierunkowa synchronizacja głośności z telefonem
- metadata artist/title oraz peer/device info
- stabilna praca Bluetooth A2DP, Wi-Fi, WWW i OTA równolegle
- TFT z metadata i częściowym odświeżaniem bez migotania

Wersjonowanie firmware stosuje Semantic Versioning MAJOR.MINOR.PATCH.

## 0.1.1-m1
- pierwsze potwierdzone OTA przez WWW
- marker `OTA TEST OK`
- poprawka mDNS dla Arduino-ESP32 3.1.3

## 0.1.0-m1
- pierwszy właściwy milestone pod dawną nazwą DINaudio (obecnie VoxOne)
- StateStore
- CommandQueue
- TFT
- enkoder
- PCM5102A test
- Wi-Fi
- AP konfiguracji
- WWW
- mDNS
- OTA

## 0.4.0 — shared audio output ownership checkpoint

- Dodano wspólny `AudioOutputManager` będący wyłącznym właścicielem fizycznego I2S/PCM5102A.
- Przygotowano lifecycle Bluetooth `suspend/resume` pod przyszłe przełączanie źródeł.
- Czasowe zatrzymanie A2DP używa `end(false)`; `end(true)` nie jest używane.
- Reconnect grace 10 s pozostaje bez zmian i nie wykonuje teardown A2DP.
- Fundament pod przyszły RadioService jest gotowy kodowo; radio ani 0.5.0 nie zostały zaimplementowane.
- Runtime suspend/resume oraz test sprzętowy nowej tabeli partycji pozostają pending.
