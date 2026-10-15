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
    desktop_background_fit(source, 1280, 960, desktop_background, desktop_background_rgb);
    for (int i = 0; i < SCREEN_SIZE; ++i) assert(desktop_background_rgb[i] == 0xff808080u);
    /* Wide and tall sources must crop equally from opposing edges. */
    for (int y = 0; y < 480; ++y)
        for (int x = 0; x < 1280; ++x) source[y * 1280 + x] = (uint32_t)x;
    desktop_background_fit(source, 1280, 480, desktop_background, desktop_background_rgb);
    assert(desktop_background_rgb[0] == (0xff000000u | 320));
    assert(desktop_background_rgb[639] == (0xff000000u | 959));
    for (int y = 0; y < 960; ++y)
        for (int x = 0; x < 640; ++x) source[y * 640 + x] = (uint32_t)y;
    desktop_background_fit(source, 640, 960, desktop_background, desktop_background_rgb);
    assert(desktop_background_rgb[0] == (0xff000000u | 240));
    assert(desktop_background_rgb[SCREEN_SIZE - 1] == (0xff000000u | 719));
    uint32_t small[4] = {0xff000000u, 0xffff0000u, 0xff000000u, 0xffff0000u};
    desktop_background_fit(small, 2, 2, desktop_background, desktop_background_rgb);
    assert(desktop_background_rgb[0] == 0xff000000u);
    assert(desktop_background_rgb[639] == 0xffff0000u);
    assert(((desktop_background_rgb[320] >> 16) & 255) >= 127);
    uint32_t single = 0xff123456u;
    desktop_background_fit(&single, 1, 1, desktop_background, desktop_background_rgb);
    for (int i = 0; i < SCREEN_SIZE; ++i) assert(desktop_background_rgb[i] == single);
    free(source);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    char *args[] = {argv[0], argv[1], NULL};
    assert(bw_initialize(2, args));
    assert(copy_text(home_path, sizeof(home_path), argv[1]));
    set_classic_gui_palette();
    scaling_checks();
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
    picker_cancel();
    assert(editor_file_dialog == EDITOR_FILE_DIALOG_NONE);
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
    puts("PASS: Settings background picker, PCX scaling/cropping, persistence and failure preservation");
    return 0;
}
