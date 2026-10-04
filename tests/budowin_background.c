#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>
int main(int argc, char **argv) {
    assert(argc == 2);
    unsigned char header[128] = {0};
    header[0] = 10;
    header[1] = 5;
    header[3] = 8;
    header[8] = 127;
    header[9] = 2;  /* xmax = 639 */
    header[10] = 223;
    header[11] = 1; /* ymax = 479 */
    header[65] = 1;
    header[66] = 128;
    header[67] = 2; /* 640 bytes per row, uncompressed */
    FILE *fixture = fopen(argv[1], "wb");
    assert(fixture);
    assert(fwrite(header, 1, sizeof(header), fixture) == sizeof(header));
    for (int i = 0; i < SCREEN_SIZE; ++i) assert(fputc(i & 255, fixture) != EOF);
    assert(fputc(12, fixture) != EOF);
    for (int i = 0; i < 256; ++i) {
        assert(fputc(i, fixture) != EOF);
        assert(fputc(255 - i, fixture) != EOF);
        assert(fputc(i ^ 85, fixture) != EOF);
    }
    assert(fclose(fixture) == 0);
    assert(load_pcx_image(argv[1], 640, 480,
                          desktop_background, desktop_background_rgb));
    for (int i = 0; i < SCREEN_SIZE; ++i) {
        uint32_t index = (uint32_t)i & 255u;
        assert(desktop_background_rgb[i] == (0xff000000u | index << 16 |
                                            (255u - index) << 8 | (index ^ 85u)));
    }
    set_classic_gui_palette();
    desktop_background_loaded = 1;
    memcpy(framebuffer, desktop_background, sizeof(framebuffer));
    memcpy(rgb_framebuffer, desktop_background_rgb, sizeof(rgb_framebuffer));
    memset(rgb_mask, 1, sizeof(rgb_mask));
    assert(present_framebuffer());
    assert(memcmp(pixels, desktop_background_rgb, sizeof(pixels)) == 0);
    put_pixel(0, 0, 4);
    fill_rect(10, 10, 30, 20, 6);
    assert(present_framebuffer());
    assert(pixels[0] == palette[4]);
    assert(pixels[10 * 640 + 10] == palette[6]);
    assert(pixels[1] == desktop_background_rgb[1]);
    cursor_pcx_icon_loaded = 1;
    memset(cursor_pcx_icon, PCX_TRANSPARENT, sizeof(cursor_pcx_icon));
    cursor_pcx_icon[0] = 4;
    draw_cursor_vga(100, 100);
    assert(pixels[100 * 640 + 100] == palette[4]);
    assert(pixels[100 * 640 + 101] == desktop_background_rgb[100 * 640 + 101]);
    restore_cursor_area(100, 100);
    assert(pixels[100 * 640 + 100] == desktop_background_rgb[100 * 640 + 100]);
    assert(!bw_screen_copy(SCREEN_SIZE, framebuffer, 1, desktop_background_rgb, rgb_mask));
    /* Icons use all source colors, and only exact top-left RGB matches
     * become transparent (nearby colors must survive cube collisions). */
    char icon_dir[MAX_PATH], icon_path[MAX_PATH];
    assert(realpath(argv[1], home_path));
    char *slash = strrchr(home_path, '/');
    assert(slash);
    *slash = '\0';
    assert(join_path(icon_dir, sizeof(icon_dir), home_path, "PCX"));
    assert(mkdir(icon_dir, 0700) == 0);
    assert(join_path(icon_path, sizeof(icon_path), icon_dir, "COLORS.PCX"));
    header[8] = header[10] = 31;
    header[9] = header[11] = 0;
    header[66] = 32;
    header[67] = 0;
    fixture = fopen(icon_path, "wb");
    assert(fixture);
    assert(fwrite(header, 1, sizeof(header), fixture) == sizeof(header));
    for (int i = 0; i < 1024; ++i) assert(fputc(i & 255, fixture) != EOF);
    assert(fputc(12, fixture) != EOF);
    for (int i = 0; i < 256; ++i) {
        assert(fputc(i, fixture) != EOF);
        assert(fputc(255 - i, fixture) != EOF);
        assert(fputc(i ^ 85, fixture) != EOF);
    }
    assert(fclose(fixture) == 0);
    assert(load_named_pcx("COLORS.PCX", 32, 32, explorer_pcx_icon, 1));
    assert(explorer_pcx_icon[0] == PCX_TRANSPARENT);
    assert(explorer_pcx_icon[1] != PCX_TRANSPARENT);
    const uint32_t *icon_rgb = pcx_icon_rgb(explorer_pcx_icon);
    assert(icon_rgb);
    draw_pcx_icon(200, 200, explorer_pcx_icon);
    assert(present_framebuffer());
    for (int i = 0; i < 1024; ++i) {
        int offset = (200 + i / 32) * 640 + 200 + i % 32;
        uint32_t expected = (i & 255) == 0 ? desktop_background_rgb[offset] :
            0xff000000u | ((uint32_t)(i & 255) << 16) |
            ((uint32_t)(255 - (i & 255)) << 8) | ((uint32_t)(i & 255) ^ 85u);
        assert(pixels[offset] == expected);
    }
    assert(load_named_pcx("COLORS.PCX", 32, 32, cursor_pcx_icon, 1));
    draw_cursor_vga(200, 200);
    assert(pixels[200 * 640 + 201] == icon_rgb[1]);
    restore_cursor_area(200, 200);
    assert(pixels[200 * 640 + 201] == icon_rgb[1]);
    fill_rect_rgb(-1, -1, 2, 2, 0x123456u);
    assert(present_framebuffer());
    assert(pixels[0] == 0xff123456u);
    put_pixel(0, 0, 4);
    assert(present_framebuffer());
    assert(pixels[0] == palette[4]);
    assert(unlink(icon_path) == 0);
    assert(rmdir(icon_dir) == 0);
    puts("PASS: exact PCX background/icon/cursor colors, transparency, RGB/UI composition and clipping");
    return 0;
}
