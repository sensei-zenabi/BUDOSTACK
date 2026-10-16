#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static void write_pcx(const char *path, int width, int height, int encoding)
{
    unsigned char header[128] = {10, 5, 0, 8};
    header[2] = (unsigned char)encoding;
    header[8] = (unsigned char)(width - 1);
    header[9] = (unsigned char)((width - 1) >> 8);
    header[10] = (unsigned char)(height - 1);
    header[11] = (unsigned char)((height - 1) >> 8);
    header[65] = 3;
    header[66] = (unsigned char)width;
    header[67] = (unsigned char)(width >> 8);
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(header, 1, 128, file) == 128);
    for (int y = 0; y < height; ++y) {
        for (int plane = 0; plane < 3; ++plane) {
            for (int x = 0; x < width; ++x) {
                int value = plane == 0 ? (x & 1) * 255 : plane == 1 ? (y & 1) * 255 : 64;
                if (encoding && value >= 192) assert(fputc(193, file) != EOF);
                assert(fputc(value, file) != EOF);
            }
        }
    }
    assert(fclose(file) == 0);
}

static void scaling_checks(void)
{
    uint32_t *source = malloc(1280 * 960 * sizeof(*source));
    assert(source);
    for (int i = 0; i < 1280 * 960; ++i)
        source[i] = 0xff000000u | ((i % 1280 & 1) ? 0xffffffu : 0);
    desktop_background_fit(source, 1280, 960, desktop_background, desktop_background_rgb, BACKGROUND_FILL);
    for (int i = 0; i < SCREEN_SIZE; ++i) assert(desktop_background_rgb[i] == 0xff808080u);
    /* Wide and tall sources must crop equally from opposing edges. */
    for (int y = 0; y < 480; ++y)
        for (int x = 0; x < 1280; ++x) source[y * 1280 + x] = (uint32_t)x;
    desktop_background_fit(source, 1280, 480, desktop_background, desktop_background_rgb, BACKGROUND_FILL);
    assert(desktop_background_rgb[0] == (0xff000000u | 320));
    assert(desktop_background_rgb[639] == (0xff000000u | 959));
    for (int y = 0; y < 960; ++y)
        for (int x = 0; x < 640; ++x) source[y * 640 + x] = (uint32_t)y;
    desktop_background_fit(source, 640, 960, desktop_background, desktop_background_rgb, BACKGROUND_FILL);
    assert(desktop_background_rgb[0] == (0xff000000u | 240));
    assert(desktop_background_rgb[SCREEN_SIZE - 1] == (0xff000000u | 719));
    uint32_t small[4] = {0xff000000u, 0xffff0000u, 0xff000000u, 0xffff0000u};
    desktop_background_fit(small, 2, 2, desktop_background, desktop_background_rgb, BACKGROUND_FILL);
    assert(desktop_background_rgb[0] == 0xff000000u);
    assert(desktop_background_rgb[639] == 0xffff0000u);
    assert(((desktop_background_rgb[320] >> 16) & 255) >= 127);
    uint32_t single = 0xff123456u;
    desktop_background_fit(&single, 1, 1, desktop_background, desktop_background_rgb, BACKGROUND_FILL);
    for (int i = 0; i < SCREEN_SIZE; ++i) assert(desktop_background_rgb[i] == single);
    free(source);
}

static void mode_checks(void)
{
    const uint32_t source[8] = {
        0xffff0000u, 0xff00ff00u, 0xff0000ffu, 0xffffffffu,
        0xff123456u, 0xffabcdefu, 0xff101010u, 0xff808080u
    };
    desktop_background_fit(source, 4, 2, desktop_background, desktop_background_rgb, BACKGROUND_FIT);
    assert(desktop_background_rgb[0] == palette[DESKTOP_COLOR]);
    assert(desktop_background_rgb[79 * 640] == palette[DESKTOP_COLOR]);
    assert(desktop_background_rgb[80 * 640] == source[0]);
    assert(desktop_background_rgb[399 * 640 + 639] == source[7]);
    assert(desktop_background_rgb[400 * 640] == palette[DESKTOP_COLOR]);
    desktop_background_fit(source, 4, 2, desktop_background, desktop_background_rgb, BACKGROUND_STRETCH);
    assert(desktop_background_rgb[0] == source[0]);
    assert(desktop_background_rgb[639] == source[3]);
    assert(desktop_background_rgb[SCREEN_SIZE - 1] == source[7]);
    desktop_background_fit(source, 4, 2, desktop_background, desktop_background_rgb, BACKGROUND_CENTER);
    assert(desktop_background_rgb[0] == palette[DESKTOP_COLOR]);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            assert(desktop_background_rgb[(239 + y) * 640 + 318 + x] == source[y * 4 + x]);
    desktop_background_fit(source, 4, 2, desktop_background, desktop_background_rgb, BACKGROUND_TILE);
    for (int y = 0; y < 480; ++y)
        for (int x = 0; x < 640; ++x)
            assert(desktop_background_rgb[y * 640 + x] == source[(y % 2) * 4 + x % 4]);
    /* Mixed-axis Stretch must average horizontally and interpolate vertically. */
    uint32_t *wide = malloc(1280 * sizeof(*wide));
    assert(wide);
    for (int x = 0; x < 1280; ++x) wide[x] = (x & 1) ? 0xffffffffu : 0xff000000u;
    desktop_background_fit(wide, 1280, 1, desktop_background, desktop_background_rgb, BACKGROUND_STRETCH);
    for (int i = 0; i < SCREEN_SIZE; ++i) assert(desktop_background_rgb[i] == 0xff808080u);
    free(wide);
    /* Odd native dimensions retain exact pixels when centered. */
    desktop_background_fit(source, 3, 1, desktop_background, desktop_background_rgb, BACKGROUND_CENTER);
    assert(desktop_background_rgb[239 * 640 + 318] == source[0]);
    assert(desktop_background_rgb[239 * 640 + 320] == source[2]);
}

static void picker_mode_click(int mode)
{
    int x, y, w, h;
    picker_geometry(&x, &y, &w, &h);
    editor_file_dialog_click(x + 180, y + h - 28, 0);
    assert(picker_background_menu);
    editor_file_dialog_click(x + 180, y + h - 34 - (BACKGROUND_MODE_COUNT * 16 + 4) + 4 + mode * 16, 0);
    assert(!picker_background_menu && picker_background_mode == mode);
}

static void picker_screenshot(BwaLoadedApp *settings, const char *directory, const char *name)
{
    char path[MAX_PATH];
    assert(join_path(path, sizeof(path), directory, name));
    draw_desktop(0);
    bwa_draw_external_app(settings);
    editor_draw_file_dialog();
    FILE *file = fopen(path, "wb");
    assert(file && fprintf(file, "P6\n640 480\n255\n") > 0);
    for (int i = 0; i < SCREEN_SIZE; ++i) {
        uint32_t color = rgb_mask[i] ? rgb_framebuffer[i] : palette[framebuffer[i]];
        unsigned char pixel[3] = {(unsigned char)(color >> 16), (unsigned char)(color >> 8), (unsigned char)color};
        assert(fwrite(pixel, 1, 3, file) == 3);
    }
    assert(fclose(file) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    char *args[] = {argv[0], argv[1], NULL};
    assert(bw_initialize(2, args));
    assert(copy_text(home_path, sizeof(home_path), argv[1]));
    set_classic_gui_palette();
    scaling_checks();
    mode_checks();
    bwa_load_external_apps();
    BwaLoadedApp *settings = bwa_find_external_app_id("settings");
    assert(settings && (settings = bwa_new_instance(settings)));
    bwa_callback_app = settings;
    assert(settings->definition.callbacks.open());
    settings->open = 1;
    active_window = settings->definition.runtime_id;
    int x = settings->window.x, y = settings->window.y, h = settings->window.h;
    assert(settings->definition.callbacks.mouse_down(x + 285, y + 55, 1));
    assert(settings->definition.callbacks.mouse_down(x + 25, y + h - 40, 1));
    assert(editor_file_dialog == BUDO_FILE_OPEN && picker_filter == 3);
    char expected[MAX_PATH];
    assert(join_path(expected, sizeof(expected), home_path, "PCX"));
    assert(!strcmp(editor_file_path, expected) && editor_file_name[0] == 0);
    picker_mode_click(BACKGROUND_TILE);
    assert(desktop_background_mode == BACKGROUND_FILL);
    picker_cancel();
    assert(editor_file_dialog == EDITOR_FILE_DIALOG_NONE && desktop_background_mode == BACKGROUND_FILL);
    char path[MAX_PATH];
    assert(join_path(path, sizeof(path), argv[2], "selected.PCX"));
    write_pcx(path, 1280, 960, 1);
    bwa_callback_app = settings;
    assert(settings->definition.callbacks.mouse_down(x + 25, y + h - 40, 1));
    editor_set_file_name(path);
    assert(editor_file_accept() && editor_file_dialog == EDITOR_FILE_DIALOG_NONE);
    for (int i = 0; i < SCREEN_SIZE; ++i) assert(desktop_background_rgb[i] == 0xff808040u);
    memset(desktop_background_rgb, 0, sizeof(desktop_background_rgb));
    desktop_background_loaded = 0;
    desktop_background_restore();
    assert(desktop_background_loaded && desktop_background_rgb[0] == 0xff808040u);
    /* Each mode survives a Settings selection and a fresh state reload. */
    write_pcx(path, 4, 2, 0);
    for (int mode = 0; mode < BACKGROUND_MODE_COUNT; ++mode) {
        bwa_callback_app = settings;
        assert(settings->definition.callbacks.mouse_down(x + 25, y + h - 40, 1));
        picker_mode_click(mode);
        editor_set_file_name(path);
        assert(editor_file_accept() && desktop_background_mode == mode);
        uint32_t expected_pixel = desktop_background_rgb[0];
        desktop_background_mode = BACKGROUND_FILL;
        desktop_background_restore();
        assert(desktop_background_mode == mode && desktop_background_rgb[0] == expected_pixel);
    }
    bwa_callback_app = settings;
    assert(settings->definition.callbacks.mouse_down(x + 25, y + h - 40, 1));
    assert(picker_background_mode == BACKGROUND_TILE);
    picker_screenshot(settings, argv[2], "background-modes.ppm");
    editor_file_dialog_key(9); /* File name -> scaling. */
    assert(picker_background_focus);
    enqueue(72);
    editor_file_dialog_key(0);
    assert(picker_background_mode == BACKGROUND_CENTER);
    editor_file_dialog_key('s');
    assert(picker_background_mode == BACKGROUND_STRETCH);
    editor_file_dialog_key(13);
    assert(picker_background_menu);
    picker_screenshot(settings, argv[2], "background-modes-expanded.ppm");
    editor_file_dialog_key(27);
    assert(!picker_background_menu && editor_file_dialog == BUDO_FILE_OPEN);
    editor_file_dialog_key(27);
    assert(editor_file_dialog == EDITOR_FILE_DIALOG_NONE && desktop_background_mode == BACKGROUND_TILE);
    /* Path-only preferences written by the first implementation mean Fill. */
    FILE *legacy = fopen(bw_state_file("background.state"), "wb");
    assert(legacy && fprintf(legacy, "%s\n", path) > 0 && fclose(legacy) == 0);
    desktop_background_restore();
    assert(desktop_background_mode == BACKGROUND_FILL);
    /* Native-sized input retains exact colors. */
    write_pcx(path, 640, 480, 0);
    assert(bwa_set_desktop_background(path));
    assert(desktop_background_rgb[0] == 0xff000040u);
    assert(desktop_background_rgb[1] == 0xffff0040u);
    assert(desktop_background_rgb[640] == 0xff00ff40u);
    uint32_t previous = desktop_background_rgb[0];
    assert(truncate(path, 130) == 0);
    assert(!bwa_set_desktop_background(path) && desktop_background_rgb[0] == previous);
    /* A save failure must leave the current desktop intact. */
    write_pcx(path, 2, 2, 0);
    assert(unlink(bw_state_file("background.state")) == 0);
    assert(mkdir(bw_state_file("background.state"), 0700) == 0);
    assert(!bwa_set_desktop_background(path) && desktop_background_rgb[0] == previous);
    assert(rmdir(bw_state_file("background.state")) == 0);
    /* Invalid planar stride must be rejected before decoding. */
    FILE *file = fopen(path, "r+b");
    assert(file && fseek(file, 3, SEEK_SET) == 0 && fputc(1, file) == 1);
    assert(fseek(file, 8, SEEK_SET) == 0 && fputc(255, file) == 255);
    assert(fclose(file) == 0 && !bwa_set_desktop_background(path));
    puts("PASS: Settings wallpaper modes, dropdown/keyboard, PCX scaling/cropping, persistence and failure preservation");
    return 0;
}
