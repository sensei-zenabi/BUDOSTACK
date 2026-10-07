#ifndef BUDOWIN_DISPLAY_SETTINGS_H
#define BUDOWIN_DISPLAY_SETTINGS_H
#define BW_MAX_PIXELS (1280 * 960)
static int display_workspace, display_scale = 1;
static int display_pending_workspace, display_pending_scale = 1;
int bw_screen_width(void) { return (display_workspace == 1 ? 960 : display_workspace == 2 ? 1280 : 640) / display_scale; }
int bw_screen_height(void) { return (display_workspace == 1 ? 720 : display_workspace == 2 ? 960 : 480) / display_scale; }
int bw_get_workspace(void) { return display_pending_workspace; }
int bw_get_display_scale(void) { return display_pending_scale; }
static void bw_load_display(void)
{
    FILE *file = fopen(bw_state_file("display.state"), "r");
    int workspace, scale;
    if (file) {
        if (fscanf(file, "%d %d", &workspace, &scale) == 2 && workspace >= 0 && workspace <= 2 && scale >= 1 && scale <= 2) {
            if (scale == 2) workspace = 2;
            display_workspace = display_pending_workspace = workspace;
            display_scale = display_pending_scale = scale;
        }
        fclose(file);
    }
}
int bw_set_display(int workspace, int scale)
{
    if (workspace < 0 || workspace > 2 || scale < 1 || scale > 2) return 0;
    if (scale == 2) workspace = 2;
    char temporary[4096];
    const char *path = bw_state_file("display.state");
    int length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (length < 0 || (size_t)length >= sizeof(temporary)) return 0;
    FILE *file = fopen(temporary, "w");
    if (!file) return 0;
    int ok = fprintf(file, "%d %d\n", workspace, scale) > 0 && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) ok = 0;
    if (ok) ok = rename(temporary, path) == 0;
    if (!ok) { unlink(temporary); return 0; }
    display_pending_workspace = workspace; display_pending_scale = scale;
    return 1;
}
static uint32_t scaled_pixels[BW_MAX_PIXELS];
static const uint32_t *bw_presentation_pixels(void)
{
    if (display_scale == 1) return pixels;
    int width = bw_screen_width(), height = bw_screen_height();
    for (int y = 0; y < height; ++y) {
        uint32_t *row = scaled_pixels + y * 2 * width * 2;
        for (int x = 0; x < width; ++x) row[x * 2] = row[x * 2 + 1] = pixels[y * width + x];
        memcpy(row + width * 2, row, (size_t)width * 2 * sizeof(*row));
    }
    return scaled_pixels;
}
#endif
