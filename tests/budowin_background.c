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
    memset(background_mask, 1, sizeof(background_mask));
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
    assert(!bw_screen_copy(SCREEN_SIZE, framebuffer, 1, desktop_background_rgb, background_mask));
    puts("PASS: every background RGB pixel, GUI composition, cursor transparency/restoration, copy bounds");
    return 0;
}
