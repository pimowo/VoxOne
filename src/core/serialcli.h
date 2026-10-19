#ifndef serialcli_h
#define serialcli_h

#include <Arduino.h>
#include "serialcli_input.h"

#ifndef VOXONE_SERIALCLI_RX_ENABLED
#define VOXONE_SERIALCLI_RX_ENABLED 1
#endif

static_assert(VOXONE_SERIALCLI_RX_ENABLED == 0 ||
                  VOXONE_SERIALCLI_RX_ENABLED == 1,
              "VOXONE_SERIALCLI_RX_ENABLED must be 0 or 1");

#define BOOTLOG(...) do { char buf[120]; snprintf(buf, sizeof(buf), __VA_ARGS__); Serial.printf("##[BOOT]#\t%s\n", buf); } while (0)

class SerialCli {
  public:
    SerialCli() {}
    void loop();
    void printf(uint8_t id, const char *format, ...);
    void printf(const char *format, ...);
    void info();
  private:
    void handleSerial();
    void on_input(const char* str, uint8_t clientId);
    void commandError(uint8_t clientId);
    void printHeapFragmentationInfo(uint8_t id);
    voxone::serialcli::InputFramer<(VOXONE_SERIALCLI_RX_ENABLED != 0)> input_;
    voxone::serialcli::CommandErrorLimiter commandErrorLimiter_;
};

extern SerialCli serialCli;

#endif
