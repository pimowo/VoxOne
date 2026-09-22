#pragma once
#include <stdint.h>

// Local 5x8 glyphs; bit 4 is leftmost. GFX's bundled fonts stop at ASCII.
namespace PolishGlyphs {
struct Glyph { uint16_t codepoint; uint8_t rows[8]; };
static constexpr Glyph kGlyphs[] = {
    {0x0104, {14,17,17,31,17,17,17, 6}}, // Ą
    {0x0105, { 0, 0,14, 1,15,17,15, 6}}, // ą
    {0x0106, { 2,14,16,16,16,16,14, 0}}, // Ć
    {0x0107, { 2, 0,14,16,16,16,14, 0}}, // ć
    {0x0118, {31,16,16,30,16,16,31, 6}}, // Ę
    {0x0119, { 0, 0,14,17,31,16,14, 6}}, // ę
    {0x0141, {16,16,20,24,16,16,31, 0}}, // Ł
    {0x0142, {12, 4,12, 4, 4, 4, 6, 0}}, // ł
    {0x0143, { 2,17,25,21,19,17,17, 0}}, // Ń
    {0x0144, { 2, 0,30,17,17,17,17, 0}}, // ń
    {0x00D3, { 2,14,17,17,17,17,14, 0}}, // Ó
    {0x00F3, { 2, 0,14,17,17,17,14, 0}}, // ó
    {0x015A, { 2,15,16,14, 1, 1,30, 0}}, // Ś
    {0x015B, { 2, 0,15,16,14, 1,30, 0}}, // ś
    {0x0179, { 2,31, 1, 2, 4, 8,31, 0}}, // Ź
    {0x017A, { 2, 0,31, 2, 4, 8,31, 0}}, // ź
    {0x017B, { 4,31, 1, 2, 4, 8,31, 0}}, // Ż
    {0x017C, { 4, 0,31, 2, 4, 8,31, 0}}  // ż
};

inline const Glyph* find(uint32_t codepoint) {
    for (const auto& glyph : kGlyphs)
        if (glyph.codepoint == codepoint) return &glyph;
    return nullptr;
}

// Invalid input consumes one byte; valid non-Polish codepoints stay intact.
inline uint32_t next(const char*& cursor) {
    const uint8_t first = static_cast<uint8_t>(*cursor++);
    if (first < 0x80) return first;
    uint32_t value;
    uint8_t count;
    if (first >= 0xC2 && first <= 0xDF) {
        value = first & 0x1F; count = 1;
    } else if (first >= 0xE0 && first <= 0xEF) {
        value = first & 0x0F; count = 2;
    } else if (first >= 0xF0 && first <= 0xF4) {
        value = first & 0x07; count = 3;
    } else {
        return 0xFFFD;
    }
    const char* const start = cursor;
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t byte = static_cast<uint8_t>(cursor[i]);
        if (!byte || (byte & 0xC0) != 0x80) return 0xFFFD;
        value = (value << 6) | (byte & 0x3F);
    }
    if ((count == 1 && value < 0x80) ||
        (count == 2 && value < 0x800) ||
        (count == 3 && value < 0x10000) ||
        (value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF) {
        cursor = start;
        return 0xFFFD;
    }
    cursor += count;
    return value;
}
}
