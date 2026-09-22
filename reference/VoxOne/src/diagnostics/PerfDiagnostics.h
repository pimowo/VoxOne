#pragma once

#include <Arduino.h>

enum class PerfArea : uint8_t {
    AppLoop,
    AppControl,
    Radio,
    RadioControl,
    RadioIcy,
    RadioFrame,
    RadioDecode,
    RadioPcm,
    PlayMedia,
    PlayMediaNetwork,
    PlayMediaDecode,
    PlayMediaPcm,
    BtLink,
    Time,
    Diagnostics,
    Yield,
    Ui,
    UiFull,
    UiVolume,
    UiMetadata,
    UiClock,
    MqttLoop,
    MqttPublish,
    Web,
    Wifi,
    Encoder,
    Commands,
    Log
};

class PerfDiagnostics {
public:
#ifdef VOXONE_DEBUG
    static void beginLoopIteration();
    static void endLoopIteration();
    static void recordDuration(PerfArea area, uint32_t us);
    static void recordRadioBuffer(size_t fill);
    static void recordRadioStarvation();
    static void recordDecoderWaiting();
    static void recordNetworkAvailable(uint32_t us);
    static void recordNetworkRead(uint32_t us);
    static void recordNetworkConnected(uint32_t us);
    static void recordI2sWrite(uint32_t us, size_t requested, size_t written);
    static void reportIfDue(bool radioRunning);
#else
    static void beginLoopIteration() {}
    static void endLoopIteration() {}
    static void recordDuration(PerfArea, uint32_t) {}
    static void recordRadioBuffer(size_t) {}
    static void recordRadioStarvation() {}
    static void recordDecoderWaiting() {}
    static void recordNetworkAvailable(uint32_t) {}
    static void recordNetworkRead(uint32_t) {}
    static void recordNetworkConnected(uint32_t) {}
    static void recordI2sWrite(uint32_t, size_t, size_t) {}
    static void reportIfDue(bool) {}
#endif
};

class PerfScope {
public:
#ifdef VOXONE_DEBUG
    explicit PerfScope(PerfArea area) : _area(area), _started(micros()) {}
    ~PerfScope() {
        PerfDiagnostics::recordDuration(_area, micros() - _started);
    }
private:
    PerfArea _area;
    uint32_t _started;
#else
    explicit PerfScope(PerfArea) {}
#endif
};
