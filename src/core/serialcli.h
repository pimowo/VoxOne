#ifndef serialcli_h
#define serialcli_h

#include <Arduino.h>

#define BOOTLOG(...) do { char buf[120]; snprintf(buf, sizeof(buf), __VA_ARGS__); Serial.printf("##[BOOT]#\t%s\n", buf); } while (0)

class SerialCli {
  public:
    SerialCli() {}
    void loop();
    void printf(uint8_t id, const char *format, ...);
    void printf(const char *format, ...);
    void info();
  private:
    char cmBuf[220];
    void handleSerial();
    void on_input(const char* str, uint8_t clientId);
    void printHeapFragmentationInfo(uint8_t id);
};

extern SerialCli serialCli;

#endif
