# Complete flash image

The versioned `VoxOne-<version>-full.bin` file is intended for the first
installation on a clean ESP32 and for recovery. It contains the bootloader,
partition table, OTA boot data, firmware, and the SPIFFS filesystem with the
web interface.

Building a release:

```powershell
pio run -e yoradio_esp32 -t fullimage
```

The target builds the current firmware and SPIFFS before invoking Espressif
`esptool merge_bin`. The version in the filename is read from
`src/core/version.h`.

## Flashing on Windows

Replace `<version>` and the serial port if necessary:

```powershell
C:\Users\piotrek\.platformio\penv\Scripts\python.exe C:\Users\piotrek\.platformio\packages\tool-esptoolpy@2.40900.250804\esptool.py --chip esp32 --port COM10 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size 4MB 0x0 .pio\build\yoradio_esp32\VoxOne-<version>-full.bin
```

This writes the complete 4 MB flash image from offset `0x0`. It can overwrite
NVS and therefore erase Wi-Fi credentials and other user settings.

Do not use `full.bin` for normal web/OTA updates. Normal updates should use
the dedicated firmware and/or filesystem image so that user settings remain
intact.
