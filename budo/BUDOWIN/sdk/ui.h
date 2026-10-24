#ifndef BUDOWIN_UI_H
#define BUDOWIN_UI_H
#include "budowin.h"
#include "selection.h"
#include <limits.h>
#include <time.h>

#define BUDO_SCROLL_WIDTH 12
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

static inline void budo_text_caret(const BwaHostApi *host, int x, int y, int height, unsigned char color)
{
    if (host->abi_minor >= 17 && host->draw_input_caret)
        host->draw_input_caret(x, y, height, color);
    else if (host->abi_minor >= 12 && host->get_time_ms && (host->get_time_ms() / 500) % 2 == 0)
        host->fill_rect(x, y, 1, height, color);
}

static inline int budo_scroll_limit(const BudoScrollbar *bar)
{
    /* Counts are nonnegative. Subtract unsigned values only after checking
     * their order, so neither invalid input nor inlining can overflow int. */
    if (bar->total <= 0) return 0;
    if (bar->page <= 0) return bar->total;
    if (bar->page >= bar->total) return 0;
    return (int)((unsigned int)bar->total - (unsigned int)bar->page);
}

static inline void budo_scroll_clamp(BudoScrollbar *bar)
{
    int limit = budo_scroll_limit(bar);
    if (bar->position < 0) bar->position = 0;
    if (bar->position > limit) bar->position = limit;
}

static inline void budo_scroll_thumb(BudoScrollbar *bar, int *start, int *size)
{
    int track = bar->length > 2 * BUDO_SCROLL_WIDTH ?
                bar->length - 2 * BUDO_SCROLL_WIDTH : 1;
    int limit = budo_scroll_limit(bar);
    budo_scroll_clamp(bar);
    int page = bar->page > 0 ? bar->page : 0;
    if (bar->total > 0 && page > bar->total) page = bar->total;
    *size = bar->total > 0 ? (int)((long long)track * page / bar->total) : track;
    if (*size < 12) *size = 12;
    if (*size > track) *size = track;
    *start = BUDO_SCROLL_WIDTH + (limit ? (int)((long long)(track - *size) * bar->position / limit) : 0);
}

/* Widen before doing coordinate arithmetic; clamp only at the int host ABI.
 * long long also holds products of two nonnegative 32-bit int values. */
static inline int budo_ui_int(long long value)
{
    if (value < INT_MIN) return INT_MIN;
    if (value > INT_MAX) return INT_MAX;
    return (int)value;
}

static inline void budo_scroll_step(BudoScrollbar *bar, int direction)
{
    long long step = direction == -1 ? -1 : direction == 1 ? 1 :
                     direction == -2 ? -(long long)(bar->page > 0 ? bar->page : 0) :
                                       (long long)(bar->page > 0 ? bar->page : 0);
    bar->position = budo_ui_int((long long)bar->position + step);
    budo_scroll_clamp(bar);
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
    int w = horizontal ? bar->length : BUDO_SCROLL_WIDTH;
    int h = horizontal ? BUDO_SCROLL_WIDTH : bar->length;
    budo_scroll_thumb(bar, &start, &size);
    host->draw_sunken_panel(bar->x, bar->y, w, h,
                            host->get_system_color(BUDO_SYS_COLOR_MIDGRAY));
    unsigned int state = BUDO_BUTTON_IMMEDIATE |
        (budo_scroll_limit(bar) ? 0U : BUDO_BUTTON_DISABLED);
    budo_button_draw(host, bar->x, bar->y, BUDO_SCROLL_WIDTH, BUDO_SCROLL_WIDTH,
                      horizontal ? "<" : "^", state |
                      (bar->held == -1 && bar->armed ? BUDO_BUTTON_PRESSED : 0U));
    budo_button_draw(host, budo_ui_int((long long)bar->x + (horizontal ? (long long)bar->length - BUDO_SCROLL_WIDTH : 0)),
                               budo_ui_int((long long)bar->y + (horizontal ? 0 : (long long)bar->length - BUDO_SCROLL_WIDTH)),
                               BUDO_SCROLL_WIDTH, BUDO_SCROLL_WIDTH, horizontal ? ">" : "v", state |
                      (bar->held == 1 && bar->armed ? BUDO_BUTTON_PRESSED : 0U));
    host->draw_standard_button(budo_ui_int((long long)bar->x + (horizontal ? start : 0)),
                               budo_ui_int((long long)bar->y + (horizontal ? 0 : start)),
                               horizontal ? size : BUDO_SCROLL_WIDTH,
                               horizontal ? BUDO_SCROLL_WIDTH : size, "", bar->dragging);
    if (host->abi_minor >= 12 && host->pointer_region) {
        host->pointer_region(bar->x, bar->y, BUDO_SCROLL_WIDTH, BUDO_SCROLL_WIDTH, BUDO_CURSOR_ARROW,
                              horizontal ? "Scroll left" : "Scroll up");
        host->pointer_region(budo_ui_int((long long)bar->x + (horizontal ? (long long)bar->length - BUDO_SCROLL_WIDTH : 0)),
                              budo_ui_int((long long)bar->y + (horizontal ? 0 : (long long)bar->length - BUDO_SCROLL_WIDTH)),
                              BUDO_SCROLL_WIDTH, BUDO_SCROLL_WIDTH, BUDO_CURSOR_ARROW,
                              horizontal ? "Scroll right" : "Scroll down");
    }
}

static inline int budo_scroll_pointer_at(BudoScrollbar *bar, int mx, int my,
                                         int event, unsigned long long now)
{
    long long coordinate = bar->horizontal ? (long long)mx - bar->x : (long long)my - bar->y;
    long long cross = bar->horizontal ? (long long)my - bar->y : (long long)mx - bar->x;
    int start, size;
    int limit = budo_scroll_limit(bar);
    budo_scroll_thumb(bar, &start, &size);
    if (event == BUDO_POINTER_UP) {
        int handled = bar->dragging || bar->held;
        bar->dragging = bar->held = bar->armed = 0;
        return handled;
    }
    if (bar->held && event == BUDO_POINTER_MOVE) {
        int inside = cross >= 0 && cross < BUDO_SCROLL_WIDTH && coordinate >= 0 &&
                     coordinate < bar->length;
        int armed = inside && (bar->held == -1 ? coordinate < BUDO_SCROLL_WIDTH :
                     bar->held == 1 ? coordinate >= (long long)bar->length - BUDO_SCROLL_WIDTH :
                     bar->held == -2 ? coordinate < start : coordinate >= (long long)start + size);
        if (!armed || !bar->armed) bar->repeat_at = now + 350;
        bar->armed = armed;
        if (armed && now >= bar->repeat_at) {
            budo_scroll_step(bar, bar->held);
            bar->repeat_at = now + 60;
        }
        return 1;
    }
    if (bar->dragging && event == BUDO_POINTER_MOVE) {
        long long travel = (long long)bar->length - 2 * BUDO_SCROLL_WIDTH - size;
        long long offset = coordinate - BUDO_SCROLL_WIDTH - bar->grab;
        if (offset < 0) offset = 0;
        if (offset > travel) offset = travel;
        bar->position = travel > 0 ? (int)((long long)offset * limit / travel) : 0;
        return 1;
    }
    if (event != BUDO_POINTER_DOWN || coordinate < 0 ||
        coordinate >= bar->length || cross < 0 || cross >= BUDO_SCROLL_WIDTH) return 0;
    if (!limit) return 1;
    bar->held = bar->armed = 0;
    if (coordinate < BUDO_SCROLL_WIDTH) { budo_scroll_step(bar, -1); bar->held = -1; }
    else if (coordinate >= (long long)bar->length - BUDO_SCROLL_WIDTH) { budo_scroll_step(bar, 1); bar->held = 1; }
    else if (coordinate >= start && coordinate < (long long)start + size) {
        bar->dragging = 1;
        bar->grab = (int)(coordinate - start);
    } else if (coordinate < start) { budo_scroll_step(bar, -2); bar->held = -2; }
    else { budo_scroll_step(bar, 2); bar->held = 2; }
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
    host->draw_text(budo_ui_int((long long)x + 4), budo_ui_int((long long)y + 4), label,
                   host->get_system_color(active ? BUDO_SYS_COLOR_TITLE_TEXT : BUDO_SYS_COLOR_TEXT), (int)(((long long)width - 8) / 6));
}

static inline int budo_menu_hit(int mx, int my, int x, int y, int width, int count)
{
    if (width <= 4 || count <= 0 || mx < x || my < y) return -1;
    unsigned int dx = (unsigned int)mx - (unsigned int)x;
    unsigned int dy = (unsigned int)my - (unsigned int)y;
    if (dx < 2U || dx >= (unsigned int)width - 2U || dy < 2U) return -1;
    unsigned int row = (dy - 2U) / 16U;
    return row < (unsigned int)count ? (int)row : -1;
}

static inline void budo_menu_row_draw(const BwaHostApi *host, int x, int y,
                                      int width, const BudoMenuItem *item)
{
    int mx = -1, my = -1, buttons = 0;
    if (host->abi_minor >= 10 && host->get_pointer_state)
        host->get_pointer_state(&mx, &my, &buttons);
    int hover = item->enabled && budo_menu_hit(mx, my, x, y, width, 1) == 0;
    int role = item->enabled ? BUDO_SYS_COLOR_TEXT : BUDO_SYS_COLOR_SHADOW;
    if (hover) {
        host->fill_rect(budo_ui_int((long long)x + 2), budo_ui_int((long long)y + 2),
                        budo_ui_int((long long)width - 4), 16,
                        host->get_system_color(BUDO_SYS_COLOR_TITLE_ACTIVE));
        role = BUDO_SYS_COLOR_TITLE_TEXT;
    }
    if (item->checked) host->draw_text(budo_ui_int((long long)x + 5), budo_ui_int((long long)y + 4), "x",
                                       host->get_system_color(role), 1);
    host->draw_text(budo_ui_int((long long)x + 15), budo_ui_int((long long)y + 4), item->label,
                   host->get_system_color(role), (int)(((long long)width - 20) / 6));
}

static inline void budo_menu_items_draw(const BwaHostApi *host, int x, int y,
                                        int width, const BudoMenuItem *items, int count)
{
    host->draw_standard_button(x, y, width, budo_ui_int((long long)count * 16 + 4), "", 0);
    if (host->abi_minor >= 12 && host->pointer_region)
        host->pointer_region(x, y, width, budo_ui_int((long long)count * 16 + 4), BUDO_CURSOR_ARROW | BUDO_CURSOR_OVERLAY, NULL);
    for (int i = 0; i < count; ++i)
        budo_menu_row_draw(host, x, budo_ui_int((long long)y + (long long)i * 16), width, &items[i]);
}

static inline void budo_menu_draw(const BwaHostApi *host, int x, int y,
                                  int width, const char *const *labels, int count)
{
    host->draw_standard_button(x, y, width, budo_ui_int((long long)count * 16 + 4), "", 0);
    if (host->abi_minor >= 12 && host->pointer_region)
        host->pointer_region(x, y, width, budo_ui_int((long long)count * 16 + 4), BUDO_CURSOR_ARROW | BUDO_CURSOR_OVERLAY, NULL);
    for (int i = 0; i < count; ++i) {
        BudoMenuItem item = {labels[i], 1, 0};
        budo_menu_row_draw(host, x, budo_ui_int((long long)y + (long long)i * 16), width, &item);
    }
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
                                  host->get_system_color(host->abi_minor >= 14 ? BUDO_SYS_COLOR_SELECTION_BG : BUDO_SYS_COLOR_TITLE_ACTIVE));
    if (!focused) return;
    unsigned int color = host->get_system_color(BUDO_SYS_COLOR_SHADOW);
    for (long long edge = 0; edge < w; edge += 2) {
        host->fill_rect(budo_ui_int((long long)x + edge), y, 1, 1, color);
        host->fill_rect(budo_ui_int((long long)x + edge), budo_ui_int((long long)y + h - 1), 1, 1, color);
    }
    for (long long edge = 0; edge < h; edge += 2) {
        host->fill_rect(x, budo_ui_int((long long)y + edge), 1, 1, color);
        host->fill_rect(budo_ui_int((long long)x + w - 1), budo_ui_int((long long)y + edge), 1, 1, color);
    }
}

static inline void budo_selection_drag_draw(const BwaHostApi *host,
                                           const BudoSelection *s)
{
    if (!s->dragging) return;
    int x = s->end_x < s->start_x ? s->end_x : s->start_x;
    int y = s->end_y < s->start_y ? s->end_y : s->start_y;
    long long w = s->end_x < s->start_x ? (long long)s->start_x - s->end_x + 1 : (long long)s->end_x - s->start_x + 1;
    long long h = s->end_y < s->start_y ? (long long)s->start_y - s->end_y + 1 : (long long)s->end_y - s->start_y + 1;
    unsigned int color = host->get_system_color(host->abi_minor >= 14 ? BUDO_SYS_COLOR_SELECTION_BG : BUDO_SYS_COLOR_TITLE_ACTIVE);
    host->fill_rect(x, y, budo_ui_int(w), 1, color);
    host->fill_rect(x, budo_ui_int((long long)y + h - 1), budo_ui_int(w), 1, color);
    host->fill_rect(x, y, 1, budo_ui_int(h), color);
    host->fill_rect(budo_ui_int((long long)x + w - 1), y, 1, budo_ui_int(h), color);
}
#endif
