#include "../budo/BUDOWIN/appsrc/paint.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static unsigned char disk[FILE_BUFFER_SIZE];
static unsigned int disk_size;
static uint32_t drawn[MAX_PIXELS];
static int surface_w, surface_h;
static int client_w = 592, client_h = 404;
static void *allocate(unsigned int count) { return malloc(count); }
static int read_file(const char *path, unsigned char *buffer,
                     unsigned int capacity, unsigned int *size) {
    (void)path;
    if (capacity < disk_size) return 0;
    memcpy(buffer, disk, disk_size);
    *size = disk_size;
    return 1;
}
static int write_file(const char *path, const unsigned char *buffer, unsigned int size) {
    (void)path;
    assert(size <= sizeof(disk));
    memcpy(disk, buffer, size);
    disk_size = size;
    return 1;
}
static void draw_rgb(int x, int y, int w, int h, unsigned int rgb) {
    int width = surface_w ? surface_w : CANVAS_W;
    int height = surface_h ? surface_h : CANVAS_H;
    assert(x >= 0 && y >= 0 && x + w <= width && y + h <= height);
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px) drawn[py * width + px] = rgb;
}
static void panel(int x, int y, int w, int h, unsigned char c) {
    (void)x; (void)y; (void)w; (void)h; (void)c;
}
static unsigned char system_color(int role) { (void)role; return 4; }
static int client_rect(int *x, int *y, int *w, int *h) {
    *x = 0; *y = 0; *w = client_w; *h = client_h;
    return 1;
}
static int point_inside(int x, int y, int rx, int ry, int w, int h) {
    return x >= rx && y >= ry && x < rx + w && y < ry + h;
}
static void test_ovals(void) {
    CANVAS_W = CANVAS_H = 80;
    zoom = 1;
    canvas_left = canvas_top = 0;
    surface_w = surface_h = 80;
    for (int height = 0; height <= 65; ++height) {
        for (int width = 0; width <= 65; ++width) {
            memset(canvas, 0, PIXELS);
            oval_shape(5, 5, 5 + width, 5 + height, 1, 0);
            /* Every contour pixel stays inside the inclusive drag bounds,
             * and reversing the drag preserves its exact raster. */
            memcpy(undo_canvas, canvas, PIXELS);
            memset(canvas, 0, PIXELS);
            oval_shape(5 + width, 5 + height, 5, 5, 1, 0);
            assert(memcmp(canvas, undo_canvas, PIXELS) == 0);
            int edges[4] = {0};
            for (int y = 0; y < 80; ++y) {
                for (int x = 0; x < 80; ++x) {
                    if (!canvas[y * 80 + x]) continue;
                    assert(x >= 5 && x <= 5 + width && y >= 5 && y <= 5 + height);
                    edges[0] |= x == 5;
                    edges[1] |= x == 5 + width;
                    edges[2] |= y == 5;
                    edges[3] |= y == 5 + height;
                    assert(canvas[y * 80 + 10 + width - x] == 1);
                    assert(canvas[(10 + height - y) * 80 + x] == 1);
                }
            }
            for (int i = 0; i < 4; ++i) {
                if (!edges[i]) fprintf(stderr, "ellipse %d x %d missing edge %d\n", width, height, i);
                assert(edges[i]);
            }
            if (width >= 6 && height >= 6) {
                flood(0, 0, 2);
                assert(canvas[(5 + height / 2) * 80 + 5 + width / 2] == 0);
            }
            memcpy(canvas, undo_canvas, PIXELS);
            /* Preview uses the same pixels and never mutates the image. */
            for (int i = 0; i < PIXELS; ++i) drawn[i] = canvas_rgb(0);
            drawing = 1;
            tool = TOOL_OVAL;
            start_x = start_y = 5;
            preview_x = 5 + width;
            preview_y = 5 + height;
            draw_preview(0, 0, 80, 80, 1);
            for (int i = 0; i < PIXELS; ++i) assert(drawn[i] == canvas_rgb(canvas[i]));
            assert(memcmp(canvas, undo_canvas, PIXELS) == 0);
            memset(canvas, 0, PIXELS);
            oval_shape(5, 5, 5 + width, 5 + height, 1, 1);
            for (int i = 0; i < PIXELS; ++i) assert(!undo_canvas[i] || canvas[i]);
        }
    }
    drawing = 0;
    /* Exercise decision arithmetic at the maximum supported dimensions. */
    CANVAS_W = CANVAS_H = MAX_DIMENSION;
    memset(canvas, 0, PIXELS);
    oval_shape(0, 0, MAX_DIMENSION - 1, MAX_DIMENSION - 1, 1, 0);
    flood(0, 0, 2);
    assert(pixel_at(MAX_DIMENSION / 2, MAX_DIMENSION / 2) == 0);
}

static void test_palette(void) {
    int cx,cy,cw,ch,sx,sy,vw,vh,py,st;
    assert(layout(&cx,&cy,&cw,&ch,&sx,&sy,&vw,&vh,&py,&st));
    assert(palette_cols == 64 && palette_rows == 4 && palette_top == 0);
    assert(!budo_scroll_limit(&palette_scroll));
    uint64_t hash = paint_content_hash();
    for (int index = 0; index < 256; ++index) {
        int x = cx + 6 + index % 64 * PALETTE_STEP + 2;
        int y = py + index / 64 * PALETTE_STEP + 2;
        assert(paint_mouse_down(x, y, 1) && fg == index);
        assert(paint_mouse_down(x, y, 2) && bg == index);
    }
    assert(paint_content_hash() == hash);
    for (int height = 278; height <= 404; height += 126) {
        client_w = 488;
        client_h = height;
        assert(layout(&cx,&cy,&cw,&ch,&sx,&sy,&vw,&vh,&py,&st));
        assert(budo_scroll_limit(&palette_scroll));
        assert(palette_scroll.x + BUDO_SCROLL_WIDTH <= cw);
        assert(palette_scroll.y + palette_scroll.length < st);
        assert(sy + vh + BUDO_SCROLL_WIDTH + 2 < py - 11);
        if (height == 278) assert(palette_rows == 1);
        palette_top = 0;
        int rows_seen = 0;
        while (1) {
            assert(layout(&cx,&cy,&cw,&ch,&sx,&sy,&vw,&vh,&py,&st));
            for (int i = 0; i < palette_rows * palette_cols; ++i) {
                int index = palette_top * palette_cols + i;
                if (index >= 256) break;
                assert(paint_mouse_down(cx + 8 + i % palette_cols * PALETTE_STEP,
                                        py + 2 + i / palette_cols * PALETTE_STEP, 1));
                assert(fg == index);
            }
            if (palette_top == budo_scroll_limit(&palette_scroll)) break;
            int old_top = palette_top;
            assert(paint_scroll_pointer(palette_scroll.x + 4,
                palette_scroll.y + palette_scroll.length - 4, BUDO_POINTER_DOWN));
            assert(paint_scroll_pointer(0, 0, BUDO_POINTER_UP));
            assert(palette_top == old_top + 1);
            ++rows_seen;
            assert(rows_seen < 256);
        }
        assert(fg == 255);
        assert(paint_key('[') && palette_top == budo_scroll_limit(&palette_scroll) - 1);
        assert(paint_key(']') && palette_top == budo_scroll_limit(&palette_scroll));
    }
    client_w = 592;
    client_h = 404;
    assert(layout(&cx,&cy,&cw,&ch,&sx,&sy,&vw,&vh,&py,&st));
    assert(palette_top == 0 && !budo_scroll_limit(&palette_scroll));
}

static unsigned long long tool_clock = 1000;
static unsigned long long tool_time(void) { return tool_clock; }
static void test_tools(BwaHostApi *api)
{
    image_width = image_height = 64;
    bytes_fill(canvas, 4, PIXELS);
    stroke_width = 3;
    stroke_round = 1;
    stroke_stamp(20, 20, 1);
    assert(pixel_at(19, 19) == 4 && pixel_at(20, 19) == 1);
    bytes_fill(canvas, 4, PIXELS);
    stroke_width = 8;
    stroke_round = 0;
    stroke_stamp(20, 20, 1);
    int count = 0;
    for (int i = 0; i < PIXELS; ++i) count += canvas[i] == 1;
    assert(count == 64 && pixel_at(16, 16) == 1 && pixel_at(23, 23) == 1 && pixel_at(24, 20) == 4);
    bytes_fill(canvas, 4, PIXELS);
    stroke_round = 1;
    stroke_stamp(20, 20, 1);
    assert(pixel_at(16, 16) == 4 && pixel_at(20, 16) == 1 && pixel_at(23, 20) == 1);
    bytes_fill(canvas, 4, PIXELS);
    stroke_line(5, 5, 40, 40, 1);
    for (int i = 5; i <= 40; ++i) assert(pixel_at(i, i) == 1 && pixel_at(i + 2, i) == 1);
    tool = TOOL_LINE;
    drawing = 1;
    start_x = start_y = 5;
    preview_x = preview_y = 40;
    surface_w = surface_h = 64;
    zoom = 1;
    canvas_left = canvas_top = 0;
    for (int i = 0; i < PIXELS; ++i) drawn[i] = canvas_rgb(4);
    draw_preview(0, 0, 64, 64, 1);
    for (int i = 0; i < PIXELS; ++i) assert(drawn[i] == canvas_rgb(canvas[i]));
    drawing = 0;
    api->abi_minor = 12;
    api->get_time_ms = tool_time;
    tool = TOOL_SPRAY;
    spray_width = 16;
    spray_density = 100;
    bytes_fill(canvas, 4, PIXELS);
    save_undo();
    spray(30, 30, 1, 1);
    unsigned char before[4096];
    memcpy(before, canvas, PIXELS);
    spray(30, 30, 1, 0);
    assert(!memcmp(before, canvas, PIXELS)); /* Repeated frames cannot increase spray speed. */
    tool_clock += 20;
    spray(30, 30, 1, 0);
    assert(memcmp(before, canvas, PIXELS)); /* Holding still continues spraying after the tick. */
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (pixel_at(x, y) == 1) {
                assert(x >= 22 && x < 38 && y >= 22 && y < 38);
                assert(stamp_inside(x - 22, y - 22, 16, 1));
            }
    do_undo();
    for (int i = 0; i < PIXELS; ++i) assert(canvas[i] == 4);
    width_focus = width_selected = 1;
    assert(paint_key('2') && paint_key('5') && paint_key('6') && paint_key(13));
    assert(spray_width == 256 && !width_focus);
    width_focus = width_selected = 1;
    paint_key('0'); paint_key(13);
    assert(spray_width == 1);
    api->get_time_ms = NULL;
    puts("PASS: stroke widths/shapes, preview parity, timed circular spray, undo and width editing");
}

int main(void) {
    BwaHostApi api = {0};
    api.memory_alloc = allocate;
    api.memory_free = free;
    api.file_read_all = read_file;
    api.file_write_all = write_file;
    api.fill_rect_rgb = draw_rgb;
    api.draw_sunken_panel = panel;
    api.get_system_color = system_color;
    host_api = &api;
    assert(ensure_buffers());
    for (int i = 0; i < 256; ++i) {
        canvas_palette[i * 3] = (unsigned char)i;
        canvas_palette[i * 3 + 1] = (unsigned char)(255 - i);
        canvas_palette[i * 3 + 2] = (unsigned char)(i ^ 85);
    }
    for (int i = 0; i < PIXELS; ++i) canvas[i] = (unsigned char)i;
    assert(save_pcx("256.pcx"));
    unsigned char expected_palette[768];
    memcpy(expected_palette, canvas_palette, sizeof(expected_palette));
    new_canvas();
    assert(load_pcx("256.pcx"));
    assert(memcmp(canvas_palette, expected_palette, 768) == 0);
    for (int i = 0; i < PIXELS; ++i) assert(canvas[i] == (unsigned char)i);
    draw_canvas(0, 0, CANVAS_W, CANVAS_H);
    for (int i = 0; i < PIXELS; ++i) {
        uint32_t index = (uint32_t)i & 255u;
        assert(drawn[i] == (index << 16 | (255u - index) << 8 | (index ^ 85u)));
    }
    assert(save_pcx("roundtrip.pcx"));
    assert(memcmp(disk + disk_size - 768, expected_palette, 768) == 0);
    do_undo(); /* Opening the image restores the preceding default canvas/palette. */
    assert(canvas[0] == 4 && canvas_palette[0] == (unsigned char)(budo_palette_rgb(0) >> 16));
    do_undo();
    assert(memcmp(canvas_palette, expected_palette, 768) == 0 && canvas[255] == 255);
    assert(!dirty); /* Undo returned exactly to the saved content. */

    /* Invalid RLE must leave both canvas and palette intact. */
    disk[128] = 0xc0;
    assert(!load_pcx("broken.pcx"));
    assert(memcmp(canvas_palette, expected_palette, 768) == 0 && canvas[255] == 255);
    /* Scrolling affects presentation only; absolute drawing coordinates
     * must not apply the viewport offset twice. */
    canvas_left = 10;
    canvas_top = 20;
    pixel(CANVAS_W - 1, CANVAS_H - 1, 123);
    assert(canvas[PIXELS - 1] == 123);
    assert(pixel_at(CANVAS_W - 1, CANVAS_H - 1) == 123);
    draw_canvas(0, 0, CANVAS_W - 10, CANVAS_H - 20);
    uint32_t index = canvas[20 * CANVAS_W + 10];
    assert(drawn[0] == (index << 16 | (255u - index) << 8 | (index ^ 85u)));
    canvas_left = canvas_top = 0;
    /* Preserve sprite dimensions, even scanline padding for odd widths,
     * and original indices/palette when opening and saving small assets. */
    assert(paint_resize(33, 17, 0));
    assert(CANVAS_W == 33 && CANVAS_H == 17);
    for (int i = 0; i < PIXELS; ++i) canvas[i] = (unsigned char)(i % 256);
    assert(save_pcx("sprite.pcx"));
    assert(disk[8] == 32 && disk[10] == 16 && disk[66] == 34);
    assert(paint_resize(64, 64, 0));
    assert(load_pcx("sprite.pcx"));
    assert(CANVAS_W == 33 && CANVAS_H == 17);
    for (int i = 0; i < PIXELS; ++i) assert(canvas[i] == (unsigned char)(i % 256));
    do_undo();
    assert(CANVAS_W == 64 && CANVAS_H == 64);
    do_undo();
    assert(CANVAS_W == 33 && CANVAS_H == 17 && !dirty);
    assert(!paint_resize(0, 10, 0) && CANVAS_W == 33);
    assert(!paint_resize(MAX_DIMENSION + 1, 1, 1) && CANVAS_H == 17);
    assert(paint_resize(3, 2, 0));
    for (int i = 0; i < PIXELS; ++i) canvas[i] = (unsigned char)(i + 1);
    assert(paint_resize(6, 4, 1));
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 6; ++x)
            assert(canvas[y * 6 + x] == (unsigned char)((y / 2) * 3 + x / 2 + 1));
    assert(paint_resize(8, 6, 0));
    assert(canvas[0] == 1 && canvas[3 * 8 + 5] == 6);
    assert(canvas[7] == bg && canvas[5 * 8] == bg);
    do_undo();
    assert(CANVAS_W == 6 && CANVAS_H == 4);
    do_undo();
    assert(CANVAS_W == 8 && CANVAS_H == 6);
    assert(save_pcx("resized.pcx"));
    assert(memcmp(disk + disk_size - 768, expected_palette, 768) == 0);
    api.window_get_client_rect = client_rect;
    api.point_in_rect = point_inside;
    paint_set_zoom(8);
    canvas_left = canvas_top = 0;
    int cx,cy,cw,ch,sx,sy,vw,vh,py,st,px,iy;
    assert(layout(&cx,&cy,&cw,&ch,&sx,&sy,&vw,&vh,&py,&st));
    assert(to_canvas(sx+17,sy+25,&px,&iy) && px == 2 && iy == 3);
    assert(!to_canvas(sx+vw,sy,&px,&iy));
    uint64_t hash = paint_content_hash();
    surface_w = surface_h = 16;
    pixel_grid = 1;
    draw_canvas(0,0,16,16);
    assert(drawn[17] == canvas_rgb(canvas[0]));
    assert(drawn[1] != drawn[17]);
    assert(paint_content_hash() == hash);
    pixel_grid = 0;
    draw_canvas(0,0,16,16);
    assert(drawn[1] == drawn[17]);
    new_canvas();
    assert(CANVAS_W == 480 && CANVAS_H == 294);
    assert(layout(&cx,&cy,&cw,&ch,&sx,&sy,&vw,&vh,&py,&st));
    assert(vw == CANVAS_W && vh == CANVAS_H); /* The workspace fills the client area. */
    test_palette();
    test_ovals();
    test_tools(&api);
    free(fill_queue);
    free(file_buffer);
    free(undo_canvas);
    free(canvas);
    puts("PASS: Paint 256-color display/save/undo, palette grid/scrolling and closed symmetric midpoint ellipses, dimension-preserving PCX, resize/undo and failed-load preservation");
    return 0;
}
