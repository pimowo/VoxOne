#include "unified_carousel_renderer.h"

#include <stdio.h>
#include <string.h>

#include "carousel_list_window.h"
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

void formatModelItem(const UnifiedCarouselModel& model, uint16_t index,
                     char* output, size_t outputSize) {
  if (!output || outputSize == 0) return;
  output[0] = '\0';
  if (model.formatItem) {
    model.formatItem(model.itemContext, index, output, outputSize);
    return;
  }
  if (index + 1 == model.selected)
    snprintf(output, outputSize, "%s", model.item ? model.item : "");
}

bool modelItemIsActive(const UnifiedCarouselModel& model, uint16_t index) {
  if (model.itemIsActive)
    return model.itemIsActive(model.itemContext, index);
  return model.playing && index + 1 == model.selected;
}

void printOledListRow(const UnifiedCarouselTheme& theme,
                      const UnifiedCarouselModel& model, uint16_t index,
                      uint16_t top, bool selected) {
  const uint16_t rowHeight = CHARHEIGHT;
  const uint16_t rowBackground =
      selected ? theme.itemForeground : theme.background;
  const uint16_t rowForeground =
      selected ? theme.itemBackground : theme.itemForeground;
  dsp.fillRect(0, top, dsp.width(), rowHeight, rowBackground);

  const bool active = modelItemIsActive(model, index);
  const uint16_t markerWidth = active ? 4 * CHARWIDTH : 0;
  WidgetConfig itemPosition{2, top, 1, WA_LEFT};
  char item[160];
  formatModelItem(model, index, item, sizeof(item));
  const uint16_t textWidth = dsp.width() > itemPosition.left + markerWidth
      ? dsp.width() - itemPosition.left - markerWidth : 0;
  printEllipsized(item, itemPosition, textWidth, rowForeground,
                  rowBackground);
  if (active) {
    WidgetConfig playingPosition{2, top, 1, WA_RIGHT};
    printEllipsized("GRA", playingPosition, markerWidth, rowForeground,
                    rowBackground);
  }
}

void renderOledList(const UnifiedCarouselLayout& layout,
                    const UnifiedCarouselTheme& theme,
                    const UnifiedCarouselModel& model) {
  dsp.fillScreen(theme.background);
  printEllipsized(model.header, layout.header, 0, theme.headerForeground,
                  theme.headerBackground);

  constexpr uint16_t kHeaderHeight = CHARHEIGHT;
  const bool footerVisible = dsp.height() >= 64;
  const uint16_t footerHeight = footerVisible ? CHARHEIGHT : 0;
  const uint16_t availableHeight =
      dsp.height() > kHeaderHeight + footerHeight
          ? dsp.height() - kHeaderHeight - footerHeight : 0;
  const uint16_t rowCapacity = availableHeight / CHARHEIGHT;
  if (model.total == 0) {
    WidgetConfig emptyPosition{
        0, static_cast<uint16_t>(kHeaderHeight + availableHeight / 2 -
                                 CHARHEIGHT / 2),
        1, WA_CENTER};
    printEllipsized(model.emptyText, emptyPosition, dsp.width(),
                    theme.itemForeground, theme.background);
  } else {
    const auto window = voxone::carousel::listWindow(
        model.total, model.selected, rowCapacity);
    for (uint16_t row = 0; row < window.count; ++row) {
      const uint16_t index = window.first + row;
      printOledListRow(theme, model, index,
                       kHeaderHeight + row * CHARHEIGHT,
                       index + 1 == model.selected);
    }
  }

  if (footerVisible) {
    char counter[20];
    snprintf(counter, sizeof(counter), "%u/%u",
             static_cast<unsigned>(model.total ? model.selected : 0),
             static_cast<unsigned>(model.total));
    printEllipsized(counter, layout.counter, 0, theme.footerForeground,
                    theme.background);
  }
}

}  // namespace

void UnifiedCarouselRenderer::render(
    const UnifiedCarouselLayout& layout, const UnifiedCarouselTheme& theme,
    const UnifiedCarouselModel& model) const {
  if (dsp.height() <= 64) {
    renderOledList(layout, theme, model);
    return;
  }
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
  itemContext_ = model.itemContext;
  formatItem_ = model.formatItem;
  itemIsActive_ = model.itemIsActive;
  if (_active && !_locked) _draw();
}

void UnifiedCarouselWidget::_draw() {
  if (!_active || _locked) return;
  const UnifiedCarouselModel model{
      header_, item_, emptyText_, selected_, total_, playing_, editing_,
      itemContext_, formatItem_, itemIsActive_};
  renderer_.render(layout_, theme_, model);
}

void UnifiedCarouselWidget::_clear() {
  dsp.fillScreen(theme_.background);
}

#endif
