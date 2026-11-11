#include "unified_carousel_renderer.h"

#include <stdio.h>
#include <string.h>

#include "options.h"
#include "utf8_text.h"
#include "../displays/dspcore.h"
#include "../displays/tools/utf8Rus.h"

#if DSP_MODEL != DSP_DUMMY

extern DspCore dsp;

namespace {

uint16_t textLeft(const WidgetConfig& position, uint16_t textWidth) {
  switch (position.align) {
    case WA_CENTER:
      return dsp.width() > textWidth ? (dsp.width() - textWidth) / 2 : 0;
    case WA_RIGHT:
      return dsp.width() > textWidth + position.left
                 ? dsp.width() - textWidth - position.left
                 : 0;
    case WA_LEFT:
    default:
      return position.left;
  }
}

uint16_t availableWidth(const WidgetConfig& position, uint16_t preferred) {
  if (preferred) return preferred > dsp.width() ? dsp.width() : preferred;
  if (position.align == WA_LEFT)
    return position.left < dsp.width() ? dsp.width() - position.left : 0;
  if (position.align == WA_RIGHT)
    return position.left < dsp.width() ? dsp.width() - position.left : 0;
  return dsp.width();
}

void printEllipsized(const char* text, const WidgetConfig& position,
                     uint16_t width, uint16_t foreground,
                     uint16_t background) {
  const uint16_t glyphWidth = CHARWIDTH * position.textsize;
  const uint16_t boundedWidth = availableWidth(position, width);
  char visible[160];
  voxone::utf8::ellipsize(visible, sizeof(visible), text ? text : "",
                          glyphWidth ? boundedWidth / glyphWidth : 0);
  const uint16_t textWidth = static_cast<uint16_t>(
      voxone::utf8::count(visible) * glyphWidth);
  char encoded[160];
  utf8Rus(encoded, sizeof(encoded), visible);
  dsp.setFont();
  dsp.setTextSize(position.textsize);
  dsp.setTextColor(foreground, background);
  dsp.setClipping({0, position.top, static_cast<uint16_t>(dsp.width()),
                   static_cast<uint16_t>(CHARHEIGHT * position.textsize)});
  dsp.setCursor(textLeft(position, textWidth), position.top);
  dsp.print(encoded);
  dsp.clearClipping();
}

}  // namespace

void UnifiedCarouselRenderer::render(
    const UnifiedCarouselLayout& layout, const UnifiedCarouselTheme& theme,
    const UnifiedCarouselModel& model) const {
  dsp.fillScreen(theme.background);
  printEllipsized(model.header, layout.header, 0, theme.headerForeground,
                  theme.headerBackground);

  const char* item = model.total ? model.item : model.emptyText;
  const uint16_t itemForeground =
      model.editing ? theme.itemBackground : theme.itemForeground;
  const uint16_t itemBackground =
      model.editing ? theme.itemForeground : theme.itemBackground;
  printEllipsized(item, layout.item, layout.itemWidth, itemForeground,
                  itemBackground);

  char counter[20];
  snprintf(counter, sizeof(counter), "%u/%u",
           static_cast<unsigned>(model.total ? model.selected : 0),
           static_cast<unsigned>(model.total));
  printEllipsized(counter, layout.counter, 0, theme.footerForeground,
                  theme.background);
  if (model.playing)
    printEllipsized("GRA", layout.playing, 0, theme.footerForeground,
                    theme.background);
}

void UnifiedCarouselWidget::init(const UnifiedCarouselLayout& layout,
                                 const UnifiedCarouselTheme& theme) {
  Widget::init({0, 0, 1, WA_LEFT}, theme.itemForeground, theme.background);
  layout_ = layout;
  theme_ = theme;
  snprintf(emptyText_, sizeof(emptyText_), "%s", "Brak elementów");
}

void UnifiedCarouselWidget::setTheme(const UnifiedCarouselTheme& theme) {
  theme_ = theme;
  _fgcolor = theme.itemForeground;
  _bgcolor = theme.background;
}

void UnifiedCarouselWidget::setModel(const UnifiedCarouselModel& model) {
  snprintf(header_, sizeof(header_), "%s", model.header ? model.header : "");
  snprintf(item_, sizeof(item_), "%s", model.item ? model.item : "");
  snprintf(emptyText_, sizeof(emptyText_), "%s",
           model.emptyText ? model.emptyText : "Brak elementów");
  selected_ = model.selected;
  total_ = model.total;
  playing_ = model.playing;
  editing_ = model.editing;
  if (_active && !_locked) _draw();
}

void UnifiedCarouselWidget::_draw() {
  if (!_active || _locked) return;
  const UnifiedCarouselModel model{header_, item_, emptyText_, selected_,
                                   total_, playing_, editing_};
  renderer_.render(layout_, theme_, model);
}

void UnifiedCarouselWidget::_clear() {
  dsp.fillScreen(theme_.background);
}

#endif
