#include <Arduino.h>
#include <BluetoothA2DPSink.h>
#include <ESP_I2S.h>
#include <freertos/queue.h>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <esp_avrc_api.h>

#include "BtBoard.h"

namespace {
enum class EventKind : uint8_t {
    Connected, Disconnected, Playing, Paused, Artist, Title, Album,
    SampleRate, Volume
};
struct Event {
    EventKind kind;
    uint32_t value = 0;
    char text[129] = {};
};

std::atomic<bool> profileReady{false};
class ReadySink final : public BluetoothA2DPSink {
protected:
    void app_a2d_callback(esp_a2d_cb_event_t event,
                          esp_a2d_cb_param_t* param) override {
        BluetoothA2DPSink::app_a2d_callback(event, param);
        if (event == ESP_A2D_PROF_STATE_EVT && param)
            profileReady.store(param->a2d_prof_stat.init_state ==
                               ESP_A2D_INIT_SUCCESS);
    }
} sink;
A2DPNoVolumeControl passthrough;
I2SClass i2s;
HardwareSerial btUart(2);
QueueHandle_t events = nullptr;
std::atomic<bool> outputReady{false};
std::atomic<bool> connected{false};
std::atomic<bool> playing{false};
std::atomic<uint32_t> pendingRate{0};
uint32_t activeRate = BtBoard::kInitialSampleRate;
int logicalVolume = 25;
char artist[129] = {};
char title[129] = {};
char album[129] = {};
char command[64] = {};
size_t commandLength = 0;
bool commandOverflow = false;

class PcmOutput final : public Print {
public:
    size_t write(uint8_t value) override { return write(&value, 1); }
    size_t write(const uint8_t* data, size_t size) override {
        if (outputReady.load(std::memory_order_relaxed)) {
            ++inFlight;
            if (outputReady.load(std::memory_order_relaxed)) {
                const size_t written = i2s.write(data, size);
                if (written != size) ++shortWrites;
            }
            --inFlight;
        }
        // A2DP's output loop must consume the packet even during reconfigure.
        return size;
    }
    std::atomic<uint32_t> shortWrites{0};
    std::atomic<uint32_t> inFlight{0};
} pcm;

void enqueue(EventKind kind, uint32_t value = 0, const uint8_t* text = nullptr) {
    if (!events) return;
    Event event{};
    event.kind = kind;
    event.value = value;
    if (text) {
        strncpy(event.text, reinterpret_cast<const char*>(text),
                sizeof(event.text) - 1);
    }
    xQueueSend(events, &event, 0); // Callback must never wait for UART.
}

void onConnection(esp_a2d_connection_state_t state, void*) {
    const bool now = state == ESP_A2D_CONNECTION_STATE_CONNECTED;
    connected.store(now);
    if (!now) playing.store(false);
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED)
        enqueue(EventKind::Connected);
    else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
        enqueue(EventKind::Disconnected);
}

void onAudio(esp_a2d_audio_state_t state, void*) {
    const bool now = state == ESP_A2D_AUDIO_STATE_STARTED;
    playing.store(now);
    enqueue(now ? EventKind::Playing : EventKind::Paused);
}

void onMetadata(uint8_t id, const uint8_t* value) {
    if (!value) return;
    if (id == ESP_AVRC_MD_ATTR_ARTIST) enqueue(EventKind::Artist, 0, value);
    if (id == ESP_AVRC_MD_ATTR_TITLE) enqueue(EventKind::Title, 0, value);
    if (id == ESP_AVRC_MD_ATTR_ALBUM) enqueue(EventKind::Album, 0, value);
}

void onRate(uint16_t rate) {
    if (rate != 0) {
        outputReady.store(false);
        pendingRate.store(rate);
    }
}

void onVolume(int raw) {
    enqueue(EventKind::Volume, static_cast<uint32_t>(constrain(raw, 0, 127)));
}

void sendText(const char* key, const char* value) {
    btUart.print(key);
    btUart.print(' ');
    for (const uint8_t* p = reinterpret_cast<const uint8_t*>(value); *p; ++p) {
        if (*p == '%' || *p == '\r' || *p == '\n' || *p < 0x20) {
            const char hex[] = "0123456789ABCDEF";
            btUart.write('%');
            btUart.write(hex[*p >> 4]);
            btUart.write(hex[*p & 15]);
        } else {
            btUart.write(*p);
        }
    }
    btUart.write('\n');
}

void sendStatus() {
    btUart.println("PROTO 1");
    btUart.println(profileReady.load() ? "READY" : "STARTING");
    btUart.println(connected.load() ? "CONNECTED" : "DISCONNECTED");
    btUart.println(playing.load() ? "PLAYING" : "PAUSED");
    btUart.print("SAMPLE_RATE "); btUart.println(activeRate);
    btUart.print("VOLUME "); btUart.println(logicalVolume);
    sendText("ARTIST", artist);
    sendText("TITLE", title);
    sendText("ALBUM", album);
}

void applyEvent(const Event& event) {
    switch (event.kind) {
    case EventKind::Connected:
        btUart.println("CONNECTED");
        break;
    case EventKind::Disconnected:
        artist[0] = title[0] = album[0] = '\0';
        btUart.println("DISCONNECTED");
        sendText("ARTIST", artist);
        sendText("TITLE", title);
        sendText("ALBUM", album);
        break;
    case EventKind::Playing:
        btUart.println("PLAYING");
        btUart.println("BT_STATE_PLAYING");
        break;
    case EventKind::Paused:
        btUart.println("PAUSED");
        break;
    case EventKind::Artist:
    case EventKind::Title:
    case EventKind::Album: {
        char* target = event.kind == EventKind::Artist ? artist :
                       event.kind == EventKind::Title ? title : album;
        memcpy(target, event.text, sizeof(event.text));
        sendText(event.kind == EventKind::Artist ? "ARTIST" :
                 event.kind == EventKind::Title ? "TITLE" : "ALBUM", target);
        break;
    }
    case EventKind::SampleRate:
        btUart.print("SAMPLE_RATE "); btUart.println(event.value);
        break;
    case EventKind::Volume:
        logicalVolume = (static_cast<int>(event.value) * 100 + 63) / 127;
        btUart.print("VOLUME "); btUart.println(logicalVolume);
        break;
    }
}

void handleCommand(const char* line) {
    if (!strcmp(line, "PROTO 1") || !strcmp(line, "GET_STATUS")) {
        sendStatus();
    } else if (!strcmp(line, "PLAY")) {
        if (sink.is_avrc_connected()) sink.play();
        else btUart.println("ERR NOT_CONNECTED");
    } else if (!strcmp(line, "PAUSE")) {
        if (sink.is_avrc_connected()) sink.pause();
        else btUart.println("ERR NOT_CONNECTED");
    } else if (!strcmp(line, "NEXT")) {
        if (sink.is_avrc_connected()) sink.next();
        else btUart.println("ERR NOT_CONNECTED");
    } else if (!strcmp(line, "PREV")) {
        if (sink.is_avrc_connected()) sink.previous();
        else btUart.println("ERR NOT_CONNECTED");
    } else if (!strncmp(line, "SET_VOLUME ", 11)) {
        char* end = nullptr;
        const long value = strtol(line + 11, &end, 10);
        if (end == line + 11 || *end || value < 0 || value > 100) {
            btUart.println("ERR BAD_VOLUME");
            return;
        }
        logicalVolume = static_cast<int>(value);
        sink.set_volume(static_cast<uint8_t>((value * 127 + 50) / 100));
        btUart.print("VOLUME "); btUart.println(logicalVolume);
    } else {
        btUart.println("ERR UNKNOWN_COMMAND");
    }
}
}

void setup() {
    Serial.begin(115200); // USB diagnostic port; separate from link UART.
    btUart.begin(BtBoard::kUartBaud, SERIAL_8N1, BtBoard::kUartRx, BtBoard::kUartTx);
    events = xQueueCreate(12, sizeof(Event));
    if (!events) {
        Serial.println("BT event queue allocation failed");
        return;
    }
    i2s.setPins(BtBoard::kI2sBclk, BtBoard::kI2sLrclk, BtBoard::kI2sData);
    if (!i2s.begin(I2S_MODE_STD, activeRate, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("BT I2S TX init failed");
        btUart.println("ERR I2S_INIT");
        return;
    }
    outputReady.store(true);
    sink.set_auto_reconnect(false);
    sink.set_volume_control(&passthrough); // Main alone applies PCM gain.
    sink.set_output(pcm);
    sink.set_on_connection_state_changed(onConnection);
    sink.set_on_audio_state_changed_post(onAudio);
    sink.set_avrc_metadata_attribute_mask(
        ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST | ESP_AVRC_MD_ATTR_ALBUM);
    sink.set_avrc_metadata_callback(onMetadata);
    sink.set_sample_rate_callback(onRate);
    sink.set_avrc_rn_volumechange(onVolume);
    sink.set_volume(static_cast<uint8_t>((logicalVolume * 127 + 50) / 100));
    sink.start(BtBoard::kDeviceName);
    sendStatus();
}

void loop() {
    static bool readyAnnounced = false;
    if (profileReady.load() && !readyAnnounced) {
        readyAnnounced = true;
        sendStatus();
    }
    const uint32_t rate = pendingRate.exchange(0);
    if (rate && rate != activeRate) {
        if (pcm.inFlight.load()) {
            pendingRate.store(rate);
        } else if (i2s.configureTX(rate, I2S_DATA_BIT_WIDTH_16BIT,
                            I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
            activeRate = rate;
            enqueue(EventKind::SampleRate, rate);
            outputReady.store(true);
        } else {
            Serial.println("BT I2S rate reconfigure failed; PCM muted");
            btUart.println("ERR I2S_RATE");
        }
    } else if (rate) {
        outputReady.store(true);
    }
    Event event{};
    while (events && xQueueReceive(events, &event, 0) == pdTRUE)
        applyEvent(event);
    while (btUart.available()) {
        const char c = static_cast<char>(btUart.read());
        if (c == '\n') {
            if (!commandOverflow) {
                command[commandLength] = '\0';
                if (commandLength && command[commandLength - 1] == '\r')
                    command[--commandLength] = '\0';
                handleCommand(command);
            } else {
                btUart.println("ERR LINE_TOO_LONG");
            }
            commandLength = 0;
            commandOverflow = false;
        } else if (commandLength < sizeof(command) - 1 && !commandOverflow) {
            command[commandLength++] = c;
        } else {
            commandOverflow = true;
        }
    }
}
