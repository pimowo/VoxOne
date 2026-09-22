#include "DisplayService.h"
#include "PolishGlyphs.h"

#include "AppConfig.h"
#include "BoardConfig.h"
#include "../core/StateStore.h"
#include "../diagnostics/Logger.h"
#include "../diagnostics/PerfDiagnostics.h"
#include <Fonts/FreeSans9pt7b.h>
#include <algorithm>
#include <climits>
#include <new>

class ReusableCanvas16 final : public Adafruit_GFX {
public:
    ReusableCanvas16(int16_t maxWidth, int16_t maxHeight)
        : Adafruit_GFX(maxWidth, maxHeight),
          _capacity(static_cast<size_t>(maxWidth) * maxHeight),
          _buffer(new (std::nothrow) uint16_t[_capacity]) {
        setTextWrap(false);
    }

    bool ready() const { return _buffer != nullptr; }

    bool configure(int16_t width, int16_t height) {
        if (!_buffer || width <= 0 || height <= 0 ||
            static_cast<size_t>(width) * height > _capacity) {
            return false;
        }
        _width = width;
        _height = height;
        rotation = 0;
        return true;
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (!_buffer || x < 0 || y < 0 || x >= _width || y >= _height) return;
        _buffer[static_cast<size_t>(y) * _width + x] = color;
    }

    void fillScreen(uint16_t color) override {
        if (_buffer)
            std::fill_n(_buffer.get(), static_cast<size_t>(_width) * _height,
                        color);
    }

    uint16_t* buffer() { return _buffer.get(); }

private:
    size_t _capacity;
    std::unique_ptr<uint16_t[]> _buffer;
};

namespace {
constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return static_cast<uint16_t>(
        ((red & 0xF8) << 8) |
        ((green & 0xFC) << 3) |
        (blue >> 3)
    );
}

static constexpr uint16_t kColorBlack = rgb565(0, 0, 0);
static constexpr uint16_t kColorBackground = rgb565(6, 8, 12);
static constexpr uint16_t kColorSurface = rgb565(18, 22, 28);
static constexpr uint16_t kColorPrimaryText = rgb565(245, 245, 245);
static constexpr uint16_t kColorSonyBlue = rgb565(120, 170, 255);
static constexpr uint16_t kColorSonyBlueDark = rgb565(90, 140, 230);
static constexpr uint16_t kColorSecondaryText = rgb565(180, 200, 220);
static constexpr uint16_t kColorInfo = rgb565(80, 160, 220);
static constexpr uint16_t kColorNeutral = rgb565(200, 200, 200);
static constexpr uint16_t kColorDivider = rgb565(40, 90, 160);
static constexpr uint16_t kColorInactive = rgb565(40, 50, 65);
static constexpr uint16_t kColorSoftBlue = rgb565(180, 200, 255);
static constexpr int DSP_WIDTH = 284;
static constexpr int HEADER_H = 19;
static constexpr int STATION_Y = 1;
static constexpr int ARTIST_BASELINE_Y = 35;
static constexpr int TITLE2_Y = 42;
static constexpr int INFO1_Y = 60;
static constexpr int METADATA_W = 174;
static constexpr int STATUS_W = 112;

static constexpr int WIFI_Y = 63;
static constexpr uint32_t WIFI_DRAW_INTERVAL_MS = 15000;

static constexpr int CLOCK_X = 218;
static constexpr int CLOCK_Y = 36;
static constexpr int CLOCK_W = 64;
static constexpr int CLOCK_H = 18;

static constexpr int VOLUME_Y = 60;
static constexpr int SPEAKER_SCALE = 2;
// yoRadio yofont5x7.c, glyph 0x13 (speaker), five 7-bit columns.
static constexpr uint8_t SPEAKER_GLYPH[] = {0x00, 0x00, 0x18, 0x3C, 0x7E};
// yoRadio yofont5x7.c glyphs 0x11 (prev), 0x0E (note), 0x10 (next).
static constexpr uint8_t NAV_PREV[] = {0x08, 0x1C, 0x3E, 0x7F, 0x00};
static constexpr uint8_t NAV_NOTE[] = {0x60, 0x7F, 0x05, 0x35, 0x3F};
static constexpr uint8_t NAV_NEXT[] = {0x00, 0x7F, 0x3E, 0x1C, 0x08};

static constexpr int LEFT_X = 2;
static constexpr int METADATA_SAFETY_GAP = 5;
static constexpr int METADATA_RIGHT_EDGE = CLOCK_X - METADATA_SAFETY_GAP - 8;
static constexpr int METADATA_VIEWPORT_W = METADATA_RIGHT_EDGE - LEFT_X;
static constexpr uint32_t SCROLL_HOLD_MS = 5000;
static constexpr uint32_t SCROLL_STEP_MS = 30;
static constexpr int SCROLL_CANVAS_H = 20;
static constexpr char SCROLL_SEPARATOR[] = " | ";

static constexpr uint32_t DISPLAY_TASK_STACK_BYTES = 4096;
static constexpr UBaseType_t DISPLAY_TASK_PRIORITY = 2;
static constexpr BaseType_t DISPLAY_TASK_CORE = 0;
static constexpr uint32_t DISPLAY_TASK_INTERVAL_MS = 10;
static constexpr uint32_t DISPLAY_NOTIFY_RUNTIME = 1u << 0;
static constexpr uint32_t DISPLAY_NOTIFY_INPUT = 1u << 1;
static constexpr uint32_t DISPLAY_NOTIFY_REDRAW = 1u << 2;
static constexpr uint32_t DISPLAY_NOTIFY_CLEAR = 1u << 3;

}

DisplayService::DisplayService(const St7789Pins& pins)
    : _pins(pins), _tft(pins.cs, pins.dc, pins.rst) {}

DisplayService::~DisplayService() = default;

void DisplayService::begin() {
    _inputMutex = xSemaphoreCreateMutex();
    _initDone = xSemaphoreCreateBinary();
    _clearDone = xSemaphoreCreateBinary();
    if (!_inputMutex || !_initDone || !_clearDone) {
        Logger::error("DISPLAY", "DisplayTask synchronization allocation failed");
        return;
    }
    if (xTaskCreatePinnedToCore(
            displayTaskEntry,
            "DisplayTask",
            DISPLAY_TASK_STACK_BYTES,
            this,
            DISPLAY_TASK_PRIORITY,
            &_displayTask,
            DISPLAY_TASK_CORE
        ) != pdPASS) {
        _displayTask = nullptr;
        Logger::error("DISPLAY", "DisplayTask creation failed");
        return;
    }
    if (xSemaphoreTake(_initDone, pdMS_TO_TICKS(3000)) != pdTRUE)
        Logger::error("DISPLAY", "DisplayTask initialization timed out");
}

void DisplayService::displayTaskEntry(void* context) {
    static_cast<DisplayService*>(context)->displayTaskLoop();
    vTaskDelete(nullptr);
}

void DisplayService::initializeDisplay() {
    SPI.begin(
        _pins.sck,
        -1,
        _pins.mosi,
        _pins.cs
    );

    _tft.init(Board::TFT_INIT_W, Board::TFT_INIT_H);
    _tft.setRotation(Board::TFT_ROTATION);
    _tft.invertDisplay(false);
    _tft.setTextWrap(false);
    _initialized = true;
    _startupActive = true;
    showStartupScreen();
}

void DisplayService::displayTaskLoop() {
    initializeDisplay();
    xSemaphoreGive(_initDone);

    while (true) {
        uint32_t notifications = 0;
        const TickType_t wait = _runtimeActive
            ? pdMS_TO_TICKS(DISPLAY_TASK_INTERVAL_MS)
            : portMAX_DELAY;
        xTaskNotifyWait(0, ULONG_MAX, &notifications, wait);

        if (notifications & DISPLAY_NOTIFY_CLEAR) {
            _clearResult = false;
            if (_initialized) {
                _tft.fillScreen(kColorBlack);
                _layoutDrawn = false;
                _visualDisabled = true;
                _clearResult = true;
            }
            xSemaphoreGive(_clearDone);
            continue;
        }

        if ((notifications & DISPLAY_NOTIFY_RUNTIME) && !_runtimeActive) {
            finishStartupOnTask();
            _runtimeActive = true;
        }
        if ((notifications & DISPLAY_NOTIFY_REDRAW) && !_visualDisabled)
            _layoutDrawn = false;

        if (_runtimeActive && !_visualDisabled) {
            copyPendingInput();
            renderFrame();
#ifdef VOXONE_DEBUG
            if (!_stackReported && _layoutDrawn) {
                _stackReported = true;
                Logger::debug("DISPLAY",
                    "DisplayTask stack high-water=" +
                    String(uxTaskGetStackHighWaterMark(nullptr)));
            }
#endif
        }
    }
}

int DisplayService::textWidth(const String& value, bool artistFont, uint8_t scale) {
    const char* cursor = value.c_str();
    int width = 0;
    while (*cursor)
        width += glyphAdvance(PolishGlyphs::next(cursor), artistFont, scale);
    return width;
}

void DisplayService::drawStartupCentered(
    const String& value, int y, uint8_t scale,
    uint16_t color, uint16_t background
) {
    const int width = textWidth(value, false, scale);
    const int x = (_tft.width() - width) / 2;
    drawUtf8Line(value, x, y, _tft.width() - x,
                 color, background, false, scale);
}

void DisplayService::showStartupScreen() {
    if (!_initialized || !_startupActive || _visualDisabled) return;
    _tft.fillScreen(kColorBackground);
    _tft.fillRect(0, 0, _tft.width(), HEADER_H, kColorSurface);
    drawStartupCentered("Vox One", 2, 2,
                        kColorPrimaryText, kColorSurface);
    drawStartupCentered("...", 49, 2, kColorSonyBlue, kColorBackground);
}

void DisplayService::finishStartup() {
    if (_runtimeRequestSent || !_displayTask) return;
    _runtimeRequestSent = true;
    xTaskNotify(_displayTask, DISPLAY_NOTIFY_RUNTIME, eSetBits);
}

void DisplayService::finishStartupOnTask() {
    if (!_scrollCanvas) {
        _scrollCanvas.reset(new (std::nothrow)
            ReusableCanvas16(_tft.width(), SCROLL_CANVAS_H));
        if (_scrollCanvas && !_scrollCanvas->ready()) _scrollCanvas.reset();
    }
    if (!_startupActive) return;
    _startupActive = false;
    _layoutDrawn = false;
}

void DisplayService::drawStaticLayout() {
    PerfScope perf(PerfArea::UiFull);
    _tft.fillScreen(kColorBackground);

    const uint16_t stationFill =
        kColorSurface;

    _tft.fillRect(
        0,
        0,
        _tft.width(),
        HEADER_H,
        stationFill
    );

    _tft.drawFastHLine(
        0,
        HEADER_H,
        _tft.width(),
        stationFill
    );

    _layoutDrawn = true;
}

int DisplayService::glyphAdvance(uint32_t codepoint, bool artistFont, uint8_t scale) {
    if (codepoint >= 32 && codepoint <= 126) {
        if (artistFont) {
            return pgm_read_byte(&FreeSans9pt7bGlyphs[codepoint - 32].xAdvance);
        }
        return 6 * scale;
    }
    return 6 * (artistFont ? 2 : scale); // Local PL glyph or one visible fallback marker.
}

void DisplayService::drawUtf8Line(
    const String& value, int x, int y, int maxWidth,
    uint16_t color, uint16_t background, bool artistFont, uint8_t scale
) {
    _tft.setFont(artistFont ? &FreeSans9pt7b : nullptr);
    _tft.setTextSize(artistFont ? 1 : scale);
    _tft.setTextColor(color, background);

    const char* cursor = value.c_str();
    int fullWidth = 0;
    while (*cursor) {
        const uint32_t codepoint = PolishGlyphs::next(cursor);
        fullWidth += glyphAdvance(codepoint, artistFont, scale);
    }
    const bool clipped = fullWidth > maxWidth;
    const int textLimit = clipped ? maxWidth - glyphAdvance('.', artistFont, scale) * 3 : maxWidth;
    cursor = value.c_str();
    int drawnWidth = 0;
    while (*cursor) {
        const uint32_t codepoint = PolishGlyphs::next(cursor);
        const int advance = glyphAdvance(codepoint, artistFont, scale);
        if (drawnWidth + advance > textLimit) break;

        const PolishGlyphs::Glyph* glyph = PolishGlyphs::find(codepoint);
        if (glyph) {
            const int top = artistFont ? y - 15 : y;
            const uint8_t glyphScale = artistFont ? 2 : scale;
            for (uint8_t row = 0; row < 8; ++row) {
                for (uint8_t col = 0; col < 5; ++col) {
                    if (glyph->rows[row] & (1 << (4 - col))) {
                        _tft.fillRect(x + drawnWidth + col * glyphScale,
                                      top + row * glyphScale,
                                      glyphScale, glyphScale, color);
                    }
                }
            }
            _tft.setCursor(x + drawnWidth + advance, y);
        } else {
            // Unsupported codepoints remain intact in the source String.
            // A single '?' marks each glyph unavailable in these small fonts.
            _tft.setCursor(x + drawnWidth, y);
            _tft.write(codepoint >= 32 && codepoint <= 126
                ? static_cast<uint8_t>(codepoint) : static_cast<uint8_t>('?'));
        }
        drawnWidth += advance;
    }
    if (clipped) {
        _tft.setCursor(x + drawnWidth, y);
        _tft.print("...");
    }
}
void DisplayService::resetScrolls() {
    const uint32_t now = millis();
    _activeScrollRow = -1;
    _nextScrollRow = 0;
    for (auto& line : _scrollLines) {
        line.offset = 0;
        line.holdStartMs = now;
        line.lastStepMs = now;
    }
}

void DisplayService::setScrollLine(uint8_t row, const String& text, bool force) {
    ScrollLine& line = _scrollLines[row];
    if (!force && line.text == text) return;
    line.text = text;
    line.viewportWidth = row == 0 ? _tft.width() - 3 :
        row == 3 ? _tft.width() - 8 : METADATA_VIEWPORT_W;
    const bool artistFont = row == 1 && !_scrollStopStyle;
    line.textWidth = textWidth(text, artistFont, row == 0 || row == 3 ? 2 : 1);
    line.needsScroll = !_scrollStopStyle || row == 0 || row == 3;
    line.needsScroll = line.needsScroll && _scrollCanvas &&
        line.textWidth > line.viewportWidth;
    line.offset = 0;
    line.holdStartMs = millis();
    line.lastStepMs = line.holdStartMs;
    if (_activeScrollRow == static_cast<int8_t>(row)) {
        _activeScrollRow = -1;
        _nextScrollRow = row;
    }
    renderScrollLine(row);
}

void DisplayService::drawScrollText(
    ReusableCanvas16& canvas, const String& text, int x, int baselineY,
    int viewportWidth, bool artistFont, uint8_t scale, uint16_t color
) {
    canvas.setFont(artistFont ? &FreeSans9pt7b : nullptr);
    canvas.setTextSize(artistFont ? 1 : scale);
    canvas.setTextColor(color);
    const char* cursor = text.c_str();
    while (*cursor && x < viewportWidth) {
        const uint32_t codepoint = PolishGlyphs::next(cursor);
        const int advance = glyphAdvance(codepoint, artistFont, scale);
        if (x + advance > 0) {
            const PolishGlyphs::Glyph* glyph = PolishGlyphs::find(codepoint);
            if (glyph) {
                const int top = artistFont ? baselineY - 15 : baselineY;
                const uint8_t glyphScale = artistFont ? 2 : scale;
                for (uint8_t row = 0; row < 8; ++row) {
                    for (uint8_t col = 0; col < 5; ++col) {
                        if (glyph->rows[row] & (1 << (4 - col)))
                            canvas.fillRect(x + col * glyphScale,
                                            top + row * glyphScale,
                                            glyphScale, glyphScale, color);
                    }
                }
            } else {
                canvas.setCursor(x, baselineY);
                canvas.write(codepoint >= 32 && codepoint <= 126
                    ? static_cast<uint8_t>(codepoint) : static_cast<uint8_t>('?'));
            }
        }
        x += advance;
    }
}

void DisplayService::renderScrollLine(uint8_t row) {
    PerfScope perf(PerfArea::UiMetadata);
    const ScrollLine& line = _scrollLines[row];
    const int x = row == 0 ? 3 : row == 3 ? 4 : LEFT_X;
    const int y = row == 0 ? STATION_Y : row == 1 ? HEADER_H + 1 :
        row == 3 ? 29 : TITLE2_Y;
    const int height = row == 0 ? HEADER_H : row == 1 ? 20 :
        row == 3 ? 19 : 9;
    const int baseline = row == 0 ? 2 : row == 1 ? 15 :
        row == 3 ? 2 : 0;
    const bool artistFont = row == 1 && !_scrollStopStyle;
    const uint8_t scale = row == 0 || row == 3 ? 2 : 1;
    const uint16_t background = row == 0 ? kColorSurface : kColorBackground;
    const uint16_t color = row == 0 ? kColorPrimaryText :
        row == 1 ? (_scrollStopStyle ? kColorInactive : kColorSecondaryText) :
                   kColorSonyBlue;
    if (!_scrollCanvas) {
        _tft.fillRect(x, y, line.viewportWidth, height, background);
        const int centeredX = row == 3 && line.textWidth < line.viewportWidth
            ? (line.viewportWidth - line.textWidth) / 2 : 0;
        drawUtf8Line(line.text, x + centeredX, y + baseline,
                     line.viewportWidth - centeredX,
                     color, background, artistFont, scale);
        return;
    }
    ReusableCanvas16& canvas = *_scrollCanvas;
    if (!canvas.configure(line.viewportWidth, height)) return;
    canvas.fillScreen(background);
    canvas.setTextWrap(false);
    if (_scrollStopStyle && row == 1) {
        canvas.setFont(nullptr);
        canvas.setTextSize(1);
        canvas.setTextColor(color);
        canvas.setCursor(0, 1);
        canvas.print("STOP");
    } else {
        const int centeredX = row == 3 && !line.needsScroll &&
            line.textWidth < line.viewportWidth
            ? (line.viewportWidth - line.textWidth) / 2 : 0;
        drawScrollText(canvas, line.text, centeredX - line.offset, baseline,
                       line.viewportWidth, artistFont, scale, color);
        if (line.needsScroll) {
            const int separatorWidth = textWidth(SCROLL_SEPARATOR, artistFont, scale);
            const int separatorX = line.textWidth - line.offset;
            drawScrollText(canvas, SCROLL_SEPARATOR, separatorX, baseline,
                           line.viewportWidth, artistFont, scale, color);
            drawScrollText(canvas, line.text, separatorX + separatorWidth,
                           baseline, line.viewportWidth, artistFont, scale, color);
        }
    }
    _tft.startWrite();
    _tft.setAddrWindow(x, y, line.viewportWidth, height);
    _tft.writePixels(canvas.buffer(),
                     static_cast<uint32_t>(line.viewportWidth) * height);
    _tft.endWrite();
}

void DisplayService::updateScroll(bool radioList) {
    const uint32_t now = millis();
    if (_activeScrollRow < 0) {
        const uint8_t firstRow = radioList ? 3 : 0;
        const uint8_t rowCount = radioList ? 1 : 3;
        for (uint8_t n = 0; n < rowCount; ++n) {
            const uint8_t row = firstRow + (_nextScrollRow + n) % rowCount;
            if (!_scrollLines[row].needsScroll) continue;
            _activeScrollRow = row;
            _scrollLines[row].holdStartMs = now;
            _scrollLines[row].lastStepMs = now;
            return;
        }
        return;
    }
    const uint8_t row = _activeScrollRow;
    ScrollLine& line = _scrollLines[row];
    if (!line.needsScroll) {
        _activeScrollRow = -1;
        _nextScrollRow = radioList ? 0 : (row + 1) % 3;
        return;
    }
    if (now - line.holdStartMs < SCROLL_HOLD_MS ||
        now - line.lastStepMs < SCROLL_STEP_MS) return;
    line.lastStepMs = now;
    const bool artistFont = row == 1 && !_scrollStopStyle;
    const int period = line.textWidth +
        textWidth(SCROLL_SEPARATOR, artistFont, row == 0 || row == 3 ? 2 : 1);
    if (++line.offset >= period) {
        line.offset = 0;
        _activeScrollRow = -1;
        _nextScrollRow = radioList ? 0 : (row + 1) % 3;
    }
    renderScrollLine(row);
}
int DisplayService::wifiLevel(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

void DisplayService::drawHeader(
    bool btConnected,
    bool reconnectGrace,
    const String& peerName
) {
    const uint16_t stationFill =
        kColorSurface;

    _tft.fillRect(
        0,
        0,
        _tft.width(),
        HEADER_H,
        stationFill
    );

    String label;

    if (btConnected || reconnectGrace) {
        label = peerName.isEmpty()
            ? String("BLUETOOTH")
            : peerName;
    } else {
        label = "Vox One";
    }

    drawUtf8Line(label, 3, 2, _tft.width() - 3,
                 kColorPrimaryText, stationFill, false, 2);
}

void DisplayService::drawMetadata(
    bool btConnected,
    bool reconnectGrace,
    const String& artist,
    const String& title
) {
    _tft.fillRect(0, HEADER_H + 1, METADATA_W, 29, kColorBackground);

    _tft.setFont();
    _tft.setTextSize(1);

    if (!btConnected && !reconnectGrace) {
        _tft.setTextColor(kColorInactive, kColorBackground);
        _tft.setCursor(LEFT_X, HEADER_H + 2);
        _tft.print("STOP");
        return;
    }

    drawUtf8Line(artist.isEmpty() ? String("-") : artist,
                 LEFT_X, ARTIST_BASELINE_Y, METADATA_W - LEFT_X,
                 kColorSecondaryText, kColorBackground, true, 1);
    drawUtf8Line(title.isEmpty() ? String("-") : title,
                 LEFT_X, TITLE2_Y, METADATA_W - LEFT_X,
                 kColorSonyBlue, kColorBackground, false, 1);
}

void DisplayService::drawSourceInfo(const DeviceState& state) {
    _tft.fillRect(
        0,
        INFO1_Y,
        STATUS_W,
        10,
        kColorBackground
    );

    _tft.setFont();
    _tft.setTextSize(1);
    _tft.setCursor(LEFT_X, INFO1_Y);

    if (!state.playMediaActive &&
        state.audioSource == AudioSource::Radio &&
        state.playback == PlaybackState::Playing) {
        String format;
        if (state.radioBitrate > 0) format = String(state.radioBitrate);
        if (!state.radioCodec.isEmpty()) {
            if (!format.isEmpty()) format += ' ';
            format += state.radioCodec;
        }
        if (!format.isEmpty()) {
            _tft.setTextColor(kColorSonyBlue, kColorBackground);
            _tft.print(format);
        }
    }
}

void DisplayService::drawWifiIndicator(
    bool wifiConnected,
    int wifiRssi,
    bool apMode
) {
    // Center the complete visible indicator under the clock text, not merely
    // the background rectangle. Keep WIFI_Y and the bar heights unchanged.
    _tft.setFont();
    int16_t x1, y1;
    uint16_t clockWidth, textHeight;
    _tft.setTextSize(2);
    _tft.getTextBounds("--:--", 0, 0, &x1, &y1, &clockWidth, &textHeight);
    const int clockCenterX = CLOCK_X + static_cast<int>(clockWidth) / 2;

    _tft.setTextSize(1);
    const char* label = wifiConnected ? "WIFI" : (apMode ? "AP" : "WIFI --");
    uint16_t labelWidth;
    _tft.getTextBounds(label, 0, 0, &x1, &y1, &labelWidth, &textHeight);
    constexpr int barCount = 4;
    constexpr int barStride = 6;
    constexpr int barWidth = 4;
    constexpr int labelGap = 2;
    const int barsWidth = (barCount - 1) * barStride + barWidth;
    const int visibleWidth = static_cast<int>(labelWidth) +
        (wifiConnected ? labelGap + barsWidth : 0);
    const int labelX = clockCenterX - visibleWidth / 2;

    _tft.fillRect(CLOCK_X, WIFI_Y, CLOCK_W, 13, kColorBackground);
    _tft.setCursor(labelX, WIFI_Y);

    if (!wifiConnected) {
        _tft.setTextColor(
            apMode ? kColorSonyBlue : kColorInactive,
            kColorBackground
        );
        _tft.print(label);
        return;
    }

    _tft.setTextColor(kColorSecondaryText, kColorBackground);
    _tft.print(label);

    const int level = wifiLevel(wifiRssi);
    const int baseX = labelX + static_cast<int>(labelWidth) + labelGap;
    const int baseY = WIFI_Y + 10;

    for (int i = 0; i < barCount; ++i) {
        const int h = 2 + i * 2;
        const uint16_t color =
            i < level ? kColorSonyBlue : kColorInactive;

        _tft.fillRect(
            baseX + i * barStride,
            baseY - h,
            barWidth,
            h,
            color
        );
    }
}

void DisplayService::drawClock(
    bool valid,
    const String& clockText
) {
    PerfScope perf(PerfArea::UiClock);
    _tft.fillRect(
        CLOCK_X,
        CLOCK_Y,
        CLOCK_W,
        CLOCK_H,
        kColorBackground
    );

    _tft.setTextSize(2);
    _tft.setCursor(CLOCK_X, CLOCK_Y);

    if (valid && !clockText.isEmpty()) {
        _tft.setTextColor(kColorPrimaryText, kColorBackground);
        _tft.print(clockText);
    } else {
        _tft.setTextColor(kColorNeutral, kColorBackground);
        _tft.print("--:--");
    }
}

void DisplayService::drawVolumeIndicator(int volume) {
    _tft.fillRect(116, 55, 52, 19, kColorBackground);
    _tft.setFont();
    _tft.setTextSize(1);

    char value[4];
    snprintf(value, sizeof(value), "%d", constrain(volume, 0, 100));
    int16_t x1, y1;
    uint16_t w, h;
    _tft.getTextBounds(value, 0, VOLUME_Y, &x1, &y1, &w, &h);

    const int iconWidth = 5 * SPEAKER_SCALE;
    const int startX = (DSP_WIDTH - (iconWidth + 6 + static_cast<int>(w))) / 2;
    for (int column = 0; column < 5; ++column) {
        for (int row = 0; row < 7; ++row) {
            if (SPEAKER_GLYPH[column] & (1 << row)) {
                _tft.fillRect(
                    startX + column * SPEAKER_SCALE,
                    57 + row * SPEAKER_SCALE,
                    SPEAKER_SCALE,
                    SPEAKER_SCALE,
                    kColorPrimaryText
                );
            }
        }
    }

    _tft.setTextColor(kColorPrimaryText, kColorBackground);
    _tft.setCursor(startX + iconWidth + 6, VOLUME_Y + 2);
    _tft.print(value);
}

void DisplayService::drawVolumeValue(int volume) {
    PerfScope perf(PerfArea::UiVolume);
    _tft.fillRect(
        0,
        22,
        _tft.width(),
        36,
        kColorBackground
    );

    char vol[8];
    snprintf(
        vol,
        sizeof(vol),
        "%d",
        constrain(volume, 0, 100)
    );

    _tft.setTextColor(kColorSonyBlue, kColorBackground);
    _tft.setTextSize(3);

    int16_t x1, y1;
    uint16_t w, h;

    _tft.getTextBounds(
        vol,
        0,
        0,
        &x1,
        &y1,
        &w,
        &h
    );

    _tft.setCursor(
        (_tft.width() - static_cast<int>(w)) / 2,
        27
    );

    _tft.print(vol);
}

void DisplayService::drawVolumeIp(const String& ip) {
    _tft.fillRect(
        0,
        60,
        _tft.width(),
        12,
        kColorBackground
    );

    _tft.setTextSize(1);
    _tft.setTextColor(kColorSecondaryText, kColorBackground);
    _tft.setCursor(3, 62);

    if (ip.isEmpty()) {
        _tft.print("IP: -");
    } else {
        _tft.print("IP: ");
        _tft.print(ip);
    }
}

void DisplayService::showVolumeScreen(
    int volume,
    const String& ip
) {
    PerfScope perf(PerfArea::UiFull);

    _tft.fillScreen(kColorBackground);

    const uint16_t stationFill =
        kColorSurface;

    _tft.fillRect(
        0,
        0,
        _tft.width(),
        HEADER_H,
        stationFill
    );

    const char* label = "GLOSNOSC";

    _tft.setTextColor(kColorPrimaryText, stationFill);
    _tft.setTextSize(2);

    int16_t x1, y1;
    uint16_t w, h;

    _tft.getTextBounds(
        label,
        0,
        0,
        &x1,
        &y1,
        &w,
        &h
    );

    _tft.setCursor(
        (_tft.width() - static_cast<int>(w)) / 2,
        2
    );

    _tft.print(label);

    drawVolumeValue(volume);
    drawVolumeIp(ip);
}

void DisplayService::drawBtTrackNavScreen() {
    _tft.fillScreen(kColorBackground);
    _tft.fillRect(0, 0, _tft.width(), HEADER_H, kColorSurface);

    const String label(u8"BT - PRZEŁĄCZ UTWÓR");
    int labelWidth = 0;
    for (const char* cursor = label.c_str(); *cursor;)
        labelWidth += glyphAdvance(PolishGlyphs::next(cursor), false, 2);
    drawUtf8Line(label, (_tft.width() - labelWidth) / 2, 2, _tft.width(),
                 kColorPrimaryText, kColorSurface, false, 2);

    // Only the three small yoRadio symbols are retained, scaled for ST7789.
    constexpr int iconScale = 2;
    constexpr int iconWidth = 5 * iconScale;
    constexpr int iconGap = 40;
    constexpr int iconY = 37; // Keeps the symbols centered near their original y=44.
    const int leftX = (_tft.width() - (3 * iconWidth + 2 * iconGap)) / 2;
    auto drawIcon = [this, iconScale, iconY](const uint8_t* columns, int x,
                                             uint16_t color) {
        for (int column = 0; column < 5; ++column) {
            for (int row = 0; row < 7; ++row) {
                if (columns[column] & (1 << row))
                    _tft.fillRect(x + column * iconScale, iconY + row * iconScale,
                                  iconScale, iconScale, color);
            }
        }
    };
    drawIcon(NAV_PREV, leftX, kColorSonyBlue);
    drawIcon(NAV_NOTE, leftX + iconWidth + iconGap, kColorSecondaryText);
    drawIcon(NAV_NEXT, leftX + 2 * (iconWidth + iconGap), kColorSonyBlue);
}

void DisplayService::drawBtNavArtist(const String& artist) {
    const int y = _tft.height() - 11;
    _tft.fillRect(0, y - 2, _tft.width(), 13, kColorBackground);
    if (artist.isEmpty()) return;

    const int maxWidth = _tft.width() - 8;
    int fullWidth = 0;
    for (const char* cursor = artist.c_str(); *cursor;)
        fullWidth += glyphAdvance(PolishGlyphs::next(cursor), false, 1);
    const int visibleWidth = fullWidth < maxWidth ? fullWidth : maxWidth;
    drawUtf8Line(artist, (_tft.width() - visibleWidth) / 2, y, maxWidth,
                 kColorSecondaryText, kColorBackground, false, 1);
}

void DisplayService::drawRadioListFooter() {
    constexpr int footerY = 60;
    _tft.fillRect(0, footerY - 2, _tft.width(), _tft.height() - footerY + 2,
                  kColorBackground);
    const String counter =
        String(_radioListPosition) + " / " + String(_radioListCount);
    drawUtf8Line(counter, (_tft.width() - textWidth(counter, false, 1)) / 2,
                 footerY, _tft.width(), kColorSecondaryText,
                 kColorBackground, false, 1);
    if (_radioListPosition && _radioListPlaying)
        drawUtf8Line("PLAY", _tft.width() - 4 - textWidth("PLAY", false, 1),
                     footerY, 40, kColorSonyBlue, kColorBackground, false, 1);
}

void DisplayService::drawRadioListScreen() {
    _tft.fillScreen(kColorBackground);
    _tft.fillRect(0, 0, _tft.width(), HEADER_H, kColorSurface);
    drawStartupCentered("RADIO - STACJE", 2, 2,
                        kColorPrimaryText, kColorSurface);
    resetScrolls();
    setScrollLine(3, _radioListStationName.isEmpty()
        ? String("BRAK STACJI") : _radioListStationName, true);
    drawRadioListFooter();
}

void DisplayService::updatePlayerFields(bool force) {
    const DeviceState s =
        StateStore::instance().snapshot();

    const bool sourceChanged = s.audioSource != _lastScrollSource ||
        s.playMediaActive != _lastPlayMediaActive;
    if (sourceChanged) {
        resetScrolls();
        _lastScrollSource = s.audioSource;
        _lastPlayMediaActive = s.playMediaActive;
    }
    const bool radio = s.audioSource == AudioSource::Radio;
    const bool radioPlaying = radio && s.playback == PlaybackState::Playing;
    const bool bt = s.audioSource == AudioSource::Bluetooth;
    const bool btConnected = bt && s.bluetoothConnected;
    const String station = s.playMediaActive
        ? String("Player Media")
        : radio
        ? (radioPlaying ? (s.radioStation.isEmpty() ? String("Radio WEB") : s.radioStation)
                       : String("Radio WEB"))
        : bt
            ? String("Bluetooth")
            : String("Vox One");
    const String artist = s.playMediaActive ? String() :
        radio ? (radioPlaying ? s.radioArtist : String()) :
        bt ? (btConnected ? s.bluetoothArtist : String("Czekam na połączenie")) :
        String("STOP");
    const String title = s.playMediaActive ? String() :
        radio ? (radioPlaying ? s.radioTitle : String()) :
        bt ? (btConnected ? s.bluetoothTitle : String()) : String();
    const bool stopStyle = !s.playMediaActive &&
        s.audioSource == AudioSource::Stop;
    const bool styleChanged = stopStyle != _scrollStopStyle;
    _scrollStopStyle = stopStyle;
    setScrollLine(0, station, force || sourceChanged);
    setScrollLine(1, artist,
        force || sourceChanged || styleChanged);
    setScrollLine(2, title,
        force || sourceChanged || styleChanged);
    if (force ||
        s.playback != _lastPlayback ||
        s.radioBitrate != _lastRadioBitrate ||
        s.radioCodec != _lastRadioCodec ||
        s.bluetoothConnected != _lastBtConnected ||
        s.bluetoothPlaying != _lastBtPlaying ||
        s.bluetoothReconnectGrace != _lastBtReconnectGrace) {

        drawSourceInfo(s);

        _lastBtPlaying = s.bluetoothPlaying;
        _lastPlayback = s.playback;
        _lastRadioBitrate = s.radioBitrate;
        _lastRadioCodec = s.radioCodec;
    }

    const int level =
        s.wifiConnected ? wifiLevel(s.wifiRssi) : -1;

    const bool wifiStateChanged =
        s.wifiConnected != _lastWifiConnected ||
        s.apMode != _lastApMode;

    const bool wifiLevelDue =
        level != _lastWifiLevel &&
        (millis() - _lastWifiDrawMs >= WIFI_DRAW_INTERVAL_MS);

    if (force ||
        wifiStateChanged ||
        wifiLevelDue) {

        drawWifiIndicator(
            s.wifiConnected,
            s.wifiRssi,
            s.apMode
        );

        _lastWifiConnected = s.wifiConnected;
        _lastApMode = s.apMode;
        _lastWifiLevel = level;
        _lastWifiDrawMs = millis();
    }

    if (force ||
        s.timeValid != _lastTimeValid ||
        s.clockText != _lastClockText) {

        drawClock(
            s.timeValid,
            s.clockText
        );

        _lastTimeValid = s.timeValid;
        _lastClockText = s.clockText;
    }

    if (force || s.volume != _lastVolume) {
        drawVolumeIndicator(s.volume);
        _lastVolume = s.volume;
    }

    _lastBtConnected = s.bluetoothConnected;
    _lastBtReconnectGrace = s.bluetoothReconnectGrace;
    updateScroll(false);
}

void DisplayService::requestUpdate(UiMode mode, bool btNavArtistPending,
                                   const StationStore& stations,
                                   uint16_t highlightedStationId,
                                   uint16_t playingStationId) {
    if (!_displayTask || !_inputMutex) return;

    DisplayInput next;
    next.mode = mode;
    next.btNavArtistPending = btNavArtistPending;
    if (mode == UiMode::RadioList) {
        next.radioListCount = stations.count();
        next.radioListPosition = stations.positionOfId(highlightedStationId);
        const Station* station = stations.getById(highlightedStationId);
        if (station) next.radioListStationName = station->name;
        next.radioListPlaying =
            next.radioListPosition > 0 && highlightedStationId == playingStationId;
    }

    bool changed = false;
    xSemaphoreTake(_inputMutex, portMAX_DELAY);
    if (next.mode != _pendingInput.mode ||
        next.btNavArtistPending != _pendingInput.btNavArtistPending ||
        next.radioListStationName != _pendingInput.radioListStationName ||
        next.radioListCount != _pendingInput.radioListCount ||
        next.radioListPosition != _pendingInput.radioListPosition ||
        next.radioListPlaying != _pendingInput.radioListPlaying) {
        _pendingInput = next;
        ++_pendingGeneration;
        changed = true;
    }
    xSemaphoreGive(_inputMutex);

    if (changed)
        xTaskNotify(_displayTask, DISPLAY_NOTIFY_INPUT, eSetBits);
}

bool DisplayService::copyPendingInput() {
    if (!_inputMutex) return false;
    bool changed = false;
    xSemaphoreTake(_inputMutex, portMAX_DELAY);
    if (_appliedGeneration != _pendingGeneration) {
        _activeInput = _pendingInput;
        _appliedGeneration = _pendingGeneration;
        changed = true;
    }
    xSemaphoreGive(_inputMutex);
    return changed;
}

void DisplayService::renderFrame() {
    PerfScope perf(PerfArea::Ui);
    if (_visualDisabled || _startupActive) return;
    const bool stationChanged =
        _activeInput.radioListStationName != _radioListStationName ||
        _activeInput.radioListCount != _radioListCount ||
        _activeInput.radioListPosition != _radioListPosition;
    const bool playingChanged =
        _activeInput.radioListPlaying != _radioListPlaying;
    _radioListStationName = _activeInput.radioListStationName;
    _radioListCount = _activeInput.radioListCount;
    _radioListPosition = _activeInput.radioListPosition;
    _radioListPlaying = _activeInput.radioListPlaying;
    const DeviceState s = StateStore::instance().snapshot();

    if (!_layoutDrawn || _activeInput.mode != _renderedMode) {
        _renderedMode = _activeInput.mode;
        if (_activeInput.mode == UiMode::Volume) {
            showVolumeScreen(s.volume, s.ip);
            _lastVolume = s.volume;
        } else if (_activeInput.mode == UiMode::BtTrackNav) {
            drawBtTrackNavScreen();
            drawBtNavArtist(_activeInput.btNavArtistPending
                ? String() : s.bluetoothArtist);
            _lastBtNavArtist = s.bluetoothArtist;
            _lastBtNavArtistPending = _activeInput.btNavArtistPending;
        } else if (_activeInput.mode == UiMode::RadioList) {
            drawRadioListScreen();
        } else {
            resetScrolls();
            drawStaticLayout();
            updatePlayerFields(true);
        }
        _layoutDrawn = true;
        return;
    }

    if (_activeInput.mode == UiMode::Volume) {
        if (s.volume != _lastVolume) {
            _lastVolume = s.volume;
            // Keep the IP field untouched while the large number changes.
            drawVolumeValue(s.volume);
        }
        return;
    }
    if (_activeInput.mode == UiMode::BtTrackNav) {
        if (_activeInput.btNavArtistPending != _lastBtNavArtistPending ||
            s.bluetoothArtist != _lastBtNavArtist) {
            drawBtNavArtist(_activeInput.btNavArtistPending
                ? String() : s.bluetoothArtist);
            _lastBtNavArtist = s.bluetoothArtist;
            _lastBtNavArtistPending = _activeInput.btNavArtistPending;
        }
        return;
    }
    if (_activeInput.mode == UiMode::RadioList) {
        if (stationChanged) {
            setScrollLine(3, _radioListStationName.isEmpty()
                ? String("BRAK STACJI") : _radioListStationName, true);
            drawRadioListFooter();
        } else if (playingChanged) {
            drawRadioListFooter();
        }
        updateScroll(true);
        return;
    }
    updatePlayerFields(false);
}

bool DisplayService::clearToBlack() {
    if (!_initialized || !_displayTask || !_clearDone) return false;
    xSemaphoreTake(_clearDone, 0);
    _clearResult = false;
    xTaskNotify(_displayTask, DISPLAY_NOTIFY_CLEAR, eSetBits);
    if (xSemaphoreTake(_clearDone, pdMS_TO_TICKS(1000)) != pdTRUE)
        return false;
    return _clearResult;
}

void DisplayService::redraw() {
    if (!_displayTask) return;
    xTaskNotify(_displayTask, DISPLAY_NOTIFY_REDRAW, eSetBits);
}

