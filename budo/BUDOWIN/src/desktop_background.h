#ifndef BUDOWIN_DESKTOP_BACKGROUND_H
#define BUDOWIN_DESKTOP_BACKGROUND_H

/* Included after the PCX decoder. Bound source memory to 80 MiB. */
#define BACKGROUND_MAX_PIXELS (16u * 1024u * 1024u)

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

/* Cover the desktop without distortion: centered crop, area reduction and
 * bilinear enlargement. Native resolution retains every original RGB value. */
static void desktop_background_fit(const uint32_t *source, int width, int height,
                                    unsigned char *indices, uint32_t *rgb)
{
    double step_x = (double)width / SCREEN_WIDTH;
    double step_y = (double)height / SCREEN_HEIGHT;
    double step = step_x < step_y ? step_x : step_y;
    double left = (width - SCREEN_WIDTH * step) / 2.0;
    double top = (height - SCREEN_HEIGHT * step) / 2.0;
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        BackgroundSpan sy = background_span(top + y * step, step, height);
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            BackgroundSpan sx = background_span(left + x * step, step, width);
            double red = 0, green = 0, blue = 0, total = 0;
            for (int iy = sy.first; iy <= sy.last; ++iy) {
                double wy = background_weight(sy, iy, step);
                for (int ix = sx.first; ix <= sx.last; ++ix) {
                    double weight = wy * background_weight(sx, ix, step);
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
            size_t offset = (size_t)y * SCREEN_WIDTH + x;
            rgb[offset] = 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            indices[offset] = pcx_rgb_to_index(r, g, b);
        }
    }
}

static int desktop_background_read(const char *path, unsigned char *indices, uint32_t *rgb)
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
    if (ok) desktop_background_fit(source_rgb, width, height, indices, rgb);
    free(source_indices);
    free(source_rgb);
    return ok;
}

static int desktop_background_apply(const char *path, int persist)
{
    unsigned char *indices = malloc(sizeof(desktop_background));
    uint32_t *rgb = malloc(sizeof(desktop_background_rgb));
    int ok = indices && rgb && desktop_background_read(path, indices, rgb);
    if (ok && persist) {
        char state[MAX_PATH], temporary[MAX_PATH];
        ok = copy_text(state, sizeof(state), bw_state_file("background.state"));
        if (ok) {
            FILE *file = desktop_file_open(state, temporary);
            if (!file) { fprintf(stderr, "BUDOWIN: unable to save background settings: %s\n", state); ok = 0; }
            else {
                if (fprintf(file, "%s\n", path) < 0) {
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
             !strchr(resolved, '\r') && desktop_background_apply(resolved, 1);
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
    if (fclose(file) != 0) ok = 0;
    if (ok) {
        char *newline = strchr(path, '\n');
        if (newline) *newline = '\0';
        else ok = 0;
    }
    if (!ok || !desktop_background_apply(path, 0))
        fprintf(stderr, "BUDOWIN: saved background unavailable or invalid; using default\n");
}

#endif
