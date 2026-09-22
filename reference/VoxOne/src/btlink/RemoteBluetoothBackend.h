#pragma once

#include "BtLink.h"

// UART control/status adapter. It deliberately does not acquire I2S; remote
// audio cannot be selected until the separate RX/router stage is tested.
class RemoteBluetoothBackend final : public BtLink {
public:
    RemoteBluetoothBackend(HardwareSerial& serial, int rx, int tx)
        : _serial(serial), _rx(rx), _tx(tx) {}
    bool begin() override;
    void loop() override;
    void play() override { sendControl("PLAY"); }
    void pause() override { sendControl("PAUSE"); }
    void next() override { sendControl("NEXT"); }
    void previous() override { sendControl("PREV"); }
    void setVolume(uint8_t value) override;
    void requestStatus() override { send("GET_STATUS"); }
    const BtLinkStatus& status() const override { return _status; }
    BtModuleState moduleState() const override { return _moduleState; }
    void consumePlayingEdge() override { _status.playingEdge = false; }
private:
    void send(const char* line);
    void sendControl(const char* line);
    void parse(const char* line);
    static bool parseUnsigned(const char* text, uint32_t maximum,
                              uint32_t& value);
    static void copyText(char* destination, size_t capacity,
                         const char* source);
    static const char* commandResultName(BtCommandResult result);
    void logLine(const char* line, bool warning = false) const;
    HardwareSerial& _serial;
    int _rx;
    int _tx;
    char _line[256] = {};
    size_t _length = 0;
    bool _overflow = false;
    BtModuleState _moduleState = BtModuleState::Disabled;
    uint32_t _waitingSince = 0;
    static constexpr uint32_t READY_TIMEOUT_MS = 1500;
    BtLinkStatus _status;
};
