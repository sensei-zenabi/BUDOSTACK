#include "../budo/BUDOWIN/sdk/ui.h"
#include <assert.h>
#include <stdio.h>

static const int values[] = {INT_MIN, -1, 0, 1, 12, 24, 25, 200, INT_MAX - 1, INT_MAX};
#define VALUE_COUNT (sizeof(values) / sizeof(values[0]))

static int expected_limit(int total, int page)
{
    long long result = (total > 0 ? (long long)total : 0) - (page > 0 ? (long long)page : 0);
    return result > 0 ? (int)result : 0;
}

static void test_scroll_boundaries(void)
{
    for (size_t t = 0; t < VALUE_COUNT; ++t)
    for (size_t p = 0; p < VALUE_COUNT; ++p)
    for (size_t l = 0; l < VALUE_COUNT; ++l)
    for (size_t v = 0; v < VALUE_COUNT; ++v) {
        BudoScrollbar bar = {.total = values[t], .page = values[p],
                             .length = values[l], .position = values[v]};
        int limit = expected_limit(bar.total, bar.page);
        int start, size;
        assert(budo_scroll_limit(&bar) == limit);
        budo_scroll_thumb(&bar, &start, &size);
        assert(bar.position >= 0 && bar.position <= limit);
        long long track = (long long)bar.length - 24;
        if (track < 1) track = 1;
        assert(size >= 1 && size <= track);
        assert(start >= 12 && (long long)start + size <= track + 12);
        if (!bar.position) assert(start == 12);
        if (limit && bar.position == limit) assert(start == 12 + track - size);
        for (int direction = -2; direction <= 2; ++direction) {
            if (!direction) continue;
            BudoScrollbar step = bar;
            long long amount = direction == -1 ? -1 : direction == 1 ? 1 :
                               direction == -2 ? -(long long)(bar.page > 0 ? bar.page : 0) :
                                                 (long long)(bar.page > 0 ? bar.page : 0);
            long long expected = (long long)bar.position + amount;
            if (expected < 0) expected = 0;
            if (expected > limit) expected = limit;
            budo_scroll_step(&step, direction);
            assert(step.position == expected);
        }
        /* Dragging may continue after a resize to an invalid/short track. */
        bar.dragging = 1;
        bar.grab = values[v];
        bar.x = values[t];
        bar.y = values[p];
        for (int horizontal = 0; horizontal <= 1; ++horizontal) {
            bar.horizontal = horizontal;
            assert(budo_scroll_pointer_at(&bar, INT_MIN, INT_MAX, BUDO_POINTER_MOVE, 0));
            assert(bar.position >= 0 && bar.position <= limit);
            assert(budo_scroll_pointer_at(&bar, INT_MAX, INT_MIN, BUDO_POINTER_MOVE, 0));
            assert(bar.position >= 0 && bar.position <= limit);
        }
    }
}

static void test_scroll_interaction(void)
{
    for (int horizontal = 0; horizontal <= 1; ++horizontal) {
        BudoScrollbar bar = {.length = 200, .total = INT_MAX, .page = 0,
                             .position = INT_MAX, .horizontal = horizontal};
        int mx = horizontal ? 199 : 1;
        int my = horizontal ? 1 : 199;
        assert(budo_scroll_pointer_at(&bar, mx, my, BUDO_POINTER_DOWN, 0));
        assert(bar.position == INT_MAX);
        assert(budo_scroll_pointer_at(&bar, mx, my, BUDO_POINTER_MOVE, 350));
        assert(bar.position == INT_MAX);
        assert(budo_scroll_pointer_at(&bar, mx, my, BUDO_POINTER_UP, 351));
        bar.page = INT_MAX / 2;
        bar.position = 1;
        mx = horizontal ? 180 : 1;
        my = horizontal ? 1 : 180;
        assert(budo_scroll_pointer_at(&bar, mx, my, BUDO_POINTER_DOWN, 1000));
        assert(bar.position == budo_scroll_limit(&bar));
        assert(budo_scroll_pointer_at(&bar, mx, my, BUDO_POINTER_MOVE, 1350));
        assert(bar.position == budo_scroll_limit(&bar));
        assert(budo_scroll_pointer_at(&bar, mx, my, BUDO_POINTER_UP, 1351));
    }
    /* Former normal-input geometry, including minimum thumb size. */
    for (int total = 1; total <= 100; ++total)
    for (int page = 0; page <= 120; ++page) {
        BudoScrollbar bar = {.length = 200, .total = total, .page = page};
        int size, start;
        budo_scroll_thumb(&bar, &start, &size);
        int old_size = 176 * page / total;
        if (old_size < 12) old_size = 12;
        if (old_size > 176) old_size = 176;
        assert(size == old_size && start == 12);
        int limit = total > page ? total - page : 0;
        for (int v = 0; v < 3; ++v) {
            bar.position = v * limit / 2;
            budo_scroll_thumb(&bar, &start, &size);
            int old_start = 12 + (limit ? (176 - old_size) * bar.position / limit : 0);
            assert(size == old_size && start == old_start);
        }
    }
}

static int draw_x, draw_y, draw_w, draw_h;
static void rect(int x, int y, int w, int h, unsigned char color)
{
    (void)color;
    draw_x = x; draw_y = y; draw_w = w; draw_h = h;
}
static void button(int x, int y, int w, int h, const char *label, int pressed)
{
    (void)label; (void)pressed;
    rect(x, y, w, h, 0);
}
static void text(int x, int y, const char *label, unsigned char color, int max_chars)
{
    (void)label; (void)max_chars;
    rect(x, y, 0, 0, color);
}
static unsigned char color(int role) { (void)role; return 0; }
static int metric(int role) { (void)role; return 20; }

static void test_coordinates(void)
{
    BwaHostApi host = {.fill_rect = rect, .draw_standard_button = button,
        .draw_sunken_panel = rect, .draw_text = text,
        .get_system_color = color, .get_system_metric = metric};
    BudoMenuItem item = {"test", 1, 1};
    for (size_t i = 0; i < VALUE_COUNT; ++i)
    for (size_t j = 0; j < VALUE_COUNT; ++j) {
        BudoScrollbar bar = {.x = values[i], .y = values[j], .length = values[j],
                             .total = INT_MAX, .page = 1};
        budo_scroll_draw(&host, &bar);
        bar.horizontal = 1;
        budo_scroll_draw(&host, &bar);
        budo_menu_bar_item(&host, values[i], values[j], values[i], "test", 1);
        budo_menu_row_draw(&host, values[i], values[j], values[i], &item);
        budo_menu_items_draw(&host, values[i], values[j], values[i], &item, 1);
        const char *labels[] = {"test"};
        budo_menu_draw(&host, values[i], values[j], values[i], labels, 1);
        budo_selection_icon_draw(&host, values[i], values[j], 3, 3, 1, 1);
        BudoSelection selection = {.dragging = 1, .start_x = INT_MIN, .start_y = INT_MIN,
                                   .end_x = values[i], .end_y = values[j]};
        budo_selection_drag_draw(&host, &selection);
        for (size_t k = 0; k < VALUE_COUNT; ++k) {
            int mx = values[k], my = values[j], x = values[i], y = values[k];
            /* double represents every 32-bit input and difference exactly. */
            double dx = (double)mx - x, dy = (double)my - y;
            int expected = -1;
            if (dx >= 2 && dx + 2 < values[j] && dy >= 2) {
                long long row = (long long)((dy - 2) / 16);
                if (row < values[i]) expected = (int)row;
            }
            assert(budo_menu_hit(mx, my, x, y, values[j], values[i]) == expected);
        }
    }
    BudoScrollbar bar = {.x = INT_MAX, .y = INT_MAX, .length = 200, .total = 100, .page = 10};
    budo_scroll_draw(&host, &bar);
    assert(draw_x == INT_MAX && draw_y == INT_MAX && draw_w == 12 && draw_h == 17);
    assert(budo_menu_hit(20, 25, 10, 20, 100, 3) == 0);
    assert(budo_menu_hit(20, 41, 10, 20, 100, 3) == 1);
    assert(budo_menu_hit(20, 70, 10, 20, 100, 3) == -1);
}

int main(void)
{
    assert(budo_ui_int(LLONG_MIN) == INT_MIN);
    assert(budo_ui_int(LLONG_MAX) == INT_MAX);
    assert(budo_ui_int(0) == 0);
    test_scroll_boundaries();
    test_scroll_interaction();
    test_coordinates();
    puts("PASS: 10000 scrollbar boundary combinations, stepping, dragging, repeat and UI coordinates");
    return 0;
}
