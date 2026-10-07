#include "../sdk/ui.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN_X 20
#define WIN_Y 24
#define WIN_W 600
#define WIN_H 430
#define MAX_DIMENSION 2048
#define MAX_PIXELS (MAX_DIMENSION * MAX_DIMENSION)
#define FILE_BUFFER_SIZE (MAX_PIXELS * 2 + MAX_DIMENSION * 2 + 1024)
static int image_width = 384, image_height = 256;
#define CANVAS_W image_width
#define CANVAS_H image_height
#define PIXELS (CANVAS_W * CANVAS_H)
static int undo_width, undo_height;
static int zoom = 1, pixel_grid = 1;
static int resize_dialog, resize_field, resize_selected;
static char resize_width[8], resize_height[8];

#define TOOL_PENCIL 0
#define TOOL_BRUSH 1
#define TOOL_ERASER 2
#define TOOL_FILL 3
#define TOOL_LINE 4
#define TOOL_RECT 5
#define TOOL_FRECT 6
#define TOOL_OVAL 7
#define TOOL_FOVAL 8
#define TOOL_PICK 9
#define TOOL_COUNT 10

#define DIALOG_NONE 0
#define DIALOG_OPEN 1
#define DIALOG_SAVE_AS 2

static const BwaHostApi *host_api;
static unsigned char *canvas;
static unsigned char *undo_canvas;
static unsigned char *file_buffer;
static unsigned char canvas_palette[768];
static unsigned char undo_palette[768];
static unsigned char undo_fg, undo_bg;
static int palette_page, undo_palette_page;
static int *fill_queue;
static int undo_valid;
static int dirty;
static uint64_t saved_fingerprint;
static int tool = TOOL_PENCIL;
static unsigned char fg = 1;
static unsigned char bg = 4;
static int drawing;
static int draw_button;
static int start_x, start_y, last_x, last_y, preview_x, preview_y;
static int dialog_mode;
static int file_owned;
static int pending_action;
static char pending_path[4096];
static int paint_menu;
static BudoScrollbar canvas_hscroll, canvas_vscroll;
static int canvas_left, canvas_top;
static void paint_action(int action);
static int paint_save(void);
static char filename[4096] = "PAINT.PCX";
static char status_text[64] = "Ready";

static const char *tool_labels[TOOL_COUNT] = {
    "PEN", "BRUSH", "ERASE", "FILL", "LINE",
    "RECT", "FRECT", "OVAL", "FOVAL", "PICK"
};

static const unsigned char palette_rgb[16][3] = {
    {0,128,128}, {0,0,0}, {127,127,127}, {170,170,170},
    {255,255,255}, {194,194,194}, {0,0,160}, {0,0,255},
    {255,255,0}, {160,0,0}, {0,160,0}, {0,160,160},
    {160,0,160}, {160,80,0}, {200,200,200}, {255,255,255}
};

static int abs_i(int v) { return v < 0 ? -v : v; }
static int min_i(int a, int b) { return a < b ? a : b; }
static int max_i(int a, int b) { return a > b ? a : b; }

static void bytes_copy(unsigned char *dest,
                       const unsigned char *src,
                       unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count; ++i) dest[i] = src[i];
}

static void bytes_fill(unsigned char *dest,
                       unsigned char value,
                       unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count; ++i) dest[i] = value;
}

static void default_palette(void)
{
    int i;
    for (i = 0; i < 256; ++i) {
        unsigned char r, g, b;
        if (i < 16) {
            r = palette_rgb[i][0]; g = palette_rgb[i][1]; b = palette_rgb[i][2];
        } else if (i < 232) {
            int c = i - 16;
            r = (unsigned char)((c / 36) * 51);
            g = (unsigned char)(((c / 6) % 6) * 51);
            b = (unsigned char)((c % 6) * 51);
        } else {
            r = g = b = (unsigned char)((i - 232) * 255 / 23);
        }
        canvas_palette[i * 3] = r;
        canvas_palette[i * 3 + 1] = g;
        canvas_palette[i * 3 + 2] = b;
    }
    fg = 1;
    bg = 4;
    palette_page = 0;
}

static unsigned int canvas_rgb(unsigned char index)
{
    return ((unsigned int)canvas_palette[index * 3] << 16) |
           ((unsigned int)canvas_palette[index * 3 + 1] << 8) |
           canvas_palette[index * 3 + 2];
}

static uint64_t paint_content_hash(void)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (int i = 0; i < PIXELS; ++i)
        hash = (hash ^ canvas[i]) * UINT64_C(1099511628211);
    for (int i = 0; i < 768; ++i)
        hash = (hash ^ canvas_palette[i]) * UINT64_C(1099511628211);
    hash = (hash ^ (unsigned int)CANVAS_W) * UINT64_C(1099511628211);
    return (hash ^ (unsigned int)CANVAS_H) * UINT64_C(1099511628211);
}

static int ensure_buffers(void)
{
    if (canvas != 0 && undo_canvas != 0 &&
        file_buffer != 0 && fill_queue != 0) {
        return 1;
    }

    canvas = (unsigned char *)host_api->memory_alloc(MAX_PIXELS);
    undo_canvas = (unsigned char *)host_api->memory_alloc(MAX_PIXELS);
    file_buffer = (unsigned char *)host_api->memory_alloc(FILE_BUFFER_SIZE);
    fill_queue = (int *)host_api->memory_alloc(
        (unsigned int)(MAX_PIXELS * sizeof(int)));

    if (canvas == 0 || undo_canvas == 0 ||
        file_buffer == 0 || fill_queue == 0) {
        if (fill_queue != 0) host_api->memory_free(fill_queue);
        if (file_buffer != 0) host_api->memory_free(file_buffer);
        if (undo_canvas != 0) host_api->memory_free(undo_canvas);
        if (canvas != 0) host_api->memory_free(canvas);
        canvas = 0;
        undo_canvas = 0;
        file_buffer = 0;
        fill_queue = 0;
        return 0;
    }

    default_palette();
    bytes_fill(canvas, 4, MAX_PIXELS);
    bytes_fill(undo_canvas, 4, MAX_PIXELS);
    undo_valid = 0;
    dirty = 0;
    saved_fingerprint = paint_content_hash();
    return 1;
}

static void set_status(const char *s)
{
    unsigned int i = 0;
    while (s[i] != '\0' && i + 1 < sizeof(status_text)) {
        status_text[i] = s[i];
        ++i;
    }
    status_text[i] = '\0';
}

static void copy_name(char *dest, const char *src)
{
    int i = 0;
    while (src[i] != '\0' && i + 1 < 4096) {
        dest[i] = src[i];
        ++i;
    }
    dest[i] = '\0';
}

static void refresh_title(void)
{
    if (canvas) dirty = paint_content_hash() != saved_fingerprint;
    if (host_api != 0 && host_api->window_set_title != 0) {
        char title[160];
        const char *name = strrchr(filename, '/');
        name = name ? name + 1 : filename;
        snprintf(title, sizeof(title), "Paint%s - [%.100s]",
                 dirty ? " *" : "", file_owned ? name : "Untitled");
        (void)host_api->window_set_title(title);
    }
}

static void save_undo(void)
{
    undo_width = CANVAS_W;
    undo_height = CANVAS_H;
    bytes_copy(undo_canvas, canvas, PIXELS);
    bytes_copy(undo_palette, canvas_palette, sizeof(canvas_palette));
    undo_fg = fg;
    undo_bg = bg;
    undo_palette_page = palette_page;
    undo_valid = 1;
}

static void do_undo(void)
{
    int i;
    if (!undo_valid) {
        set_status("Nothing to undo");
        return;
    }
    for (i = 0; i < max_i(PIXELS, undo_width * undo_height); ++i) {
        unsigned char t = canvas[i];
        canvas[i] = undo_canvas[i];
        undo_canvas[i] = t;
    }
    int dimension = CANVAS_W;
    CANVAS_W = undo_width; undo_width = dimension;
    dimension = CANVAS_H;
    CANVAS_H = undo_height; undo_height = dimension;
    canvas_left = canvas_top = 0;
    for (i = 0; i < 768; ++i) {
        unsigned char t = canvas_palette[i];
        canvas_palette[i] = undo_palette[i];
        undo_palette[i] = t;
    }
    unsigned char color = fg;
    fg = undo_fg; undo_fg = color;
    color = bg;
    bg = undo_bg; undo_bg = color;
    int page = palette_page;
    palette_page = undo_palette_page; undo_palette_page = page;
    dirty = 1;
    refresh_title();
    set_status("Undo");
}

static void clear_canvas(unsigned char color)
{
    save_undo();
    bytes_fill(canvas, color, PIXELS);
    dirty = 1;
    refresh_title();
}

static void new_dimensions(void)
{
    int x, y, width, height;
    CANVAS_W = 384;
    CANVAS_H = 256;
    if (host_api->window_get_client_rect &&
        host_api->window_get_client_rect(&x, &y, &width, &height)) {
        CANVAS_W = min_i(MAX_DIMENSION, max_i(1, width - 94 - BUDO_SCROLL_WIDTH - 6));
        CANVAS_H = min_i(MAX_DIMENSION, max_i(1, height - 101));
    }
}

static void new_canvas(void)
{
    save_undo();
    new_dimensions();
    zoom = 1;
    default_palette();
    bytes_fill(canvas, 4, PIXELS);
    dirty = 0;
    saved_fingerprint = paint_content_hash();
    refresh_title();
}

static void pixel(int x, int y, unsigned char color)
{
    if (x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H)
        canvas[y * CANVAS_W + x] = color;
}

static unsigned char pixel_at(int x, int y)
{
    if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H) return bg;
    return canvas[y * CANVAS_W + x];
}

static void line(int x0, int y0, int x1, int y1, unsigned char color)
{
    int dx = abs_i(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs_i(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = err * 2;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

static void brush(int x, int y, int radius, unsigned char color)
{
    int px, py;
    for (py = -radius; py <= radius; ++py)
        for (px = -radius; px <= radius; ++px)
            if (px * px + py * py <= radius * radius + 1)
                pixel(x + px, y + py, color);
}

static void rect_shape(int x0, int y0, int x1, int y1,
                       unsigned char color, int filled)
{
    int l = min_i(x0, x1), r = max_i(x0, x1);
    int t = min_i(y0, y1), b = max_i(y0, y1);
    int x, y;
    if (filled) {
        for (y = t; y <= b; ++y)
            for (x = l; x <= r; ++x)
                pixel(x, y, color);
    } else {
        for (x = l; x <= r; ++x) {
            pixel(x, t, color);
            pixel(x, b, color);
        }
        for (y = t; y <= b; ++y) {
            pixel(l, y, color);
            pixel(r, y, color);
        }
    }
}

static void oval_shape(int x0, int y0, int x1, int y1,
                       unsigned char color, int filled)
{
    int l = min_i(x0, x1), r = max_i(x0, x1);
    int t = min_i(y0, y1), b = max_i(y0, y1);
    int rx = (r - l) / 2, ry = (b - t) / 2;
    int cx = l + rx, cy = t + ry;
    int y;
    if (rx <= 0 || ry <= 0) {
        rect_shape(x0, y0, x1, y1, color, filled);
        return;
    }
    for (y = -ry; y <= ry; ++y) {
        long yy = (long)y * y;
        long ry2 = (long)ry * ry;
        long rx2 = (long)rx * rx;
        int x = 0;
        while (x < rx &&
               (long)(x + 1) * (x + 1) * ry2 + yy * rx2 <= rx2 * ry2)
            ++x;
        if (filled) {
            int px;
            for (px = -x; px <= x; ++px) pixel(cx + px, cy + y, color);
        } else {
            pixel(cx - x, cy + y, color);
            pixel(cx + x, cy + y, color);
        }
    }
}

static void flood(int sx, int sy, unsigned char replacement)
{
    int head = 0, tail = 0;
    unsigned char target;
    if (sx < 0 || sx >= CANVAS_W || sy < 0 || sy >= CANVAS_H) return;
    target = pixel_at(sx, sy);
    if (target == replacement) return;
    fill_queue[tail++] = sy * CANVAS_W + sx;
    canvas[sy * CANVAS_W + sx] = replacement;
    while (head < tail) {
        int p = fill_queue[head++];
        int x = p % CANVAS_W;
        int y = p / CANVAS_W;
        int n;
        if (x > 0) {
            n = p - 1;
            if (canvas[n] == target) { canvas[n] = replacement; fill_queue[tail++] = n; }
        }
        if (x + 1 < CANVAS_W) {
            n = p + 1;
            if (canvas[n] == target) { canvas[n] = replacement; fill_queue[tail++] = n; }
        }
        if (y > 0) {
            n = p - CANVAS_W;
            if (canvas[n] == target) { canvas[n] = replacement; fill_queue[tail++] = n; }
        }
        if (y + 1 < CANVAS_H) {
            n = p + CANVAS_W;
            if (canvas[n] == target) { canvas[n] = replacement; fill_queue[tail++] = n; }
        }
    }
}

static int nearest_color(const unsigned char *palette,
                         unsigned char r, unsigned char g, unsigned char b)
{
    int best = 0, i;
    long best_d = 0x7fffffffL;
    for (i = 0; i < 256; ++i) {
        long dr = (long)r - palette[i * 3];
        long dg = (long)g - palette[i * 3 + 1];
        long db = (long)b - palette[i * 3 + 2];
        long d = dr * dr + dg * dg + db * db;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

static int load_pcx(const char *path)
{
    const unsigned char *h;
    const unsigned char *pal;
    unsigned char *loaded;
    unsigned int size = 0;
    unsigned int pos;
    int xmin, ymin, xmax, ymax, w, hh, bpl, y;

    if (!host_api->file_read_all(path, file_buffer, FILE_BUFFER_SIZE, &size) ||
        size < 897U) return 0;
    h = file_buffer;
    if (h[0] != 10 || (h[2] != 0 && h[2] != 1) || h[3] != 8 || h[65] != 1)
        return 0;
    xmin = h[4] | ((int)h[5] << 8);
    ymin = h[6] | ((int)h[7] << 8);
    xmax = h[8] | ((int)h[9] << 8);
    ymax = h[10] | ((int)h[11] << 8);
    w = xmax - xmin + 1;
    hh = ymax - ymin + 1;
    bpl = h[66] | ((int)h[67] << 8);
    if (w <= 0 || hh <= 0 || w > MAX_DIMENSION || hh > MAX_DIMENSION || bpl < w || file_buffer[size - 769U] != 12)
        return 0;
    pal = file_buffer + size - 768U;
    loaded = (unsigned char *)host_api->memory_alloc((unsigned int)(w * hh));
    if (loaded == 0) return 0;
    bytes_fill(loaded, (unsigned char)nearest_color(pal, 255, 255, 255), (unsigned int)(w * hh));
    pos = 128U;
    for (y = 0; y < hh; ++y) {
        int decoded = 0;
        while (decoded < bpl) {
            int code, count = 1, value;
            if (pos >= size - 769U) goto invalid;
            code = file_buffer[pos++];
            if (h[2] == 1 && (code & 0xC0) == 0xC0) {
                count = code & 0x3F;
                if (count == 0 || pos >= size - 769U) goto invalid;
                value = file_buffer[pos++];
            } else {
                value = code;
            }
            if (count > bpl - decoded) goto invalid;
            while (count-- > 0) {
                if (decoded < w)
                    loaded[y * w + decoded] = (unsigned char)value;
                ++decoded;
            }
        }
    }
    save_undo();
    CANVAS_W = w;
    CANVAS_H = hh;
    bytes_copy(canvas, loaded, PIXELS);
    bytes_copy(canvas_palette, pal, sizeof(canvas_palette));
    host_api->memory_free(loaded);
    fg = (unsigned char)nearest_color(canvas_palette, 0, 0, 0);
    bg = (unsigned char)nearest_color(canvas_palette, 255, 255, 255);
    palette_page = 0;
    dirty = 0;
    saved_fingerprint = paint_content_hash();
    refresh_title();
    return 1;
invalid:
    host_api->memory_free(loaded);
    return 0;
}

static int buffer_put(unsigned int *pos, unsigned char value)
{
    if (*pos >= FILE_BUFFER_SIZE) return 0;
    file_buffer[(*pos)++] = value;
    return 1;
}

static int write_run(unsigned int *pos, int value, int count)
{
    while (count > 0) {
        int n = count > 63 ? 63 : count;

        if (n > 1 || (value & 0xC0) == 0xC0) {
            if (!buffer_put(pos, (unsigned char)(0xC0 | n)) ||
                !buffer_put(pos, (unsigned char)value)) {
                return 0;
            }
        } else if (!buffer_put(pos, (unsigned char)value)) {
            return 0;
        }

        count -= n;
    }

    return 1;
}

static int save_pcx(const char *path)
{
    unsigned int pos = 128U;
    int x, y, i;

    bytes_fill(file_buffer, 0, 128U);
    file_buffer[0] = 10;
    file_buffer[1] = 5;
    file_buffer[2] = 1;
    file_buffer[3] = 8;
    file_buffer[8] = (unsigned char)((CANVAS_W - 1) & 255);
    file_buffer[9] = (unsigned char)((CANVAS_W - 1) >> 8);
    file_buffer[10] = (unsigned char)((CANVAS_H - 1) & 255);
    file_buffer[11] = (unsigned char)((CANVAS_H - 1) >> 8);
    file_buffer[12] = 72;
    file_buffer[14] = 72;
    file_buffer[65] = 1;
    int bpl = (CANVAS_W + 1) & ~1;
    file_buffer[66] = (unsigned char)(bpl & 255);
    file_buffer[67] = (unsigned char)(bpl >> 8);
    file_buffer[68] = 1;

    for (y = 0; y < CANVAS_H; ++y) {
        x = 0;
        while (x < CANVAS_W) {
            int value = canvas[y * CANVAS_W + x];
            int run = 1;

            while (x + run < CANVAS_W && run < 63 &&
                   canvas[y * CANVAS_W + x + run] == value) {
                ++run;
            }

            if (!write_run(&pos, value, run)) return 0;
            x += run;
        }
        if (bpl > CANVAS_W && !write_run(&pos, 0, 1)) return 0;
    }

    if (!buffer_put(&pos, 12)) return 0;

    for (i = 0; i < 256; ++i) {
        unsigned char r = canvas_palette[i * 3];
        unsigned char g = canvas_palette[i * 3 + 1];
        unsigned char b = canvas_palette[i * 3 + 2];

        if (!buffer_put(&pos, r) ||
            !buffer_put(&pos, g) ||
            !buffer_put(&pos, b)) {
            return 0;
        }
    }

    if (!host_api->file_write_all(path, file_buffer, pos)) {
        return 0;
    }

    dirty = 0;
    saved_fingerprint = paint_content_hash();
    return 1;
}

static void begin_dialog(int mode)
{
    (void)host_api->file_dialog(mode, filename);
}

static void paint_continue(void)
{
    int action = pending_action;
    pending_action = 0;
    if (action == 1) {
        new_canvas();
        dirty = 0;
        saved_fingerprint = paint_content_hash();
        file_owned = 0;
        canvas_left = canvas_top = 0;
        copy_name(filename, "PAINT.PCX");
        refresh_title();
        set_status("New image");
    } else if (action == 2) begin_dialog(DIALOG_OPEN);
    else if (action == 3) (void)host_api->window_close();
    else if (action == 4) {
        if (load_pcx(pending_path)) {
            copy_name(filename, pending_path);
            file_owned = 1;
            canvas_left = canvas_top = 0;
            refresh_title();
        } else set_status("Unable to open PCX; current image preserved");
    }
}

static void paint_action(int action)
{
    refresh_title();
    paint_menu = 0;
    pending_action = action;
    if (dirty) {
        (void)host_api->confirm_dialog(BUDO_CONFIRM_SAVE, "Unsaved changes",
                                       "Save your image before continuing?");
    } else paint_continue();
}

static int paint_save(void)
{
    if (!file_owned) {
        begin_dialog(DIALOG_SAVE_AS);
        return 0;
    }
    int saved = save_pcx(filename);
    if (saved) refresh_title();
    set_status(saved ? "Image saved" : "Save failed; original file preserved");
    return saved;
}

static int paint_file_selected(int mode, const char *path)
{
    int ok = mode == BUDO_FILE_OPEN ? load_pcx(path) : save_pcx(path);
    if (!ok) return 0;
    copy_name(filename, path);
    file_owned = 1;
    if (mode == BUDO_FILE_OPEN) canvas_left = canvas_top = 0;
    refresh_title();
    set_status(mode == BUDO_FILE_OPEN ? "Image loaded" : "Image saved");
    if (pending_action) paint_continue();
    return 1;
}

static int paint_request_close(void)
{
    refresh_title();
    if (!dirty) return 1;
    paint_action(3);
    return 0;
}

static void paint_confirm_result(int kind, int response)
{
    (void)kind;
    if (response == BUDO_RESPONSE_CANCEL) {
        pending_action = 0;
        return;
    }
    if (response == BUDO_RESPONSE_DISCARD) {
        if (pending_action == 3) {
            new_canvas();
            file_owned = 0;
            copy_name(filename, "PAINT.PCX");
            refresh_title();
        }
        paint_continue();
    } else if (paint_save()) paint_continue();
}

static int layout(int *cx, int *cy, int *cw, int *ch,
                  int *sx, int *sy, int *vw, int *vh,
                  int *palette_y, int *status_y)
{
    int work_x, work_y, aw, ah;
    if (!host_api->window_get_client_rect(cx, cy, cw, ch)) return 0;
    work_x = *cx + 94;
    work_y = *cy + 28;
    *palette_y = *cy + *ch - 43;
    *status_y = *cy + *ch - 14;
    aw = *cx + *cw - BUDO_SCROLL_WIDTH - 6 - work_x;
    ah = *palette_y - BUDO_SCROLL_WIDTH - 18 - work_y;
    *vw = min_i(CANVAS_W * zoom, max_i(1, aw));
    *vh = min_i(CANVAS_H * zoom, max_i(1, ah));
    *sx = work_x;
    *sy = work_y;
    return 1;
}

static int to_canvas(int mx, int my, int *px, int *py)
{
    int cx, cy, cw, ch, sx, sy, vw, vh, py0, st;
    if (!layout(&cx, &cy, &cw, &ch, &sx, &sy, &vw, &vh, &py0, &st))
        return 0;
    if (!host_api->point_in_rect(mx, my, sx, sy, vw, vh)) return 0;
    *px = (mx - sx) / zoom + canvas_left;
    *py = (my - sy) / zoom + canvas_top;
    return 1;
}

static void draw_canvas(int sx, int sy, int vw, int vh)
{
    int y;
    host_api->draw_sunken_panel(sx - 2, sy - 2, vw + 4, vh + 4,
        host_api->get_system_color(BUDO_SYS_COLOR_HIGHLIGHT));
    for (y = 0; y < vh; ++y) {
        int iy = y / zoom + canvas_top;
        if (iy >= CANVAS_H) break;
        int x = 0;
        while (x < vw) {
            int ix = x / zoom + canvas_left;
            if (ix >= CANVAS_W) break;
            unsigned char c = canvas[iy * CANVAS_W + ix];
            int run = min_i(zoom - x % zoom, vw - x);
            while (x + run < vw && (x + run) / zoom + canvas_left < CANVAS_W &&
                canvas[iy * CANVAS_W + (x + run) / zoom + canvas_left] == c)
                run += min_i(zoom, vw - x - run);
            host_api->fill_rect_rgb(sx + x, sy + y, run, 1, canvas_rgb(c));
            x += run;
        }
    }
    if (pixel_grid && zoom >= 8) {
        /* A subtle line blended with each pixel, never stored in the image. */
        for (int y0 = 0; y0 < vh; y0 += zoom)
            for (int x0 = 0; x0 < vw; x0 += zoom) {
                unsigned int c = canvas_rgb(pixel_at(canvas_left + x0 / zoom, canvas_top + y0 / zoom));
                unsigned int r = (((c >> 16) & 255) * 3 + 128) / 4;
                unsigned int g = (((c >> 8) & 255) * 3 + 128) / 4;
                unsigned int b = ((c & 255) * 3 + 128) / 4;
                unsigned int grid = (r << 16) | (g << 8) | b;
                host_api->fill_rect_rgb(sx + x0, sy + y0, min_i(zoom, vw - x0), 1, grid);
                host_api->fill_rect_rgb(sx + x0, sy + y0, 1, min_i(zoom, vh - y0), grid);
            }
    }
}

static void preview_pixel(int sx, int sy, int vw, int vh,
                          int x, int y, unsigned char c)
{
    x = (x - canvas_left) * zoom;
    y = (y - canvas_top) * zoom;
    if (x >= 0 && x < vw && y >= 0 && y < vh)
        host_api->fill_rect_rgb(sx + x, sy + y, min_i(zoom, vw - x), min_i(zoom, vh - y), canvas_rgb(c));
}

static void preview_line(int sx, int sy, int vw, int vh,
                         int x0, int y0, int x1, int y1, unsigned char c)
{
    int dx = abs_i(x1 - x0), stepx = x0 < x1 ? 1 : -1;
    int dy = -abs_i(y1 - y0), stepy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        preview_pixel(sx, sy, vw, vh, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = err * 2;
            if (e2 >= dy) { err += dy; x0 += stepx; }
            if (e2 <= dx) { err += dx; y0 += stepy; }
        }
    }
}

static void draw_preview(int sx, int sy, int vw, int vh, unsigned char c)
{
    int l, r, t, b;
    if (!drawing) return;
    l = min_i(start_x, preview_x); r = max_i(start_x, preview_x);
    t = min_i(start_y, preview_y); b = max_i(start_y, preview_y);
    if (tool == TOOL_LINE)
        preview_line(sx, sy, vw, vh, start_x, start_y, preview_x, preview_y, c);
    else if (tool == TOOL_RECT || tool == TOOL_FRECT) {
        preview_line(sx, sy, vw, vh, l, t, r, t, c);
        preview_line(sx, sy, vw, vh, r, t, r, b, c);
        preview_line(sx, sy, vw, vh, r, b, l, b, c);
        preview_line(sx, sy, vw, vh, l, b, l, t, c);
    } else if (tool == TOOL_OVAL || tool == TOOL_FOVAL) {
        int rx = (r - l) / 2, ry = (b - t) / 2;
        int cx = l + rx, cy = t + ry, y;
        if (rx <= 0 || ry <= 0) return;
        for (y = -ry; y <= ry; ++y) {
            long yy = (long)y * y, ry2 = (long)ry * ry, rx2 = (long)rx * rx;
            int x = 0;
            while (x < rx &&
                   (long)(x + 1) * (x + 1) * ry2 + yy * rx2 <= rx2 * ry2)
                ++x;
            preview_pixel(sx, sy, vw, vh, cx - x, cy + y, c);
            preview_pixel(sx, sy, vw, vh, cx + x, cy + y, c);
        }
    }
}

static void paint_scroll_layout(int sx, int sy, int vw, int vh)
{
    canvas_hscroll.x = sx;
    canvas_hscroll.y = sy + vh + 2;
    canvas_hscroll.length = vw;
    canvas_hscroll.horizontal = 1;
    canvas_hscroll.total = CANVAS_W;
    canvas_hscroll.page = max_i(1, vw / zoom);
    canvas_hscroll.position = canvas_left;
    canvas_vscroll.x = sx + vw + 2;
    canvas_vscroll.y = sy;
    canvas_vscroll.length = vh;
    canvas_vscroll.total = CANVAS_H;
    canvas_vscroll.page = max_i(1, vh / zoom);
    canvas_vscroll.position = canvas_top;
    budo_scroll_clamp(&canvas_hscroll);
    budo_scroll_clamp(&canvas_vscroll);
    canvas_left = canvas_hscroll.position;
    canvas_top = canvas_vscroll.position;
}

static int paint_scroll_pointer(int x, int y, int event)
{
    int cx,cy,cw,ch,sx,sy,vw,vh,py,st;
    if (!layout(&cx,&cy,&cw,&ch,&sx,&sy,&vw,&vh,&py,&st)) return 0;
    paint_scroll_layout(sx, sy, vw, vh);
    int handled = budo_scroll_pointer_host(host_api, &canvas_hscroll, x, y, event);
    handled |= budo_scroll_pointer_host(host_api, &canvas_vscroll, x, y, event);
    canvas_left = canvas_hscroll.position;
    canvas_top = canvas_vscroll.position;
    return handled;
}

static void paint_set_zoom(int value)
{
    int cx,cy,cw,ch,sx,sy,vw,vh,py,st;
    int center_x = canvas_left, center_y = canvas_top;
    if (layout(&cx,&cy,&cw,&ch,&sx,&sy,&vw,&vh,&py,&st)) {
        center_x += vw / zoom / 2;
        center_y += vh / zoom / 2;
    }
    zoom = value;
    if (layout(&cx,&cy,&cw,&ch,&sx,&sy,&vw,&vh,&py,&st)) {
        canvas_left = center_x - vw / zoom / 2;
        canvas_top = center_y - vh / zoom / 2;
        paint_scroll_layout(sx,sy,vw,vh);
    }
}

static int paint_resize(int width, int height, int scale)
{
    if (width < 1 || height < 1 || width > MAX_DIMENSION || height > MAX_DIMENSION) {
        set_status("Size must be 1 to 2048 pixels");
        return 0;
    }
    unsigned char *resized = host_api->memory_alloc((unsigned int)(width * height));
    if (!resized) { set_status("Unable to allocate resized image"); return 0; }
    bytes_fill(resized, bg, (unsigned int)(width * height));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            int source_x = scale ? x * CANVAS_W / width : x;
            int source_y = scale ? y * CANVAS_H / height : y;
            if (source_x < CANVAS_W && source_y < CANVAS_H)
                resized[y * width + x] = canvas[source_y * CANVAS_W + source_x];
        }
    save_undo();
    CANVAS_W = width;
    CANVAS_H = height;
    bytes_copy(canvas, resized, PIXELS);
    host_api->memory_free(resized);
    canvas_left = canvas_top = 0;
    refresh_title();
    return 1;
}

static void paint_resize_begin(int scale)
{
    resize_dialog = scale ? 2 : 1;
    resize_field = 0;
    resize_selected = 1;
    snprintf(resize_width, sizeof(resize_width), "%d", CANVAS_W);
    snprintf(resize_height, sizeof(resize_height), "%d", CANVAS_H);
}

static void paint_resize_accept(void)
{
    int width = 0, height = 0;
    (void)sscanf(resize_width, "%d", &width);
    (void)sscanf(resize_height, "%d", &height);
    if (paint_resize(width, height, resize_dialog == 2)) resize_dialog = 0;
}

static void paint_resize_draw(int cx, int cy, int cw, int ch)
{
    int x = cx + (cw - 300) / 2, y = cy + (ch - 144) / 2;
    if (host_api->abi_minor >= 14 && host_api->set_modal_focus) host_api->set_modal_focus(1);
    host_api->draw_sunken_panel(x,y,300,144,host_api->get_system_color(BUDO_SYS_COLOR_FACE));
    if (host_api->abi_minor >= 14) host_api->pointer_region(x,y,300,144,BUDO_CURSOR_ARROW,NULL);
    host_api->draw_text(x+10,y+10,resize_dialog == 2 ? "Image Size (nearest neighbor)" : "Canvas Size (top-left anchor)",1,46);
    for (int i = 0; i < 2; ++i) {
        host_api->draw_text(x+12,y+37+i*26,i ? "Height" : "Width",1,8);
        host_api->draw_sunken_panel(x+76,y+30+i*26,100,22,resize_field == i && resize_selected ? 6 : 4);
        if (host_api->abi_minor >= 14 && host_api->pointer_region)
            host_api->pointer_region(x+76,y+30+i*26,100,22,BUDO_CURSOR_TEXT,i ? "Height" : "Width");
        host_api->draw_text(x+80,y+37+i*26,i ? resize_height : resize_width,resize_field == i && resize_selected ? 4 : 1,8);
    }
    host_api->draw_text(x+12,y+88,"1..2048 pixels",1,30);
    host_api->draw_standard_button(x+116,y+112,80,20,"OK",0);
    host_api->draw_standard_button(x+204,y+112,80,20,"Cancel",0);
}

static void paint_draw(void)
{
    int cx, cy, cw, ch, sx, sy, vw, vh, pal_y, status_y, i;
    if (!resize_dialog && host_api->abi_minor >= 14 && host_api->set_modal_focus) host_api->set_modal_focus(0);
    static const char *file_items[5] = {"New   Ctrl+N", "Open... Ctrl+O",
                                      "Save  Ctrl+S", "Save As...", "Close"};
    static const char *edit_items[2] = {"Undo  Ctrl+Z", "Clear"};
    BudoMenuItem view_items[] = {
        {"100%", 1, zoom == 1}, {"200%", 1, zoom == 2},
        {"400%", 1, zoom == 4}, {"800%", 1, zoom == 8},
        {"1600%", 1, zoom == 16}, {"3200%", 1, zoom == 32},
        {"Pixel grid", 1, pixel_grid}
    };
    static const char *image_items[] = {"Image Size...", "Canvas Size..."};
    int x;
    if (!layout(&cx,&cy,&cw,&ch,&sx,&sy,&vw,&vh,&pal_y,&status_y)) return;
    host_api->fill_rect(cx, cy, cw, ch,
        host_api->get_system_color(BUDO_SYS_COLOR_FACE));
    budo_menu_bar_item(host_api, cx + 4, cy + 3, 36, "File", paint_menu == 1);
    budo_menu_bar_item(host_api, cx + 44, cy + 3, 36, "Edit", paint_menu == 2);
    budo_menu_bar_item(host_api, cx + 84, cy + 3, 36, "View", paint_menu == 3);
    budo_menu_bar_item(host_api, cx + 124, cy + 3, 48, "Image", paint_menu == 4);
    paint_scroll_layout(sx, sy, vw, vh);
    for (i = 0; i < TOOL_COUNT; ++i)
        host_api->draw_standard_button(cx + 5 + (i & 1) * 42,
            cy + 30 + (i >> 1) * 24, 38, 20, tool_labels[i], i == tool);
    draw_canvas(sx, sy, vw, vh);
    if (host_api->abi_minor >= 12 && host_api->pointer_region)
        host_api->pointer_region(sx, sy, vw, vh, BUDO_CURSOR_CROSSHAIR, NULL);
    budo_scroll_draw(host_api, &canvas_hscroll);
    budo_scroll_draw(host_api, &canvas_vscroll);
    draw_preview(sx, sy, vw, vh, draw_button == 2 ? bg : fg);
    host_api->draw_text(cx + 6, pal_y - 11, "Colors",
        host_api->get_system_color(BUDO_SYS_COLOR_TEXT), 6);
    for (i = 0; i < 16; ++i) {
        x = cx + 6 + i * 22;
        int index = palette_page * 16 + i;
        host_api->fill_rect_rgb(x, pal_y, 18, 16, canvas_rgb((unsigned char)index));
        host_api->draw_rect(x - 1, pal_y - 1, 20, 18,
            host_api->get_system_color(index == fg ?
                BUDO_SYS_COLOR_ACCENT : BUDO_SYS_COLOR_SHADOW));
        if (index == bg)
            host_api->draw_rect(x + 2, pal_y + 2, 14, 12,
                host_api->get_system_color(BUDO_SYS_COLOR_HIGHLIGHT));
    }
    host_api->draw_text(cx + 366, pal_y + 4, "FG",
        host_api->get_system_color(BUDO_SYS_COLOR_TEXT), 2);
    host_api->fill_rect_rgb(cx + 386, pal_y + 1, 14, 14, canvas_rgb(fg));
    host_api->draw_text(cx + 406, pal_y + 4, "BG",
        host_api->get_system_color(BUDO_SYS_COLOR_TEXT), 2);
    host_api->fill_rect_rgb(cx + 426, pal_y + 1, 14, 14, canvas_rgb(bg));
    host_api->draw_standard_button(cx + 448, pal_y, 18, 16, "<", 0);
    host_api->draw_standard_button(cx + 470, pal_y, 18, 16, ">", 0);
    host_api->fill_rect(cx, status_y - 1, cw, 1,
        host_api->get_system_color(BUDO_SYS_COLOR_SHADOW));
    char display_status[128];
    snprintf(display_status, sizeof(display_status), "%dx%d  %d%%  %s", CANVAS_W, CANVAS_H, zoom * 100, status_text);
    host_api->draw_text(cx + 6, status_y + 2, display_status,
        host_api->get_system_color(BUDO_SYS_COLOR_TEXT), (cw - 12) / 6);
    if (paint_menu == 1) budo_menu_draw(host_api, cx + 4, cy + 22, 156, file_items, 5);
    else if (paint_menu == 2) budo_menu_draw(host_api, cx + 44, cy + 22, 156, edit_items, 2);
    else if (paint_menu == 3) budo_menu_items_draw(host_api, cx + 84, cy + 22, 156, view_items, 7);
    else if (paint_menu == 4) budo_menu_draw(host_api, cx + 124, cy + 22, 156, image_items, 2);
    if (resize_dialog) paint_resize_draw(cx,cy,cw,ch);
}

static void commit_shape(unsigned char c)
{
    if (tool == TOOL_LINE) line(start_x,start_y,preview_x,preview_y,c);
    else if (tool == TOOL_RECT) rect_shape(start_x,start_y,preview_x,preview_y,c,0);
    else if (tool == TOOL_FRECT) rect_shape(start_x,start_y,preview_x,preview_y,c,1);
    else if (tool == TOOL_OVAL) oval_shape(start_x,start_y,preview_x,preview_y,c,0);
    else if (tool == TOOL_FOVAL) oval_shape(start_x,start_y,preview_x,preview_y,c,1);
}

static int paint_mouse_down(int x, int y, int buttons)
{
    int cx,cy,cw,ch,sx,sy,vw,vh,pal_y,status_y,px,py,i;
    int button = (buttons & 2) ? 2 : 1;
    unsigned char c = button == 2 ? bg : fg;
    if (!layout(&cx,&cy,&cw,&ch,&sx,&sy,&vw,&vh,&pal_y,&status_y)) return 0;
    (void)status_y;
    if (resize_dialog) {
        int dx = cx + (cw - 300) / 2, dy = cy + (ch - 144) / 2;
        for (int i = 0; i < 2; ++i)
            if (host_api->point_in_rect(x,y,dx+76,dy+30+i*26,100,22)) { resize_field = i; resize_selected = 1; }
        if (host_api->point_in_rect(x,y,dx+116,dy+112,80,20)) paint_resize_accept();
        if (host_api->point_in_rect(x,y,dx+204,dy+112,80,20)) resize_dialog = 0;
        return 1;
    }
    if (paint_menu) {
        int item = budo_menu_hit(x, y, cx + 4 + (paint_menu - 1) * 40, cy + 22,
                                156, paint_menu == 1 ? 5 : paint_menu == 3 ? 7 : 2);
        int menu = paint_menu;
        paint_menu = 0;
        if (item >= 0 && menu == 1) {
            if (item == 0) paint_action(1);
            else if (item == 1) paint_action(2);
            else if (item == 2) (void)paint_save();
            else if (item == 3) begin_dialog(DIALOG_SAVE_AS);
            else paint_action(3);
        } else if (item >= 0 && menu == 3) {
            if (item < 6) paint_set_zoom(1 << item);
            else pixel_grid = !pixel_grid;
        } else if (item >= 0 && menu == 4) {
            paint_resize_begin(item == 0);
        } else if (item >= 0) {
            if (item == 0) do_undo();
            else { clear_canvas(bg); refresh_title(); set_status("Canvas cleared"); }
        }
        return 1;
    }
    if (host_api->point_in_rect(x,y,cx+4,cy+3,36,18)) { paint_menu=1; return 1; }
    if (host_api->point_in_rect(x,y,cx+44,cy+3,36,18)) { paint_menu=2; return 1; }
    if (host_api->point_in_rect(x,y,cx+84,cy+3,36,18)) { paint_menu=3; return 1; }
    if (host_api->point_in_rect(x,y,cx+124,cy+3,48,18)) { paint_menu=4; return 1; }
    if (paint_scroll_pointer(x, y, BUDO_POINTER_DOWN)) return 1;
    for (i = 0; i < TOOL_COUNT; ++i) {
        int tx = cx + 5 + (i & 1) * 42;
        int ty = cy + 30 + (i >> 1) * 24;
        if (host_api->point_in_rect(x,y,tx,ty,38,20)) {
            tool = i; set_status(tool_labels[i]); return 1;
        }
    }
    for (i = 0; i < 16; ++i) {
        int px_color = cx + 6 + i * 22;
        if (host_api->point_in_rect(x,y,px_color,pal_y,18,16)) {
            unsigned char index = (unsigned char)(palette_page * 16 + i);
            if (button == 2) bg = index; else fg = index;
            set_status(button == 2 ? "Background color selected" :
                                      "Foreground color selected");
            return 1;
        }
    }
    if (host_api->point_in_rect(x,y,cx+448,pal_y,18,16) ||
        host_api->point_in_rect(x,y,cx+470,pal_y,18,16)) {
        palette_page = (palette_page + (x < cx + 470 ? 15 : 1)) % 16;
        set_status("Palette page changed ([ / ])");
        return 1;
    }
    if (!to_canvas(x,y,&px,&py)) return 0;
    if (tool == TOOL_PICK) {
        if (button == 2) bg = pixel_at(px,py); else fg = pixel_at(px,py);
        set_status("Color picked"); return 1;
    }
    save_undo();
    drawing = 1; draw_button = button;
    start_x = last_x = preview_x = px;
    start_y = last_y = preview_y = py;
    if (tool == TOOL_PENCIL) { pixel(px,py,c); dirty = 1; }
    else if (tool == TOOL_BRUSH) { brush(px,py,2,c); dirty = 1; }
    else if (tool == TOOL_ERASER) { brush(px,py,4,bg); dirty = 1; }
    else if (tool == TOOL_FILL) { flood(px,py,c); drawing = 0; dirty = 1; }
    if (dirty) refresh_title();
    return 1;
}

static int paint_mouse_move(int x, int y, int buttons)
{
    if (paint_menu || resize_dialog) return 1;
    if (paint_scroll_pointer(x, y, BUDO_POINTER_MOVE)) return 1;
    int px,py;
    unsigned char c;
    if (!drawing || (buttons & draw_button) == 0) return 0;
    if (!to_canvas(x,y,&px,&py)) return 1;
    c = draw_button == 2 ? bg : fg;
    if (tool == TOOL_PENCIL) {
        line(last_x,last_y,px,py,c); last_x=px; last_y=py; dirty=1;
    } else if (tool == TOOL_BRUSH) {
        line(last_x,last_y,px,py,c); brush(px,py,2,c);
        last_x=px; last_y=py; dirty=1;
    } else if (tool == TOOL_ERASER) {
        line(last_x,last_y,px,py,bg); brush(px,py,4,bg);
        last_x=px; last_y=py; dirty=1;
    } else { preview_x=px; preview_y=py; }
    return 1;
}

static int paint_mouse_up(int x, int y, int buttons)
{
    if (paint_scroll_pointer(x, y, BUDO_POINTER_UP)) return 1;
    int px,py;
    unsigned char c;
    (void)buttons;
    if (!drawing) return 0;
    if (to_canvas(x,y,&px,&py)) { preview_x=px; preview_y=py; }
    c = draw_button == 2 ? bg : fg;
    if (tool == TOOL_LINE || tool == TOOL_RECT || tool == TOOL_FRECT ||
        tool == TOOL_OVAL || tool == TOOL_FOVAL) {
        commit_shape(c); dirty=1; refresh_title();
    }
    drawing=0; set_status("Ready"); return 1;
}

static int paint_key(int key)
{
    if (resize_dialog) {
        char *value = resize_field ? resize_height : resize_width;
        size_t length = strlen(value);
        if (key == 27) resize_dialog = 0;
        else if (key == 13) paint_resize_accept();
        else if (key == 9) { resize_field = !resize_field; resize_selected = 1; }
        else if (key == 8 || (key >= '0' && key <= '9')) {
            if (resize_selected) { value[0] = 0; length = 0; resize_selected = 0; }
            if (key == 8 && length) value[length - 1] = 0;
            else if (key != 8 && length < 4) { value[length] = (char)key; value[length + 1] = 0; }
        }
        return 1;
    }
    if (key == '+' || key == '=') { paint_set_zoom(min_i(32, zoom * 2)); return 1; }
    if (key == '-') { paint_set_zoom(max_i(1, zoom / 2)); return 1; }
    if (key == '[' || key == ']') {
        palette_page = (palette_page + (key == '[' ? 15 : 1)) % 16;
        set_status("Palette page changed ([ / ])");
        return 1;
    }
    if (key == 26) { do_undo(); return 1; }
    if (key == 14) { paint_action(1); return 1; }
    if (key == 15) { paint_action(2); return 1; }
    if (key == 19) { (void)paint_save(); return 1; }
    if (key == 0x100 + 73 || key == 0x100 + 81 || key == 0x100 + 201 || key == 0x100 + 202) {
        canvas_top += (key == 0x100 + 73 || key == 0x100 + 201) ? -32 : 32;
        return 1;
    }
    if (key == 27) {
        if (paint_menu) { paint_menu = 0; return 1; }
        if (drawing) {
            drawing=0;
            if (undo_valid) { bytes_copy(canvas,undo_canvas,PIXELS); undo_valid=0; }
            set_status("Drawing cancelled"); return 1;
        }
        return host_api->window_close();
    }
    if (key >= '1' && key <= '9') {
        tool = key - '1'; set_status(tool_labels[tool]); return 1;
    }
    if (key == '0') { tool=TOOL_PICK; set_status(tool_labels[tool]); return 1; }
    return 0;
}

static int paint_open(void)
{
    if (!ensure_buffers()) {
        return 0;
    }

    canvas_hscroll.held = canvas_hscroll.armed = canvas_hscroll.dragging = 0;
    canvas_vscroll.held = canvas_vscroll.armed = canvas_vscroll.dragging = 0;
    drawing=0; dialog_mode=DIALOG_NONE; set_status("Ready");
    if (!host_api->window_create(WIN_X,WIN_Y,WIN_W,WIN_H,
        dirty ? "Paint *" : "Paint", BUDO_WINDOW_DEFAULT_BUTTONS)) {
        return 0;
    }
    if (!file_owned && !dirty && !undo_valid) {
        new_dimensions();
        saved_fingerprint = paint_content_hash();
    }
    if (host_api->window_set_min_size != 0) {
        (void)host_api->window_set_min_size(500, 330);
    }
    refresh_title();
    return 1;
}

static int paint_open_file(const char *path)
{
    if (path == 0 || !ensure_buffers()) return 0;
    if (dirty) {
        copy_name(pending_path, path);
        paint_action(4);
        return paint_open();
    }
    if (!load_pcx(path)) return 0;
    copy_name(filename, path);
    file_owned = 1;
    canvas_left = canvas_top = 0;
    set_status("Image loaded");
    return paint_open();
}

static void paint_draw_icon(int x, int y)
{
    host_api->fill_rect(x+3,y+2,18,13,
        host_api->get_system_color(BUDO_SYS_COLOR_HIGHLIGHT));
    host_api->draw_rect(x+2,y+1,20,15,
        host_api->get_system_color(BUDO_SYS_COLOR_SHADOW));
    host_api->fill_rect(x+5,y+4,4,4,7);
    host_api->fill_rect(x+10,y+4,4,4,9);
    host_api->fill_rect(x+15,y+4,4,4,10);
    host_api->fill_rect(x+8,y+10,10,2,1);
    host_api->fill_rect(x+17,y+8,2,5,1);
}

int bwa_entry(const BwaHostApi *host, BwaAppDefinition *app)
{
    if (host==0 || app==0 || host->abi_major!=BWA_ABI_MAJOR ||
        host->abi_minor<10 || host->window_create==0 ||
        host->window_get_client_rect==0 || host->window_close==0 ||
        host->fill_rect==0 || host->draw_rect==0 || host->draw_text==0 ||
        host->draw_standard_button==0 || host->draw_sunken_panel==0 ||
        host->point_in_rect==0 || host->get_system_color==0 ||
        host->window_set_min_size==0 || host->window_set_title==0 ||
        host->file_read_all==0 || host->file_write_all==0 ||
        host->memory_alloc==0 || host->memory_free==0 || host->fill_rect_rgb==0 ||
        host->file_dialog==0 || host->confirm_dialog==0) return 0;

    host_api=host;
    canvas=0;
    undo_canvas=0;
    file_buffer=0;
    fill_queue=0;
    undo_valid=0;
    dirty=0;
    app->runtime_id=0;
    app->app_id="paint";
    app->name="Paint";
    app->flags=BWA_FLAG_SINGLETON;
    app->callbacks.open=paint_open;
    app->callbacks.draw=paint_draw;
    app->callbacks.mouse_down=paint_mouse_down;
    app->callbacks.key=paint_key;
    app->request_close = paint_request_close;
    app->file_selected = paint_file_selected;
    app->confirm_result = paint_confirm_result;
    app->callbacks.close=0;
    app->callbacks.draw_icon=paint_draw_icon;
    app->callbacks.open_file=paint_open_file;
    app->callbacks.mouse_move=paint_mouse_move;
    app->callbacks.mouse_up=paint_mouse_up;
    return 1;
}
