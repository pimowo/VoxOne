# Complete flash image

The versioned `VoxOne-<version>-full.bin` file is intended for the first
installation on a clean ESP32 and for recovery. It contains the bootloader,
partition table, OTA boot data, firmware, and the SPIFFS filesystem with the
web interface.

Building an image for a selected profile:

```powershell
pio run -e <profile> -t fullimage
```

where `<profile>` is `w0`, `m0`, `s0`, `din0` or `l0`. The target builds the
current firmware and SPIFFS before invoking Espressif `esptool merge_bin`,
checks the exact output size and verifies every embedded fragment byte for
byte. The version in the filename is read from `src/core/version.h`.

Every profile currently uses `partitions_yoradio.csv`: NVS at `0x9000`,
`otadata` at `0xe000`, `app0` and `app1` at `0x10000` and `0x1f0000`, and
SPIFFS at `0x3d0000`. Each app slot is `0x1e0000` bytes and SPIFFS is
`0x30000` bytes. The layout occupies the first 4 MiB. M0/S0/DIN0 full images
are 4 MiB. L0 is configured for a 16 MiB full image even though the current
partition table leaves the upper 12 MiB unallocated. The W0 board definition
supplies its flash size; its physical flash capacity must still match the
selected board before installation.

## Flashing on Windows

Replace `<version>` and the serial port if necessary:

```powershell
C:\Users\piotrek\.platformio\penv\Scripts\python.exe C:\Users\piotrek\.platformio\packages\tool-esptoolpy@2.40900.250804\esptool.py --chip esp32 --port COM10 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size 4MB 0x0 .pio\build\w0\VoxOne-<version>-full.bin
```

This writes the complete W0 flash image from offset `0x0`. A full image can
overwrite NVS, `otadata`, both application slots and SPIFFS, and therefore can
erase Wi-Fi credentials and other user settings.

Do not use `full.bin` for normal web/OTA updates. Normal updates should use
the dedicated firmware and/or filesystem image so that user settings remain
intact.

The release contract requires profile-matched artifacts and installation
instructions. A build success alone is not hardware qualification; current
profile and HIL status is documented in [HARDWARE_TARGETS.md](HARDWARE_TARGETS.md)
and [STABLE_CONTRACT.md](STABLE_CONTRACT.md).
