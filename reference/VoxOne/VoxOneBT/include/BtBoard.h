#pragma once

// Provisional ESP32-WROOM-32 D1-mini-format profile. Verify the actual PCB
// silkscreen/schematic before wiring. Never connect I2S DATA directly to DAC.
namespace BtBoard {
constexpr int kI2sBclk = 26;
constexpr int kI2sLrclk = 25;
constexpr int kI2sData = 27;
constexpr int kUartTx = 17;
constexpr int kUartRx = 16;
constexpr unsigned long kUartBaud = 115200;
constexpr uint32_t kInitialSampleRate = 44100;
constexpr char kDeviceName[] = "VoxOneBT";
}
