#include "PerfDiagnostics.h"

#ifdef VOXONE_DEBUG

#include <esp_heap_caps.h>
#include <limits>

#include "Logger.h"

namespace {
struct DurationStats {
    uint32_t current = 0;
    uint32_t maximum = 0;
    uint32_t total = 0;
    uint32_t count = 0;
    uint32_t over5 = 0;
    uint32_t over10 = 0;
    uint32_t over20 = 0;
    uint32_t over50 = 0;
    uint32_t over100 = 0;

    void add(uint32_t us) {
        current = us;
        maximum = max(maximum, us);
        total += us;
        ++count;
        if (us > 5000) ++over5;
        if (us > 10000) ++over10;
        if (us > 20000) ++over20;
        if (us > 50000) ++over50;
        if (us > 100000) ++over100;
    }

    void clear() { *this = {}; }
};

struct PerfState {
    DurationStats loop;
    DurationStats unaccounted;
    DurationStats appControl;
    DurationStats radio;
    DurationStats radioControl;
    DurationStats radioIcy;
    DurationStats radioFrame;
    DurationStats radioDecode;
    DurationStats radioPcm;
    DurationStats playMedia;
    DurationStats playMediaNetwork;
    DurationStats playMediaDecode;
    DurationStats playMediaPcm;
    DurationStats btLink;
    DurationStats time;
    DurationStats diagnostics;
    DurationStats yield;
    DurationStats netAvailable;
    DurationStats netRead;
    DurationStats netConnected;
    DurationStats i2s;
    DurationStats ui;
    DurationStats uiFull;
    DurationStats uiVolume;
    DurationStats uiMetadata;
    DurationStats uiClock;
    DurationStats mqttLoop;
    DurationStats mqttPublish;
    DurationStats web;
    DurationStats wifi;
    DurationStats encoder;
    DurationStats commands;
    DurationStats log;
    size_t bufferCurrent = 0;
    size_t bufferMinimum = std::numeric_limits<size_t>::max();
    size_t bufferMaximum = 0;
    uint32_t starvation = 0;
    uint32_t decoderWaiting = 0;
    uint32_t i2sShort = 0;
    uint32_t lastReportMs = 0;
    uint32_t iterationStartedUs = 0;
    uint32_t iterationAccountedUs = 0;
    bool iterationActive = false;
} state;

portMUX_TYPE perfMux = portMUX_INITIALIZER_UNLOCKED;

DurationStats& duration(PerfArea area) {
    switch (area) {
        case PerfArea::AppLoop: return state.loop;
        case PerfArea::AppControl: return state.appControl;
        case PerfArea::Radio: return state.radio;
        case PerfArea::RadioControl: return state.radioControl;
        case PerfArea::RadioIcy: return state.radioIcy;
        case PerfArea::RadioFrame: return state.radioFrame;
        case PerfArea::RadioDecode: return state.radioDecode;
        case PerfArea::RadioPcm: return state.radioPcm;
        case PerfArea::PlayMedia: return state.playMedia;
        case PerfArea::PlayMediaNetwork: return state.playMediaNetwork;
        case PerfArea::PlayMediaDecode: return state.playMediaDecode;
        case PerfArea::PlayMediaPcm: return state.playMediaPcm;
        case PerfArea::BtLink: return state.btLink;
        case PerfArea::Time: return state.time;
        case PerfArea::Diagnostics: return state.diagnostics;
        case PerfArea::Yield: return state.yield;
        case PerfArea::Ui: return state.ui;
        case PerfArea::UiFull: return state.uiFull;
        case PerfArea::UiVolume: return state.uiVolume;
        case PerfArea::UiMetadata: return state.uiMetadata;
        case PerfArea::UiClock: return state.uiClock;
        case PerfArea::MqttLoop: return state.mqttLoop;
        case PerfArea::MqttPublish: return state.mqttPublish;
        case PerfArea::Web: return state.web;
        case PerfArea::Wifi: return state.wifi;
        case PerfArea::Encoder: return state.encoder;
        case PerfArea::Commands: return state.commands;
        default: return state.log;
    }
}

bool countsTowardIteration(PerfArea area) {
    switch (area) {
        case PerfArea::AppControl:
        case PerfArea::Radio:
        case PerfArea::RadioControl:
        case PerfArea::PlayMedia:
        case PerfArea::BtLink:
        case PerfArea::Time:
        case PerfArea::Diagnostics:
        case PerfArea::Yield:
        case PerfArea::MqttLoop:
        case PerfArea::Web:
        case PerfArea::Wifi:
        case PerfArea::Encoder:
        case PerfArea::Commands:
            return true;
        default:
            return false;
    }
}

uint32_t average(const DurationStats& value) {
    return value.count ? value.total / value.count : 0;
}

void clearWindow() {
    state.loop.clear();
    state.unaccounted.clear();
    state.appControl.clear();
    state.radio.clear();
    state.radioControl.clear();
    state.radioIcy.clear();
    state.radioFrame.clear();
    state.radioDecode.clear();
    state.radioPcm.clear();
    state.playMedia.clear();
    state.playMediaNetwork.clear();
    state.playMediaDecode.clear();
    state.playMediaPcm.clear();
    state.btLink.clear();
    state.time.clear();
    state.diagnostics.clear();
    state.yield.clear();
    state.netAvailable.clear();
    state.netRead.clear();
    state.netConnected.clear();
    state.i2s.clear();
    state.ui.clear();
    state.uiFull.clear();
    state.uiVolume.clear();
    state.uiMetadata.clear();
    state.uiClock.clear();
    state.mqttLoop.clear();
    state.mqttPublish.clear();
    state.web.clear();
    state.wifi.clear();
    state.encoder.clear();
    state.commands.clear();
    state.log.clear();
    state.bufferMinimum = state.bufferCurrent;
    state.bufferMaximum = state.bufferCurrent;
    state.starvation = 0;
    state.decoderWaiting = 0;
    state.i2sShort = 0;
}
}

void PerfDiagnostics::beginLoopIteration() {
    portENTER_CRITICAL(&perfMux);
    state.iterationStartedUs = micros();
    state.iterationAccountedUs = 0;
    state.iterationActive = true;
    portEXIT_CRITICAL(&perfMux);
}

void PerfDiagnostics::endLoopIteration() {
    portENTER_CRITICAL(&perfMux);
    if (!state.iterationActive) {
        portEXIT_CRITICAL(&perfMux);
        return;
    }
    const uint32_t totalUs = micros() - state.iterationStartedUs;
    state.loop.add(totalUs);
    state.unaccounted.add(totalUs > state.iterationAccountedUs
        ? totalUs - state.iterationAccountedUs : 0);
    state.iterationActive = false;
    portEXIT_CRITICAL(&perfMux);
}

void PerfDiagnostics::recordDuration(PerfArea area, uint32_t us) {
    portENTER_CRITICAL(&perfMux);
    duration(area).add(us);
    if (state.iterationActive && countsTowardIteration(area))
        state.iterationAccountedUs += us;
    portEXIT_CRITICAL(&perfMux);
}

void PerfDiagnostics::recordRadioBuffer(size_t fill) {
    portENTER_CRITICAL(&perfMux);
    state.bufferCurrent = fill;
    state.bufferMinimum = min(state.bufferMinimum, fill);
    state.bufferMaximum = max(state.bufferMaximum, fill);
    portEXIT_CRITICAL(&perfMux);
}

void PerfDiagnostics::recordRadioStarvation() {
    portENTER_CRITICAL(&perfMux);
    ++state.starvation;
    portEXIT_CRITICAL(&perfMux);
}
void PerfDiagnostics::recordDecoderWaiting() {
    portENTER_CRITICAL(&perfMux);
    ++state.decoderWaiting;
    portEXIT_CRITICAL(&perfMux);
}
void PerfDiagnostics::recordNetworkAvailable(uint32_t us) {
    portENTER_CRITICAL(&perfMux);
    state.netAvailable.add(us);
    portEXIT_CRITICAL(&perfMux);
}
void PerfDiagnostics::recordNetworkRead(uint32_t us) {
    portENTER_CRITICAL(&perfMux);
    state.netRead.add(us);
    portEXIT_CRITICAL(&perfMux);
}
void PerfDiagnostics::recordNetworkConnected(uint32_t us) {
    portENTER_CRITICAL(&perfMux);
    state.netConnected.add(us);
    portEXIT_CRITICAL(&perfMux);
}

void PerfDiagnostics::recordI2sWrite(uint32_t us, size_t requested,
                                     size_t written) {
    portENTER_CRITICAL(&perfMux);
    state.i2s.add(us);
    if (written != requested) ++state.i2sShort;
    portEXIT_CRITICAL(&perfMux);
}

void PerfDiagnostics::reportIfDue(bool radioRunning) {
    const uint32_t now = millis();
    PerfState report;
    portENTER_CRITICAL(&perfMux);
    if (!radioRunning || now - state.lastReportMs < 5000) {
        portEXIT_CRITICAL(&perfMux);
        return;
    }
    state.lastReportMs = now;
    report = state;
    clearWindow();
    portEXIT_CRITICAL(&perfMux);
    const size_t minimum = report.bufferMinimum == std::numeric_limits<size_t>::max()
        ? report.bufferCurrent : report.bufferMinimum;
    Logger::debug("PERF",
        "RADIO PERF buf=" + String(report.bufferCurrent) +
        " min=" + String(minimum) +
        " max=" + String(report.bufferMaximum) +
        " starve=" + String(report.starvation) +
        " need=" + String(report.decoderWaiting));
    Logger::debug("PERF",
        "loop_us cur=" + String(report.loop.current) +
        " max=" + String(report.loop.maximum) +
        " unaccounted_cur/max=" + String(report.unaccounted.current) + "/" +
        String(report.unaccounted.maximum) +
        " >5/10/20/50/100ms=" + String(report.loop.over5) + "/" +
        String(report.loop.over10) + "/" + String(report.loop.over20) + "/" +
        String(report.loop.over50) + "/" + String(report.loop.over100));
    Logger::debug("PERF",
        "services_us radio/retry/playmedia/bt/app=" +
        String(report.radio.maximum) + "/" + String(report.radioControl.maximum) +
        "/" + String(report.playMedia.maximum) + "/" +
        String(report.btLink.maximum) + "/" + String(report.appControl.maximum));
    Logger::debug("PERF",
        "radio_us icy/frame/decode/pcm=" + String(report.radioIcy.maximum) + "/" +
        String(report.radioFrame.maximum) + "/" + String(report.radioDecode.maximum) +
        "/" + String(report.radioPcm.maximum) +
        " net_read_avg/max=" + String(average(report.netRead)) + "/" +
        String(report.netRead.maximum) +
        " avail/connected_max=" + String(report.netAvailable.maximum) + "/" +
        String(report.netConnected.maximum));
    Logger::debug("PERF",
        "playmedia_us net/decode/pcm=" + String(report.playMediaNetwork.maximum) +
        "/" + String(report.playMediaDecode.maximum) + "/" +
        String(report.playMediaPcm.maximum) +
        " i2s_avg/max=" + String(average(report.i2s)) + "/" +
        String(report.i2s.maximum) + " short=" + String(report.i2sShort));
    Logger::debug("PERF",
        "ui_us total/full/vol/meta/clock=" + String(report.ui.maximum) + "/" +
        String(report.uiFull.maximum) + "/" + String(report.uiVolume.maximum) + "/" +
        String(report.uiMetadata.maximum) + "/" + String(report.uiClock.maximum));
    Logger::debug("PERF",
        "mqtt_us loop/pub=" + String(report.mqttLoop.maximum) + "/" +
        String(report.mqttPublish.maximum) +
        " web/wifi/time/enc/cmd=" + String(report.web.maximum) + "/" +
        String(report.wifi.maximum) + "/" + String(report.time.maximum) + "/" +
        String(report.encoder.maximum) + "/" + String(report.commands.maximum));
    Logger::debug("PERF",
        "diag_us report/yield/log=" + String(report.diagnostics.maximum) + "/" +
        String(report.yield.maximum) + "/" + String(report.log.maximum) +
        " heap=" + String(ESP.getFreeHeap()) +
        " minHeap=" + String(ESP.getMinFreeHeap()) +
        " intFree=" + String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) +
        " intLargest=" +
        String(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

#endif
