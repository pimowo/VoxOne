# VoxOneBT (prototype)

Separate PlatformIO project for a classic ESP32-WROOM-32. Build without
uploading from the VoxOne repository root:

```powershell
& 'C:\Users\piotrek\.platformio\penv\Scripts\pio.exe' run -d VoxOneBT -e voxonebt
```

`include/BtBoard.h` contains provisional GPIO numbers. Confirm the exact
D1-mini-format board's PCB/pinout before connecting or uploading. UART2 is
3.3 V and crossed TX/RX to Main, with common ground. I2S is TX master PCM
to a **future Main I2S RX slave**, not to the DAC. See
[`docs/VOXONE_BT_PROTOCOL.md`](../docs/VOXONE_BT_PROTOCOL.md).

This project keeps A2DP/AVRCP running independently. It does not include
Wi-Fi, web setup, radio or display. DESK Main still uses local Bluetooth;
compilation of this project is not a two-board hardware validation.
