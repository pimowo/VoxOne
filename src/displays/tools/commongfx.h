#ifndef common_gfx_h
#define common_gfx_h
#include "../widgets/widgetsconfig.h" // displayXXXDDDDconf.h
#include "utf8Rus.h"

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
      drawBitmap((width()  - LOGO_WIDTH ) / 2, top, logo, LOGO_WIDTH, LOGO_HEIGHT, 1);
      display();
    }
    #else
      inline void loop(bool force=false){}
      inline void drawLogo(uint16_t top){ drawRGBBitmap((width() - LOGO_WIDTH) / 2, top, logo, LOGO_WIDTH, LOGO_HEIGHT); }
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
    bool _clipping;
    clipArea _cliparea;
    void * _scrollid;
    #ifdef PSFBUFFER
    psFrameBuffer* _fb=nullptr;
    #endif
};

extern DspCore dsp;
#endif
