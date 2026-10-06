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
    *x = 0; *y = 0; *w = 592; *h = 404;
    return 1;
}
static int point_inside(int x, int y, int rx, int ry, int w, int h) {
    return x >= rx && y >= ry && x < rx + w && y < ry + h;
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
    assert(canvas[0] == 4 && canvas_palette[0] == palette_rgb[0][0]);
    do_undo();
    assert(memcmp(canvas_palette, expected_palette, 768) == 0 && canvas[255] == 255);
    assert(!dirty); /* Undo returned exactly to the saved content. */
    assert(paint_key('[') && palette_page == 15);
    assert(paint_key(']') && palette_page == 0);
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
    assert(CANVAS_W == 480 && CANVAS_H == 303);
    assert(layout(&cx,&cy,&cw,&ch,&sx,&sy,&vw,&vh,&py,&st));
    assert(vw == 480 && vh == 303); /* The workspace fills the client area. */
    free(fill_queue);
    free(file_buffer);
    free(undo_canvas);
    free(canvas);
    puts("PASS: Paint 256-color display/save/undo, palette pages, dimension-preserving PCX, resize/undo and failed-load preservation");
    return 0;
}
