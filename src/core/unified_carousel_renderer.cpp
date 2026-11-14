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

void printMarquee(const char* text, const WidgetConfig& position,
                  uint16_t width, uint16_t foreground, uint16_t background,
                  uint16_t offset, uint16_t cycleWidth) {
  const uint16_t boundedWidth = availableWidth(position, width);
  const uint16_t glyphWidth = CHARWIDTH * position.textsize;
  const uint16_t textWidth = static_cast<uint16_t>(
      voxone::utf8::count(text ? text : "") * glyphWidth);
  if (!boundedWidth || textWidth <= boundedWidth || !cycleWidth) {
    printEllipsized(text, position, width, foreground, background);
    return;
  }

  const uint16_t left = position.align == WA_CENTER
      ? (dsp.width() > boundedWidth ? (dsp.width() - boundedWidth) / 2 : 0)
      : position.align == WA_RIGHT
          ? (dsp.width() > boundedWidth + position.left
                 ? dsp.width() - boundedWidth - position.left : 0)
          : position.left;
  const uint16_t height = CHARHEIGHT * position.textsize;
  char encoded[160];
  utf8Rus(encoded, sizeof(encoded), text ? text : "");
  dsp.fillRect(left, position.top, boundedWidth, height, background);
  dsp.setFont();
  dsp.setTextSize(position.textsize);
  dsp.setTextColor(foreground, background);
  dsp.setClipping({left, position.top, boundedWidth, height});
  dsp.setCursor(static_cast<int16_t>(left) - offset, position.top);
  dsp.print(encoded);
  dsp.setCursor(static_cast<int16_t>(left) - offset + cycleWidth,
                position.top);
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
                      uint16_t top, bool selected, bool marquee,
                      uint16_t marqueeOffset, uint16_t marqueeCycle) {
  const uint16_t rowHeight = CHARHEIGHT;
  const uint16_t rowBackground = theme.background;
  const uint16_t rowForeground = theme.itemForeground;
  dsp.fillRect(0, top, dsp.width(), rowHeight, rowBackground);
  if (selected && rowHeight > 2)
    dsp.fillRect(0, top + 1, 2, rowHeight - 2, theme.itemForeground);

  const bool active = modelItemIsActive(model, index);
  const uint16_t markerWidth = active ? 4 * CHARWIDTH : 0;
  WidgetConfig itemPosition{static_cast<uint16_t>(selected ? 4 : 2), top, 1,
                            WA_LEFT};
  char item[160];
  formatModelItem(model, index, item, sizeof(item));
  const uint16_t textWidth = dsp.width() > itemPosition.left + markerWidth
      ? dsp.width() - itemPosition.left - markerWidth : 0;
  if (marquee)
    printMarquee(item, itemPosition, textWidth, rowForeground, rowBackground,
                 marqueeOffset, marqueeCycle);
  else
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
                    const UnifiedCarouselModel& model, bool marquee,
                    uint16_t marqueeOffset, uint16_t marqueeCycle) {
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
                       index + 1 == model.selected,
                       marquee && index + 1 == model.selected,
                       marqueeOffset, marqueeCycle);
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

uint16_t marqueeViewportWidth(const UnifiedCarouselLayout& layout,
                              const UnifiedCarouselModel& model) {
  if (dsp.height() > 64) return availableWidth(layout.item, layout.itemWidth);
  const bool active = model.total && model.selected &&
      modelItemIsActive(model, model.selected - 1);
  const uint16_t itemLeft = 4;
  const uint16_t markerWidth = active ? 4 * CHARWIDTH : 0;
  return dsp.width() > itemLeft + markerWidth
      ? dsp.width() - itemLeft - markerWidth : 0;
}

void selectedItemText(const UnifiedCarouselModel& model, char* output,
                      size_t outputSize) {
  if (!output || !outputSize) return;
  output[0] = '\0';
  if (!model.total || !model.selected) return;
  formatModelItem(model, model.selected - 1, output, outputSize);
}

}  // namespace

void UnifiedCarouselRenderer::render(
    const UnifiedCarouselLayout& layout, const UnifiedCarouselTheme& theme,
    const UnifiedCarouselModel& model) {
  selectedItemText(model, marqueeText_, sizeof(marqueeText_));
  const uint16_t glyphWidth = CHARWIDTH *
      (dsp.height() <= 64 ? 1 : layout.item.textsize);
  const uint16_t textWidth = static_cast<uint16_t>(
      voxone::utf8::count(marqueeText_) * glyphWidth);
  marquee_.reset(textWidth, marqueeViewportWidth(layout, model), glyphWidth,
                  millis());
  const bool marquee = model.marqueeItem && marquee_.active();
  if (dsp.height() <= 64) {
    renderOledList(layout, theme, model, marquee, marquee_.offset(),
                   marquee_.cycleWidth());
    return;
  }
  dsp.fillScreen(theme.background);
  printEllipsized(model.header, layout.header, 0, theme.headerForeground,
                  theme.headerBackground);

  const char* item = model.total ? model.item : model.emptyText;
  const uint16_t itemForeground = theme.itemForeground;
  const uint16_t itemBackground = theme.itemBackground;
  if (marquee)
    printMarquee(item, layout.item, layout.itemWidth, itemForeground,
                 itemBackground, marquee_.offset(), marquee_.cycleWidth());
  else
    printEllipsized(item, layout.item, layout.itemWidth, itemForeground,
                    itemBackground);
  if (model.editing) {
    const uint16_t width = availableWidth(layout.item, layout.itemWidth);
    const uint16_t markerWidth = width / 3;
    const uint16_t top = layout.item.top +
        CHARHEIGHT * layout.item.textsize;
    if (markerWidth && top < dsp.height())
      dsp.fillRect((dsp.width() - markerWidth) / 2, top, markerWidth, 1,
                   theme.itemForeground);
  }

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

void UnifiedCarouselRenderer::loop(
    const UnifiedCarouselLayout& layout, const UnifiedCarouselTheme& theme,
    const UnifiedCarouselModel& model) {
  if (!model.marqueeItem || !marquee_.step(millis())) return;
  if (dsp.height() <= 64) {
    constexpr uint16_t kHeaderHeight = CHARHEIGHT;
    const bool footerVisible = dsp.height() >= 64;
    const uint16_t footerHeight = footerVisible ? CHARHEIGHT : 0;
    const uint16_t availableHeight = dsp.height() > kHeaderHeight + footerHeight
        ? dsp.height() - kHeaderHeight - footerHeight : 0;
    const uint16_t rowCapacity = availableHeight / CHARHEIGHT;
    const auto window = voxone::carousel::listWindow(
        model.total, model.selected, rowCapacity);
    if (!model.selected || model.selected <= window.first ||
        model.selected > window.first + window.count) return;
    const uint16_t index = model.selected - 1;
    const uint16_t row = index - window.first;
    printOledListRow(theme, model, index,
                     kHeaderHeight + row * CHARHEIGHT, true, true,
                     marquee_.offset(), marquee_.cycleWidth());
    return;
  }
  printMarquee(marqueeText_, layout.item, layout.itemWidth,
               theme.itemForeground, theme.itemBackground,
               marquee_.offset(), marquee_.cycleWidth());
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
  marqueeItem_ = model.marqueeItem;
  if (_active && !_locked) _draw();
}

void UnifiedCarouselWidget::loop() {
  if (!_active || _locked) return;
  const UnifiedCarouselModel model{
      header_, item_, emptyText_, selected_, total_, playing_, editing_,
      itemContext_, formatItem_, itemIsActive_, marqueeItem_};
  renderer_.loop(layout_, theme_, model);
}

void UnifiedCarouselWidget::_draw() {
  if (!_active || _locked) return;
  const UnifiedCarouselModel model{
      header_, item_, emptyText_, selected_, total_, playing_, editing_,
      itemContext_, formatItem_, itemIsActive_, marqueeItem_};
  renderer_.render(layout_, theme_, model);
}

void UnifiedCarouselWidget::_clear() {
  dsp.fillScreen(theme_.background);
}

#endif
