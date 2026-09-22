# RECOVERY

## Aktualnie

- recovery AP,
- restart,
- przewodowe wgranie firmware przez USB/serial.

VoxOne nie obsługuje OTA ani rollbacku OTA. Jeden slot aplikacji factory ma
rozmiar 0x3E0000. Zmiana tabeli partycji wymaga pierwszego wgrania przewodowego
bootloadera, tabeli i aplikacji; procedura: [układ partycji](../docs_FINAL_PARTITIONS.md).
Nie wykonuj pełnego erase_flash, jeśli chcesz zachować konfigurację NVS.

## Docelowo

- restart modułu przed restartem ESP,
- safe mode po kilku nieudanych bootach,
- minimalne WWW i diagnostyka,
- recovery config/playlist z `.bak`.
