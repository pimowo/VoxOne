#ifndef common_gfx_h
#define common_gfx_h
#include "../widgets/widgetsconfig.h" // displayXXXDDDDconf.h
#include "utf8Rus.h"

#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_OLED_128X64
  #include "../fonts/voxone_boot_logo_oled_128x64.h"
#elif VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  #include "boot_branding_codec.h"
  #include "../fonts/voxone_boot_logo_st7789_284x76.h"
#elif VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
  #include "boot_branding_codec.h"
  #include "../fonts/voxone_boot_logo_st7796_480x320.h"
#endif

typedef struct clipArea {
  uint16_t left; 
  uint16_t top; 
  uint16_t width;  
  uint16_t height;
} clipArea;

class psFrameBuffer;

class DspCore: public yoDisplay {
  public:
    DspCore();
    void initDisplay();
    void clearDsp(bool black=false);
    void printClock(){}
    #ifdef DSP_OLED
    inline void loop(bool force=false){
      display();
      //delay(DSP_MODEL==DSP_ST7920?20:5);
      vTaskDelay(DSP_MODEL==DSP_ST7920?10:0);
    }
    inline void drawLogo(uint16_t top) {
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_OLED_128X64
      (void)top;
      using namespace voxone::display_boot_branding::oled;
      drawBitmap(kBootLogoLeft, kBootLogoTop, kBootLogoBitmap,
                 kBootLogoWidth, kBootLogoHeight, 1);
#else
      drawBitmap((width()  - LOGO_WIDTH ) / 2, top, logo, LOGO_WIDTH, LOGO_HEIGHT, 1);
      display();
#endif
    }
    #else
      inline void loop(bool force=false){}
      inline void drawLogo(uint16_t top){
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
        (void)top;
        using namespace voxone::display_boot_branding::st7789;
        _drawBootLogoRle(kBootLogoRle565, kBootLogoRle565Size,
                         kBootLogoLeft, kBootLogoTop,
                         kBootLogoWidth, kBootLogoPixelCount);
#elif VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
        (void)top;
        using namespace voxone::display_boot_branding::st7796;
        _drawBootLogoRle(kBootLogoRle565, kBootLogoRle565Size,
                         kBootLogoLeft, kBootLogoTop,
                         kBootLogoWidth, kBootLogoPixelCount);
#else
        drawRGBBitmap((width() - LOGO_WIDTH) / 2, top, logo,
                      LOGO_WIDTH, LOGO_HEIGHT);
#endif
      }
    #endif
    void flip();
    void invert();
    void sleep();
    void wake();
    void setScrollId(void * scrollid) { _scrollid = scrollid; }
    void * getScrollId() { return _scrollid; }
    uint16_t textWidth(const char *txt);
      inline void writePixel(int16_t x, int16_t y, uint16_t color) {
        if(_clipping){
          if ((x < _cliparea.left) || (x > _cliparea.left+_cliparea.width) || (y < _cliparea.top) || (y > _cliparea.top + _cliparea.height)) return;
        }
        yoDisplay::writePixel(x, y, color);
      }
      inline void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        if(_clipping){
          if ((x < _cliparea.left) || (x >= _cliparea.left+_cliparea.width) || (y < _cliparea.top) || (y > _cliparea.top + _cliparea.height))  return;
        }
        yoDisplay::writeFillRect(x, y, w, h, color);
      }
    inline void setClipping(clipArea ca){
      _cliparea = ca;
      _clipping = true;
    }
    inline void clearClipping(){
      _clipping = false;
    }
  private:
#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76 || \
    VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7796_480X320
    inline void _drawBootLogoRle(const uint8_t* encoded, size_t encodedSize,
                                 int16_t left, int16_t top, uint16_t logoWidth,
                                 uint32_t expectedPixels) {
      startWrite();
      const auto result = voxone::display_boot_branding::decodeRle565(
          encodedSize, expectedPixels,
          [encoded](size_t offset) { return pgm_read_byte(encoded + offset); },
          [this, left, top, logoWidth](uint32_t pixel, uint32_t run,
                                      uint16_t color) {
            while (run) {
              const uint16_t x = pixel % logoWidth;
              const uint16_t remainingInRow = logoWidth - x;
              const uint16_t count =
                  run < remainingInRow ? static_cast<uint16_t>(run)
                                       : remainingInRow;
              writeFastHLine(left + x, top + pixel / logoWidth, count, color);
              pixel += count;
              run -= count;
            }
          });
      endWrite();
      (void)result;
    }
#endif
    bool _clipping;
    clipArea _cliparea;
    void * _scrollid;
    #ifdef PSFBUFFER
    psFrameBuffer* _fb=nullptr;
    #endif
};

extern DspCore dsp;
#endif
