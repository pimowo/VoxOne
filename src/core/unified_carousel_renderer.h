#ifndef VOXONE_UNIFIED_CAROUSEL_RENDERER_H
#define VOXONE_UNIFIED_CAROUSEL_RENDERER_H

#include <stdint.h>

#include "options.h"
#include "../displays/dspcore.h"
#include "../displays/widgets/widgets.h"

#if DSP_MODEL != DSP_DUMMY

struct UnifiedCarouselLayout {
  WidgetConfig header;
  WidgetConfig item;
  WidgetConfig counter;
  WidgetConfig playing;
  uint16_t itemWidth;
};

struct UnifiedCarouselTheme {
  uint16_t background;
  uint16_t headerForeground;
  uint16_t headerBackground;
  uint16_t itemForeground;
  uint16_t itemBackground;
  uint16_t footerForeground;
};

struct UnifiedCarouselModel {
  const char* header;
  const char* item;
  const char* emptyText;
  uint16_t selected;
  uint16_t total;
  bool playing;
  bool editing;
};

class UnifiedCarouselRenderer {
 public:
  void render(const UnifiedCarouselLayout& layout,
              const UnifiedCarouselTheme& theme,
              const UnifiedCarouselModel& model) const;
};

class UnifiedCarouselWidget : public Widget {
 public:
  void init(const UnifiedCarouselLayout& layout,
            const UnifiedCarouselTheme& theme);
  void setTheme(const UnifiedCarouselTheme& theme);
  void setModel(const UnifiedCarouselModel& model);

 private:
  UnifiedCarouselLayout layout_{};
  UnifiedCarouselTheme theme_{};
  char header_[64]{};
  char item_[160]{};
  char emptyText_[48]{};
  uint16_t selected_ = 0;
  uint16_t total_ = 0;
  bool playing_ = false;
  bool editing_ = false;
  UnifiedCarouselRenderer renderer_{};

  void _draw() override;
  void _clear() override;
};

#endif

#endif
