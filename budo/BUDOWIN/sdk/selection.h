#ifndef BUDOWIN_SELECTION_H
#define BUDOWIN_SELECTION_H
#include <stddef.h>
#include <string.h>

#define BUDO_SELECT_SHIFT 0x03u
#define BUDO_SELECT_CTRL 0x04u

typedef struct BudoSelectionRect {
    int id;
    int x, y, w, h;
} BudoSelectionRect;

/* IDs index caller-owned storage; order contains only the currently visible
 * IDs. Separate instances keep each application's selection independent.
 * The selection model needs no heap allocation. */
typedef struct BudoSelection {
    unsigned char *selected;
    unsigned char *snapshot;
    size_t capacity;
    int focus, anchor;
    int dragging;
    int start_x, start_y, end_x, end_y;
    int drag_focus, drag_anchor;
    unsigned int drag_modifiers;
} BudoSelection;

static inline void budo_selection_init(BudoSelection *s, unsigned char *selected,
                                       unsigned char *snapshot, size_t capacity)
{
    memset(s, 0, sizeof(*s));
    s->selected = selected;
    s->snapshot = snapshot;
    s->capacity = capacity;
    s->focus = s->anchor = -1;
    if (capacity) memset(selected, 0, capacity);
}

static inline int budo_selection_valid(const BudoSelection *s, int id)
{
    return id >= 0 && (size_t)id < s->capacity;
}

static inline int budo_selection_id(const int *order, int position)
{
    return order ? order[position] : position;
}

static inline int budo_selection_position(const int *order, int count, int id)
{
    if (!order) return id >= 0 && id < count ? id : -1;
    for (int i = 0; i < count; ++i) {
        if (budo_selection_id(order, i) == id) return i;
    }
    return -1;
}

static inline void budo_selection_clear(BudoSelection *s)
{
    if (s->capacity) memset(s->selected, 0, s->capacity);
    s->focus = s->anchor = -1;
    s->dragging = 0;
}

static inline void budo_selection_range(BudoSelection *s, const int *order,
                                        int count, int first, int last, int add)
{
    int a = budo_selection_position(order, count, first);
    int b = budo_selection_position(order, count, last);
    if (!budo_selection_valid(s, last) || b < 0) return;
    if (a < 0) {
        first = last;
        a = b;
    }
    if (!add) budo_selection_clear(s);
    int low = a < b ? a : b;
    int high = a > b ? a : b;
    for (int i = low; i <= high; ++i) {
        if (budo_selection_valid(s, budo_selection_id(order, i))) s->selected[budo_selection_id(order, i)] = 1;
    }
    s->focus = last;
    s->anchor = first;
}

static inline void budo_selection_click(BudoSelection *s, const int *order,
                                        int count, int id, unsigned int modifiers,
                                        int context)
{
    if (!budo_selection_valid(s, id)) {
        if (!(modifiers & (BUDO_SELECT_CTRL | BUDO_SELECT_SHIFT)))
            budo_selection_clear(s);
        return;
    }
    if (context) {
        if (!s->selected[id]) {
            budo_selection_clear(s);
            s->selected[id] = 1;
            s->anchor = id;
        }
    } else if ((modifiers & BUDO_SELECT_SHIFT) && s->anchor >= 0) {
        budo_selection_range(s, order, count, s->anchor, id,
                             (modifiers & BUDO_SELECT_CTRL) != 0);
    } else if (modifiers & BUDO_SELECT_CTRL) {
        s->selected[id] = !s->selected[id];
        s->anchor = id;
    } else {
        budo_selection_clear(s);
        s->selected[id] = 1;
        s->anchor = id;
    }
    s->focus = id;
}

static inline void budo_selection_all(BudoSelection *s, const int *order, int count)
{
    int focus = s->focus;
    budo_selection_clear(s);
    for (int i = 0; i < count; ++i) {
        if (!budo_selection_valid(s, budo_selection_id(order, i))) continue;
        s->selected[budo_selection_id(order, i)] = 1;
        if (s->focus < 0) s->focus = budo_selection_id(order, i);
    }
    if (budo_selection_position(order, count, focus) >= 0) s->focus = focus;
    s->anchor = s->focus;
}

/* DOS navigation scan codes, as delivered by BUDOWIN key callbacks. */
static inline int budo_selection_move(BudoSelection *s, const int *order,
                                      int count, int scan, int columns,
                                      int page_size, unsigned int modifiers)
{
    if (count <= 0) return -1;
    int position = budo_selection_position(order, count, s->focus);
    if (columns < 1) columns = 1;
    if (page_size < 1) page_size = 1;
    if (position < 0) position = 0;
    else if (scan == 75) --position;
    else if (scan == 77) ++position;
    else if (scan == 72) position -= columns;
    else if (scan == 80) position += columns;
    else if (scan == 73) position -= page_size;
    else if (scan == 81) position += page_size;
    if (scan == 71 || position < 0) position = 0;
    if (scan == 79 || position >= count) position = count - 1;
    int target = budo_selection_id(order, position);
    if ((modifiers & BUDO_SELECT_SHIFT) && s->anchor >= 0) {
        budo_selection_range(s, order, count, s->anchor, target,
                             (modifiers & BUDO_SELECT_CTRL) != 0);
    } else if (modifiers & BUDO_SELECT_CTRL) {
        s->focus = target;
        s->anchor = target;
    } else {
        budo_selection_click(s, order, count, target, 0, 0);
    }
    return position;
}

static inline void budo_selection_space(BudoSelection *s, const int *order,
                                        int count, unsigned int modifiers)
{
    if (budo_selection_valid(s, s->focus))
        budo_selection_click(s, order, count, s->focus, modifiers, 0);
}

static inline void budo_selection_drag_begin(BudoSelection *s, int x, int y,
                                             unsigned int modifiers)
{
    if (!s->snapshot) return;
    if (!(modifiers & (BUDO_SELECT_CTRL | BUDO_SELECT_SHIFT)))
        budo_selection_clear(s);
    memcpy(s->snapshot, s->selected, s->capacity);
    s->drag_focus = s->focus;
    s->drag_anchor = s->anchor;
    s->start_x = s->end_x = x;
    s->start_y = s->end_y = y;
    s->drag_modifiers = modifiers;
    s->dragging = 1;
}

static inline void budo_selection_drag_update(BudoSelection *s, int x, int y,
                                              const BudoSelectionRect *rects,
                                              int count)
{
    if (!s->dragging || !s->snapshot) return;
    s->end_x = x;
    s->end_y = y;
    int left = x < s->start_x ? x : s->start_x;
    int top = y < s->start_y ? y : s->start_y;
    int right = x > s->start_x ? x : s->start_x;
    int bottom = y > s->start_y ? y : s->start_y;
    memcpy(s->selected, s->snapshot, s->capacity);
    s->focus = s->drag_focus;
    s->anchor = s->drag_anchor;
    for (int i = 0; i < count; ++i) {
        const BudoSelectionRect *r = &rects[i];
        if (!budo_selection_valid(s, r->id) || r->w <= 0 || r->h <= 0) continue;
        if (right >= r->x && left < r->x + r->w &&
            bottom >= r->y && top < r->y + r->h) {
            s->selected[r->id] = (s->drag_modifiers & BUDO_SELECT_CTRL) ?
                                 !s->snapshot[r->id] : 1;
            s->focus = r->id;
            if (s->anchor < 0) s->anchor = r->id;
        }
    }
}
#endif
