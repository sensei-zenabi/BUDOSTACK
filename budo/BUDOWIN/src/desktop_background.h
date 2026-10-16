#ifndef BUDOWIN_DESKTOP_BACKGROUND_H
#define BUDOWIN_DESKTOP_BACKGROUND_H

/* Included after the PCX decoder. Bound source memory to 80 MiB. */
#define BACKGROUND_MAX_PIXELS (16u * 1024u * 1024u)

enum {
    BACKGROUND_FILL, BACKGROUND_FIT, BACKGROUND_STRETCH,
    BACKGROUND_CENTER, BACKGROUND_TILE, BACKGROUND_MODE_COUNT
};
static const char *const background_modes[BACKGROUND_MODE_COUNT] = {
    "Fill", "Fit", "Stretch", "Center", "Tile"
};
static int desktop_background_mode = BACKGROUND_FILL;
static int picker_background_mode, picker_background_menu, picker_background_focus;
static int picker_background_accepting;

typedef struct BackgroundSpan {
    int first, last;
    double left, right;
} BackgroundSpan;

static BackgroundSpan background_span(double start, double step, int limit)
{
    BackgroundSpan span;
    if (step >= 1.0) {
        span.left = start;
        span.right = start + step;
        if (span.right > limit) span.right = limit;
        span.first = (int)span.left;
        span.last = (int)span.right;
        if (span.last == span.right) --span.last;
    } else {
        /* Bilinear samples refer to pixel centers; clamp at image edges. */
        span.left = start + step / 2.0 - 0.5;
        if (span.left < 0) span.left = 0;
        if (span.left > limit - 1) span.left = limit - 1;
        span.right = span.left + 1;
        span.first = (int)span.left;
        span.last = span.first + 1;
    }
    if (span.last >= limit) span.last = limit - 1;
    return span;
}

static double background_weight(BackgroundSpan span, int sample, double step)
{
    if (step < 1.0) {
        double fraction = span.left - span.first;
        return sample == span.first ? 1.0 - fraction : fraction;
    }
    double left = span.left > sample ? span.left : sample;
    double right = span.right < sample + 1 ? span.right : sample + 1;
    return right > left ? right - left : 0;
}

/* Area reduction and bilinear enlargement, independently on each axis.
 * Fit/Center margins use the desktop color at selection time. */
static void desktop_background_fit(const uint32_t *source, int width, int height,
                                    unsigned char *indices, uint32_t *rgb, int mode)
{
    uint32_t margin = 0xff000000u | budo_palette_rgb(DESKTOP_COLOR);
    if (DESKTOP_COLOR < 16) {
        const unsigned char *color = desktop_ui_style.colors[DESKTOP_COLOR];
        margin = 0xff000000u | ((uint32_t)(color[0] * 255 / 63) << 16) |
                 ((uint32_t)(color[1] * 255 / 63) << 8) | (uint32_t)(color[2] * 255 / 63);
    }
    double step_x = (double)width / SCREEN_WIDTH;
    double step_y = (double)height / SCREEN_HEIGHT;
    double step = mode == BACKGROUND_FIT ?
                  (step_x > step_y ? step_x : step_y) :
                  (step_x < step_y ? step_x : step_y);
    if (mode != BACKGROUND_STRETCH) step_x = step_y = step;
    if (mode == BACKGROUND_CENTER || mode == BACKGROUND_TILE) step_x = step_y = 1;
    double left = (width - SCREEN_WIDTH * step_x) / 2.0;
    double top = (height - SCREEN_HEIGHT * step_y) / 2.0;
    if (mode == BACKGROUND_FIT) {
        int fitted_w = (int)(width / step + 0.5);
        int fitted_h = (int)(height / step + 0.5);
        if (fitted_w < 1) fitted_w = 1;
        if (fitted_h < 1) fitted_h = 1;
        step_x = (double)width / fitted_w;
        step_y = (double)height / fitted_h;
        left = -((SCREEN_WIDTH - fitted_w) / 2) * step_x;
        top = -((SCREEN_HEIGHT - fitted_h) / 2) * step_y;
    } else if (mode == BACKGROUND_CENTER) {
        left = (width - SCREEN_WIDTH) / 2;
        top = (height - SCREEN_HEIGHT) / 2;
    }
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            size_t offset = (size_t)y * SCREEN_WIDTH + x;
            double source_x = left + (x + 0.5) * step_x;
            double source_y = top + (y + 0.5) * step_y;
            if (mode == BACKGROUND_TILE) {
                uint32_t color = source[(size_t)(y % height) * width + x % width];
                rgb[offset] = color;
                indices[offset] = pcx_rgb_to_index((unsigned char)(color >> 16),
                                                  (unsigned char)(color >> 8), (unsigned char)color);
                continue;
            }
            if (source_x < 0 || source_x >= width || source_y < 0 || source_y >= height) {
                rgb[offset] = margin;
                indices[offset] = DESKTOP_COLOR;
                continue;
            }
            double start_x = left + x * step_x;
            double start_y = top + y * step_y;
            BackgroundSpan sx = background_span(start_x < 0 ? 0 : start_x, step_x, width);
            BackgroundSpan sy = background_span(start_y < 0 ? 0 : start_y, step_y, height);
            double red = 0, green = 0, blue = 0, total = 0;
            for (int iy = sy.first; iy <= sy.last; ++iy) {
                double wy = background_weight(sy, iy, step_y);
                for (int ix = sx.first; ix <= sx.last; ++ix) {
                    double weight = wy * background_weight(sx, ix, step_x);
                    uint32_t color = source[(size_t)iy * width + ix];
                    red += ((color >> 16) & 255) * weight;
                    green += ((color >> 8) & 255) * weight;
                    blue += (color & 255) * weight;
                    total += weight;
                }
            }
            unsigned char r = (unsigned char)(red / total + 0.5);
            unsigned char g = (unsigned char)(green / total + 0.5);
            unsigned char b = (unsigned char)(blue / total + 0.5);
            rgb[offset] = 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            indices[offset] = pcx_rgb_to_index(r, g, b);
        }
    }
}

static int desktop_background_read(const char *path, unsigned char *indices, uint32_t *rgb, int mode)
{
    unsigned char header[128];
    const char *extension = strrchr(path, '.');
    if (!extension || compare_names_ci(extension, ".pcx") != 0) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    int ok = fread(header, 1, sizeof(header), file) == sizeof(header);
    if (fclose(file) != 0) ok = 0;
    if (!ok || header[0] != 10 || pcx_u16(header + 8) < pcx_u16(header + 4) ||
        pcx_u16(header + 10) < pcx_u16(header + 6)) return 0;
    int width = pcx_u16(header + 8) - pcx_u16(header + 4) + 1;
    int height = pcx_u16(header + 10) - pcx_u16(header + 6) + 1;
    if ((size_t)width > BACKGROUND_MAX_PIXELS / (size_t)height) return 0;
    size_t count = (size_t)width * height;
    unsigned char *source_indices = malloc(count);
    uint32_t *source_rgb = malloc(count * sizeof(*source_rgb));
    ok = source_indices && source_rgb &&
         load_pcx_image(path, width, height, source_indices, source_rgb);
    if (ok) desktop_background_fit(source_rgb, width, height, indices, rgb, mode);
    free(source_indices);
    free(source_rgb);
    return ok;
}

static int desktop_background_apply(const char *path, int persist, int mode)
{
    if (mode < 0 || mode >= BACKGROUND_MODE_COUNT) return 0;
    unsigned char *indices = malloc(sizeof(desktop_background));
    uint32_t *rgb = malloc(sizeof(desktop_background_rgb));
    int ok = indices && rgb && desktop_background_read(path, indices, rgb, mode);
    if (ok && persist) {
        char state[MAX_PATH], temporary[MAX_PATH];
        ok = copy_text(state, sizeof(state), bw_state_file("background.state"));
        if (ok) {
            FILE *file = desktop_file_open(state, temporary);
            if (!file) { fprintf(stderr, "BUDOWIN: unable to save background settings: %s\n", state); ok = 0; }
            else {
                if (fprintf(file, "%s\n%d\n", path, mode) < 0) {
                    (void)fclose(file);
                    (void)unlink(temporary);
                    ok = 0;
                } else ok = desktop_file_finish(file, state, temporary);
                if (!ok) perror("Save desktop background");
            }
        }
    }
    if (ok) {
        memcpy(desktop_background, indices, sizeof(desktop_background));
        memcpy(desktop_background_rgb, rgb, sizeof(desktop_background_rgb));
        desktop_background_loaded = 1;
        desktop_background_mode = mode;
    }
    free(indices);
    free(rgb);
    return ok;
}

static int bwa_set_desktop_background(const char *path)
{
    if (!path) return 0;
    char *resolved = realpath(path, NULL);
    if (!resolved) return 0;
    int ok = strlen(resolved) < MAX_PATH && !strchr(resolved, '\n') &&
             !strchr(resolved, '\r') && desktop_background_apply(resolved, 1,
                 picker_background_accepting ? picker_background_mode : desktop_background_mode);
    free(resolved);
    return ok;
}

static void desktop_background_restore(void)
{
    char path[MAX_PATH];
    FILE *file = fopen(bw_state_file("background.state"), "rb");
    if (!file) {
        if (errno != ENOENT) perror("Load desktop background");
        return;
    }
    int ok = fgets(path, sizeof(path), file) != NULL;
    int mode = BACKGROUND_FILL;
    if (ok) {
        /* The original path-only state means Fill. Reject malformed modes. */
        int next = fgetc(file);
        if (next != EOF) {
            char line[32];
            (void)ungetc(next, file);
            if (!fgets(line, sizeof(line), file) || strlen(line) != 2 ||
                line[0] < '0' || line[0] >= '0' + BACKGROUND_MODE_COUNT || line[1] != '\n' ||
                fgetc(file) != EOF) ok = 0;
            else mode = line[0] - '0';
        }
        if (ferror(file)) ok = 0;
    }
    if (fclose(file) != 0) ok = 0;
    if (ok) {
        char *newline = strchr(path, '\n');
        if (newline) *newline = '\0';
        else ok = 0;
    }
    if (!ok || !desktop_background_apply(path, 0, mode))
        fprintf(stderr, "BUDOWIN: saved background unavailable or invalid; using default\n");
}

#endif
