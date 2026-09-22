# VoxOne

Autonomiczny moduł audio na klasycznym ESP32.

Aktualny stan: **0.4.0 z lokalną zmianą na układ bez OTA; test przewodowego wgrania oczekuje na wykonanie**.
Aktualna wersja firmware: **0.4.0**.

## Aktualizacje firmware

VoxOne nie obsługuje OTA. Aktualizacje wykonuje się przez USB/serial.
Jeden slot aplikacji factory ma rozmiar 0x3E0000 = 4 063 232 B.
Zmiana tabeli partycji wymaga pierwszego wgrania przewodowego także bootloadera
i tabeli partycji. NVS i coredump zachowują dotychczasowe offsety i rozmiary.
Procedura: [układ partycji i upload](docs_FINAL_PARTITIONS.md).

## Wersjonowanie

VoxOne używa Semantic Versioning MAJOR.MINOR.PATCH. Wersje 0.x.y oznaczają okres
przed stabilnym 1.0.0; PATCH oznacza poprawki błędów, MINOR nowe funkcje, a MAJOR
niekompatybilne zmiany architektury lub API. Nazwy M1, M2.1 i M2.2 pozostają nazwami
historycznych milestone'ów.

## Struktura

```text
VoxOne/
├── include/
│   ├── AppConfig.h
│   ├── BoardConfig.h
│   └── BuildInfo.h
├── src/
│   ├── audio/
│   ├── core/
│   ├── diagnostics/
│   ├── hal/
│   ├── network/
│   ├── storage/
│   ├── ui/
│   └── main.cpp
├── docs/
│   ├── MASTER_SPEC.md
│   ├── ARCHITECTURE.md
│   ├── HARDWARE_PROFILES.md
│   ├── STATE_MACHINE.md
│   ├── WEB_API.md
│   ├── WEB_UI.md
│   ├── STORAGE.md
│   ├── RECOVERY.md
│   ├── MQTT.md
│   ├── YORADIO_COMPAT.md
│   ├── TEST_PLAN.md
│   └── ROADMAP.md
├── CHANGELOG.md
└── platformio.ini
```

## M1 — gotowe
- sprzęt referencyjny
- TFT
- enkoder
- PCM5102A
- Wi-Fi
- AP setup
- WWW
- mDNS
- OTA przez WWW (historycznie; obecnie usunięte)

## Następny etap
**M2 — Bluetooth A2DP + AVRCP**
