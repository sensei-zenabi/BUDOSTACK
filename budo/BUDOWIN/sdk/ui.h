#ifndef BUDOWIN_UI_H
#define BUDOWIN_UI_H
#include "budowin.h"
#include "selection.h"
#include <time.h>

#define BUDO_SCROLL_WIDTH 16
#define BUDO_POINTER_DOWN 1
#define BUDO_POINTER_MOVE 2
#define BUDO_POINTER_UP 3

typedef struct BudoScrollbar {
    int x, y, length, horizontal;
    int total, page, position;
    int dragging, grab;
    int held, armed;
    unsigned long long repeat_at;
} BudoScrollbar;

static inline int budo_scroll_limit(const BudoScrollbar *bar)
{
    return bar->total > bar->page ? bar->total - bar->page : 0;
}

static inline void budo_scroll_clamp(BudoScrollbar *bar)
{
    int limit = budo_scroll_limit(bar);
    if (bar->position < 0) bar->position = 0;
    if (bar->position > limit) bar->position = limit;
}

static inline void budo_scroll_thumb(BudoScrollbar *bar, int *start, int *size)
{
    int track = bar->length - 32;
    int limit = budo_scroll_limit(bar);
    budo_scroll_clamp(bar);
    if (track < 1) track = 1;
    *size = bar->total > 0 ? (int)((long long)track * bar->page / bar->total) : track;
    if (*size < 12) *size = 12;
    if (*size > track) *size = track;
    *start = 16 + (limit ? (int)((long long)(track - *size) * bar->position / limit) : 0);
}

static inline void budo_button_draw(const BwaHostApi *host, int x, int y,
                                     int w, int h, const char *label,
                                     unsigned int state)
{
    if (host->abi_minor >= 12 && host->draw_button_state)
        host->draw_button_state(x, y, w, h, label, state);
    else host->draw_standard_button(x, y, w, h, label,
                                    (state & BUDO_BUTTON_PRESSED) != 0);
}

static inline void budo_scroll_draw(const BwaHostApi *host, BudoScrollbar *bar)
{
    int start, size;
    int horizontal = bar->horizontal;
    int w = horizontal ? bar->length : 16;
    int h = horizontal ? 16 : bar->length;
    budo_scroll_thumb(bar, &start, &size);
    host->draw_sunken_panel(bar->x, bar->y, w, h,
                            host->get_system_color(BUDO_SYS_COLOR_MIDGRAY));
    unsigned int state = BUDO_BUTTON_IMMEDIATE |
        (budo_scroll_limit(bar) ? 0U : BUDO_BUTTON_DISABLED);
    budo_button_draw(host, bar->x, bar->y, 16, 16,
                      horizontal ? "<" : "^", state |
                      (bar->held == -1 && bar->armed ? BUDO_BUTTON_PRESSED : 0U));
    budo_button_draw(host, bar->x + (horizontal ? bar->length - 16 : 0),
                               bar->y + (horizontal ? 0 : bar->length - 16),
                               16, 16, horizontal ? ">" : "v", state |
                      (bar->held == 1 && bar->armed ? BUDO_BUTTON_PRESSED : 0U));
    host->draw_standard_button(bar->x + (horizontal ? start : 0),
                               bar->y + (horizontal ? 0 : start),
                               horizontal ? size : 16,
                               horizontal ? 16 : size, "", bar->dragging);
    if (host->abi_minor >= 12 && host->pointer_region) {
        host->pointer_region(bar->x, bar->y, 16, 16, BUDO_CURSOR_ARROW,
                              horizontal ? "Scroll left" : "Scroll up");
        host->pointer_region(bar->x + (horizontal ? bar->length - 16 : 0),
                              bar->y + (horizontal ? 0 : bar->length - 16),
                              16, 16, BUDO_CURSOR_ARROW,
                              horizontal ? "Scroll right" : "Scroll down");
    }
}

static inline int budo_scroll_pointer_at(BudoScrollbar *bar, int mx, int my,
                                         int event, unsigned long long now)
{
    int coordinate = bar->horizontal ? mx - bar->x : my - bar->y;
    int cross = bar->horizontal ? my - bar->y : mx - bar->x;
    int start, size;
    int limit = budo_scroll_limit(bar);
    budo_scroll_thumb(bar, &start, &size);
    if (event == BUDO_POINTER_UP) {
        int handled = bar->dragging || bar->held;
        bar->dragging = bar->held = bar->armed = 0;
        return handled;
    }
    if (bar->held && event == BUDO_POINTER_MOVE) {
        int inside = cross >= 0 && cross < 16 && coordinate >= 0 &&
                     coordinate < bar->length;
        int armed = inside && (bar->held == -1 ? coordinate < 16 :
                     bar->held == 1 ? coordinate >= bar->length - 16 :
                     bar->held == -2 ? coordinate < start : coordinate >= start + size);
        if (!armed || !bar->armed) bar->repeat_at = now + 350;
        bar->armed = armed;
        if (armed && now >= bar->repeat_at) {
            bar->position += bar->held == -1 ? -1 : bar->held == 1 ? 1 :
                             bar->held == -2 ? -bar->page : bar->page;
            budo_scroll_clamp(bar);
            bar->repeat_at = now + 60;
        }
        return 1;
    }
    if (bar->dragging && event == BUDO_POINTER_MOVE) {
        int travel = bar->length - 32 - size;
        int offset = coordinate - 16 - bar->grab;
        if (offset < 0) offset = 0;
        if (offset > travel) offset = travel;
        bar->position = travel > 0 ? (int)((long long)offset * limit / travel) : 0;
        return 1;
    }
    if (event != BUDO_POINTER_DOWN || coordinate < 0 ||
        coordinate >= bar->length || cross < 0 || cross >= 16) return 0;
    if (!limit) return 1;
    bar->held = bar->armed = 0;
    if (coordinate < 16) { --bar->position; bar->held = -1; }
    else if (coordinate >= bar->length - 16) { ++bar->position; bar->held = 1; }
    else if (coordinate >= start && coordinate < start + size) {
        bar->dragging = 1;
        bar->grab = coordinate - start;
    } else if (coordinate < start) { bar->position -= bar->page; bar->held = -2; }
    else { bar->position += bar->page; bar->held = 2; }
    bar->armed = bar->held != 0;
    bar->repeat_at = now + 350;
    budo_scroll_clamp(bar);
    return 1;
}

static inline int budo_scroll_pointer(BudoScrollbar *bar, int mx, int my, int event)
{
    struct timespec now;
    if (timespec_get(&now, TIME_UTC) != TIME_UTC) return 0;
    return budo_scroll_pointer_at(bar, mx, my, event,
        (unsigned long long)now.tv_sec * 1000 + (unsigned long long)now.tv_nsec / 1000000);
}

static inline int budo_scroll_pointer_host(const BwaHostApi *host,
                                           BudoScrollbar *bar, int x, int y, int event)
{
    if (host->abi_minor >= 12 && host->get_time_ms)
        return budo_scroll_pointer_at(bar, x, y, event, host->get_time_ms());
    return budo_scroll_pointer(bar, x, y, event);
}

/* Shared menu bars, bevels, disabled items and check marks. */
typedef struct BudoMenuItem {
    const char *label;
    int enabled;
    int checked;
} BudoMenuItem;

static inline void budo_menu_bar_item(const BwaHostApi *host, int x, int y,
                                      int width, const char *label, int active)
{
    int height = host->get_system_metric(BUDO_SYS_METRIC_MENU_HEIGHT);
    if (active) host->draw_standard_button(x, y, width, height, "", 1);
    host->draw_text(x + 4, y + 4, label,
                   host->get_system_color(BUDO_SYS_COLOR_TEXT), (width - 8) / 6);
}

static inline void budo_menu_row_draw(const BwaHostApi *host, int x, int y,
                                      int width, const BudoMenuItem *item)
{
    int mx = -1, my = -1, buttons = 0;
    if (host->abi_minor >= 10 && host->get_pointer_state)
        host->get_pointer_state(&mx, &my, &buttons);
    int hover = item->enabled && mx >= x + 2 && mx < x + width - 2 &&
                my >= y + 2 && my < y + 18;
    int role = item->enabled ? BUDO_SYS_COLOR_TEXT : BUDO_SYS_COLOR_SHADOW;
    if (hover) {
        host->fill_rect(x + 2, y + 2, width - 4, 16,
                        host->get_system_color(BUDO_SYS_COLOR_TITLE_ACTIVE));
        role = BUDO_SYS_COLOR_TITLE_TEXT;
    }
    if (item->checked) host->draw_text(x + 5, y + 4, "x",
                                       host->get_system_color(role), 1);
    host->draw_text(x + 15, y + 4, item->label,
                   host->get_system_color(role), (width - 20) / 6);
}

static inline void budo_menu_items_draw(const BwaHostApi *host, int x, int y,
                                        int width, const BudoMenuItem *items, int count)
{
    host->draw_standard_button(x, y, width, count * 16 + 4, "", 0);
    if (host->abi_minor >= 12 && host->pointer_region)
        host->pointer_region(x, y, width, count * 16 + 4, BUDO_CURSOR_ARROW | BUDO_CURSOR_OVERLAY, NULL);
    for (int i = 0; i < count; ++i)
        budo_menu_row_draw(host, x, y + i * 16, width, &items[i]);
}

static inline void budo_menu_draw(const BwaHostApi *host, int x, int y,
                                  int width, const char *const *labels, int count)
{
    host->draw_standard_button(x, y, width, count * 16 + 4, "", 0);
    if (host->abi_minor >= 12 && host->pointer_region)
        host->pointer_region(x, y, width, count * 16 + 4, BUDO_CURSOR_ARROW | BUDO_CURSOR_OVERLAY, NULL);
    for (int i = 0; i < count; ++i) {
        BudoMenuItem item = {labels[i], 1, 0};
        budo_menu_row_draw(host, x, y + i * 16, width, &item);
    }
}

static inline int budo_menu_hit(int mx, int my, int x, int y, int width, int count)
{
    if (mx < x + 2 || mx >= x + width - 2 || my < y + 2 ||
        my >= y + 2 + count * 16) return -1;
    return (my - y - 2) / 16;
}
static inline unsigned int budo_selection_modifiers(const BwaHostApi *host)
{
    return host->abi_minor >= 11 && host->get_keyboard_modifiers ?
           host->get_keyboard_modifiers() : 0;
}

/* Paint only the icon rectangle; labels and grid gutters remain untouched. */
static inline void budo_selection_icon_draw(const BwaHostApi *host, int x, int y,
                                           int w, int h, int selected, int focused)
{
    if (w <= 0 || h <= 0) return;
    if (selected) host->fill_rect(x, y, w, h,
                                  host->get_system_color(BUDO_SYS_COLOR_TITLE_ACTIVE));
    if (!focused) return;
    unsigned int color = host->get_system_color(BUDO_SYS_COLOR_SHADOW);
    for (int edge = 0; edge < w; edge += 2) {
        host->fill_rect(x + edge, y, 1, 1, color);
        host->fill_rect(x + edge, y + h - 1, 1, 1, color);
    }
    for (int edge = 0; edge < h; edge += 2) {
        host->fill_rect(x, y + edge, 1, 1, color);
        host->fill_rect(x + w - 1, y + edge, 1, 1, color);
    }
}

static inline void budo_selection_drag_draw(const BwaHostApi *host,
                                           const BudoSelection *s)
{
    if (!s->dragging) return;
    int x = s->end_x < s->start_x ? s->end_x : s->start_x;
    int y = s->end_y < s->start_y ? s->end_y : s->start_y;
    int w = s->end_x < s->start_x ? s->start_x - s->end_x + 1 : s->end_x - s->start_x + 1;
    int h = s->end_y < s->start_y ? s->start_y - s->end_y + 1 : s->end_y - s->start_y + 1;
    unsigned int color = host->get_system_color(BUDO_SYS_COLOR_TITLE_ACTIVE);
    host->fill_rect(x, y, w, 1, color);
    host->fill_rect(x, y + h - 1, w, 1, color);
    host->fill_rect(x, y, 1, h, color);
    host->fill_rect(x + w - 1, y, 1, h, color);
}
#endif
