#include "Arduino.h"
#include "../../core/options.h"
#include "../dspcore.h"
#include "utf8Rus.h"

struct Utf8LatinGlyph {
  uint16_t lowerUtf8;
  uint16_t upperUtf8;
  uint8_t lowerGlyph;
  uint8_t upperGlyph;
  char lowerFallback;
  char upperFallback;
};

// UTF-8 lead/trail pairs mapped to project-local glcdfont slots. Codes 0xC0-0xFF
// remain reserved for the existing Cyrillic mapping; 0xA8/0xB8 remain Ё/ё.
static const Utf8LatinGlyph latinGlyphs[] = {
  {0xC485, 0xC484, 0x80, 0x81, 'a', 'A'}, // ą Ą
  {0xC487, 0xC486, 0x82, 0x83, 'c', 'C'}, // ć Ć
  {0xC499, 0xC498, 0x84, 0x85, 'e', 'E'}, // ę Ę
  {0xC582, 0xC581, 0x86, 0x87, 'l', 'L'}, // ł Ł
  {0xC584, 0xC583, 0x88, 0x89, 'n', 'N'}, // ń Ń
  {0xC3B3, 0xC393, 0x8A, 0x8B, 'o', 'O'}, // ó Ó
  {0xC59B, 0xC59A, 0x8C, 0x8D, 's', 'S'}, // ś Ś
  {0xC5BA, 0xC5B9, 0x8E, 0x8F, 'z', 'Z'}, // ź Ź
  {0xC5BC, 0xC5BB, 0x90, 0x91, 'z', 'Z'}, // ż Ż
  {0xC3A4, 0xC384, 0x92, 0x93, 'a', 'A'}, // ä Ä
  {0xC3B6, 0xC396, 0x94, 0x95, 'o', 'O'}, // ö Ö
  {0xC3BC, 0xC39C, 0x96, 0x97, 'u', 'U'}, // ü Ü
  {0xC39F, 0x0000, 0x98, 0x98, 's', 'S'}, // ß
  {0xC3A9, 0xC389, 0x99, 0x9A, 'e', 'E'}, // é É
  {0xC588, 0xC587, 0x9B, 0x9C, 'n', 'N'}, // ň Ň
  {0xC595, 0xC594, 0x9D, 0x9E, 'r', 'R'}, // ř Ř
  {0xC5A1, 0xC5A0, 0x9F, 0xA0, 's', 'S'}, // š Š
  {0xC5A5, 0xC5A4, 0xA1, 0xA2, 't', 'T'}, // ť Ť
  {0xC5BE, 0xC5BD, 0xA3, 0xA4, 'z', 'Z'}, // ž Ž
  {0xC5AF, 0xC5AE, 0xA5, 0xA6, 'u', 'U'}, // ů Ů
  {0xC48D, 0xC48C, 0xA7, 0xA9, 'c', 'C'}, // č Č
  {0xC48F, 0xC48E, 0xAA, 0xAB, 'd', 'D'}, // ď Ď
  {0xC4BA, 0xC4B9, 0xAC, 0xAD, 'l', 'L'}, // ĺ Ĺ
  {0xC4BE, 0xC4BD, 0xAE, 0xAF, 'l', 'L'}, // ľ Ľ
  {0xC3A1, 0xC381, 0xB0, 0xB1, 'a', 'A'}, // á Á
  {0xC3AD, 0xC38D, 0xB2, 0xB3, 'i', 'I'}, // í Í
  {0xC3B4, 0xC394, 0xB4, 0xB5, 'o', 'O'}, // ô Ô
  {0xC3BA, 0xC39A, 0xB6, 0xB7, 'u', 'U'}, // ú Ú
  {0xC3BD, 0xC39D, 0xB9, 0xBA, 'y', 'Y'}, // ý Ý
  {0xC3A5, 0xC385, 0xBB, 0xBC, 'a', 'A'}  // å Å
};

static bool mapLatinGlyph(uint8_t lead, uint8_t trail, bool uppercase, char &mapped) {
  const uint16_t utf8 = (static_cast<uint16_t>(lead) << 8) | trail;
  for (const Utf8LatinGlyph &entry : latinGlyphs) {
    const bool isLower = utf8 == entry.lowerUtf8;
    const bool isUpper = entry.upperUtf8 != 0 && utf8 == entry.upperUtf8;
    if (!isLower && !isUpper) continue;
#if defined(DSP_LCD)
    mapped = (uppercase || isUpper) ? entry.upperFallback : entry.lowerFallback;
#else
    mapped = static_cast<char>((uppercase || isUpper) ? entry.upperGlyph : entry.lowerGlyph);
#endif
    return true;
  }
  return false;
}

size_t strlen_utf8(const char* s) {
  size_t count = 0;
  while (*s) {
    count++;
    if ((*s & 0xF0) == 0xF0) { // 4-byte character
      s += 4;
    } else if ((*s & 0xE0) == 0xE0) { // 3-byte character
      s += 3;
    } else if ((*s & 0xC0) == 0xC0) { // 2-byte character
      s += 2;
    } else { // 1-byte character (ASCII)
      s += 1;
    }
  }
  return count;
}

char* utf8Rus(const char* str, bool uppercase) {
  static char out[BUFLEN];
  int outPos = 0;
#if defined(DSP_LCD) && !defined(LCD_RUS)
  static const char* mapD0[] = {
    "A","B","V","G","D","E","ZH","Z","I","Y",
    "K","L","M","N","O","P","R","S","T","U",
    "F","H","TS","CH","SH","SHCH","'","YU","'","E","YU","YA"
  };
#endif
#if defined(DSP_LCD) && defined(LCD_RUS)
  // except 0401 --> 0xa2 = Ё, 0451 --> 0xb5 = ё
  static const unsigned char utf_recode[] PROGMEM =
  {
    0x41,0xa0,0x42,0xa1,0xe0,0x45,0xa3,0xa4,0xa5,0xa6,0x4b,0xa7,0x4d,0x48,0x4f,
    0xa8,0x50,0x43,0x54,0xa9,0xaa,0x58,0xe1,0xab,0xac,0xe2,0xad,0xae,0x62,0xaf,0xb0,0xb1,
    0x61,0xb2,0xb3,0xb4,0xe3,0x65,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0x6f,
    0xbe,0x70,0x63,0xbf,0x79,0xe4,0x78,0xe5,0xc0,0xc1,0xe6,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7
  };
#endif
  for (int i = 0; str[i] && outPos < BUFLEN - 1; i++) {
    uint8_t c = (uint8_t)str[i];
    char latinGlyph;
    if ((c == 0xC3 || c == 0xC4 || c == 0xC5) && str[i+1] &&
        mapLatinGlyph(c, static_cast<uint8_t>(str[i+1]), uppercase, latinGlyph)) {
      out[outPos++] = latinGlyph;
      i++;
    } else if (c == 0xD0 && str[i+1]) {
      uint8_t n = (uint8_t)str[++i];
      if (n == 0x81) {                  // Ё
      #if defined(DSP_LCD) && !defined(LCD_RUS)
        const char* t = "YO";
        for (; *t && outPos < BUFLEN-1; t++) out[outPos++] = *t;
      #else
        out[outPos++] = uppercase ? 0xA8 : 0xB8;
      #endif
      } else if (n >= 144 && n <= 191) {
      #if defined(DSP_LCD) && !defined(LCD_RUS)
        if(n>=176) n-=32;
        const char* t = mapD0[n - 0x90];
        for (; *t && outPos < BUFLEN-1; t++) out[outPos++] = *t;
      #else
        #if defined(DSP_LCD) && defined(LCD_RUS)
          if(n>=176) n-=32;
          out[outPos++] = utf_recode[n - 0x90];
        #else
          uint8_t ch = n + 48;
          if(n>=176 && uppercase) ch-=32;
          out[outPos++] = ch;
        #endif
      #endif
      }
    } else if (c == 0xD1 && str[i+1]) {
      uint8_t n = (uint8_t)str[++i];
      if (n == 0x91) {                  // ё
      #if defined(DSP_LCD) && !defined(LCD_RUS)
        const char* t = "YO";
        for (; *t && outPos < BUFLEN-1; t++) out[outPos++] = *t;
      #else
        out[outPos++] = uppercase ? 0xA8 : 0xB8;
      #endif
      } else if (n >= 128 && n <= 143) {
      #if defined(DSP_LCD) && !defined(LCD_RUS)
        n+=16;
        const char* t = mapD0[n - 128];
        for (; *t && outPos < BUFLEN-1; t++) out[outPos++] = *t;
      #else
        #if defined(DSP_LCD) && defined(LCD_RUS)
          n+=16;
          out[outPos++] = utf_recode[n - 128];
        #else
          uint8_t ch = n + 112;
          if(uppercase) ch-=32;
          out[outPos++] = ch;
        #endif
      #endif
      }
    } else {                              // ASCII
    #if defined(DSP_LCD) && !defined(LCD_RUS)
      char ch = (char)toupper(c);
      if (ch == 7) ch = (char)165;
      if (ch == 9) ch = (char)223;
      out[outPos++] = ch;
    #else
      out[outPos++] = uppercase ? toupper(c) : c;
    #endif
    }
  }
  out[outPos] = 0;
  return out;
}

