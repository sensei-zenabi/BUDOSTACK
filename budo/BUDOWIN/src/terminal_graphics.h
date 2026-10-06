#ifndef BUDOWIN_TERMINAL_GRAPHICS_H
#define BUDOWIN_TERMINAL_GRAPHICS_H

#define SESSION_ART_W 640
#define SESSION_ART_H 480
#define SESSION_ART_PIXELS (SESSION_ART_W * SESSION_ART_H)
#define SESSION_OSC_LIMIT (24U * 1024U * 1024U)
static uint32_t *session_art_back[16], *session_art_front[16];
static char *session_osc;
static size_t session_osc_size, session_osc_capacity;
static int session_osc_failed, session_overlay;
static unsigned int session_mouse_left, session_mouse_right;
static int session_mouse_x, session_mouse_y;

static void session_art_free(void)
{
    for (int i = 0; i < 16; ++i) {
        free(session_art_back[i]);
        free(session_art_front[i]);
        session_art_back[i] = session_art_front[i] = NULL;
    }
    free(session_osc);
    session_osc = NULL;
    session_osc_size = session_osc_capacity = 0;
}

static void session_osc_append(unsigned char c)
{
    if (session_osc_failed) return;
    if (session_osc_size + 1 >= session_osc_capacity) {
        size_t capacity = session_osc_capacity ? session_osc_capacity * 2 : 4096;
        if (capacity > SESSION_OSC_LIMIT) {
            session_osc_failed = 1;
            fprintf(stderr, "Terminal escape sequence exceeds capacity\n");
            return;
        }
        char *text = realloc(session_osc, capacity);
        if (!text) {
            session_osc_failed = 1;
            perror("Terminal escape sequence");
            return;
        }
        session_osc = text;
        session_osc_capacity = capacity;
    }
    session_osc[session_osc_size++] = (char)c;
    session_osc[session_osc_size] = 0;
}

static uint32_t *session_art_buffer(int layer)
{
    if (layer < 1 || layer > 16) return NULL;
    if (!session_art_back[layer - 1]) {
        session_art_back[layer - 1] = calloc(SESSION_ART_PIXELS, sizeof(uint32_t));
        if (!session_art_back[layer - 1]) perror("Terminal graphics layer");
    }
    return session_art_back[layer - 1];
}

static void session_art_pixel(uint32_t *image, int x, int y, uint32_t color)
{
    if (image && x >= 0 && x < SESSION_ART_W && y >= 0 && y < SESSION_ART_H)
        image[y * SESSION_ART_W + x] = color;
}

static void session_art_commit(int layer)
{
    for (int i = 0; i < 16; ++i) {
        if ((layer && layer != i + 1) || !session_art_back[i]) continue;
        if (!session_art_front[i]) session_art_front[i] = malloc(SESSION_ART_PIXELS * sizeof(uint32_t));
        if (!session_art_front[i]) {
            perror("Terminal graphics frame");
            continue;
        }
        memcpy(session_art_front[i], session_art_back[i], SESSION_ART_PIXELS * sizeof(uint32_t));
    }
}

static int session_base64_value(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static unsigned char *session_base64_decode(const char *text, size_t *size)
{
    size_t length = strlen(text);
    unsigned char *bytes = malloc(length / 4 * 3 + 4);
    if (!bytes) {
        perror("Terminal sprite");
        return NULL;
    }
    unsigned int accumulator = 0;
    int bits = 0;
    *size = 0;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '=') break;
        int value = session_base64_value((unsigned char)text[i]);
        if (value < 0) {
            free(bytes);
            return NULL;
        }
        accumulator = (accumulator << 6) | (unsigned int)value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes[(*size)++] = (unsigned char)(accumulator >> bits);
        }
    }
    return bytes;
}

static void session_art_rect(int *x, int *y, int *w, int *h)
{
    *x = terminal_window.x + 7;
    *y = terminal_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 6;
    *w = terminal_window.w - 30;
    *h = session_rows * 9;
    if (*w * SESSION_ART_H > *h * SESSION_ART_W) {
        int width = *h * SESSION_ART_W / SESSION_ART_H;
        *x += (*w - width) / 2;
        *w = width;
    } else {
        int height = *w * SESSION_ART_H / SESSION_ART_W;
        *y += (*h - height) / 2;
        *h = height;
    }
}

static uint32_t session_palette_rgb(unsigned char index)
{
    if (index >= 16) index = 5;
    const unsigned char *color = desktop_ui_style.colors[index];
    return ((uint32_t)(color[0] * 255 / 63) << 16) |
        ((uint32_t)(color[1] * 255 / 63) << 8) | (uint32_t)(color[2] * 255 / 63);
}

static void session_art_draw(void)
{
    int x, y, w, h;
    session_art_rect(&x, &y, &w, &h);
    /* Layer 1 is topmost, matching apps/terminal. */
    for (int layer = 15; layer >= 0; --layer) {
        const uint32_t *image = session_art_front[layer];
        if (!image) continue;
        for (int py = 0; py < h; ++py)
            for (int px = 0; px < w; ++px) {
                uint32_t color = image[(py * SESSION_ART_H / h) * SESSION_ART_W + px * SESSION_ART_W / w];
                unsigned int alpha = color >> 24;
                if (!alpha) continue;
                int offset = (y + py) * SCREEN_WIDTH + x + px;
                uint32_t under = rgb_mask[offset] ? rgb_framebuffer[offset] : session_palette_rgb(framebuffer[offset]);
                unsigned int r = (((color >> 16) & 255) * alpha + ((under >> 16) & 255) * (255 - alpha)) / 255;
                unsigned int g = (((color >> 8) & 255) * alpha + ((under >> 8) & 255) * (255 - alpha)) / 255;
                unsigned int b = ((color & 255) * alpha + (under & 255) * (255 - alpha)) / 255;
                put_rgb_pixel(x + px, y + py, (r << 16) | (g << 8) | b);
            }
    }
}

static void session_art_mouse(int x, int y, int buttons, int previous)
{
    int rx, ry, w, h;
    session_art_rect(&rx, &ry, &w, &h);
    if (w < 1 || h < 1 || !point_in_rect(x, y, rx, ry, w, h)) return;
    session_mouse_x = (x - rx) * SESSION_ART_W / w;
    session_mouse_y = (y - ry) * SESSION_ART_H / h;
    if ((buttons & 1) && !(previous & 1)) ++session_mouse_left;
    if ((buttons & 2) && !(previous & 2)) ++session_mouse_right;
}

static void session_osc_finish(void)
{
    if (session_osc_failed || !session_osc || strncmp(session_osc, "777;", 4)) return;
    char *copy = strdup(session_osc + 4);
    if (!copy) {
        perror("Terminal command");
        return;
    }
    const char *action = NULL, *sprite = NULL, *text = NULL;
    int x = 0, y = 0, w = 1, h = 1, r = 0, g = 0, b = 0, layer = 1, text_color = 17;
    int render_layer = 0, query_mouse = 0, query_overlay = 0, is_art = 0;
    char *save = NULL;
    for (char *token = strtok_r(copy, ";", &save); token; token = strtok_r(NULL, ";", &save)) {
        char *value = strchr(token, '=');
        if (!value) continue;
        *value++ = 0;
        char *end;
        long number = strtol(value, &end, 10);
        int valid = end != value && !*end && number >= -1000000 && number <= 1000000;
        if (!strcmp(token, "pixel") || !strcmp(token, "sprite") || !strcmp(token, "text")) {
            action = value;
            is_art = !strcmp(token, "sprite") ? 2 : !strcmp(token, "text") ? 3 : 1;
        } else if (!strcmp(token, "sprite_data")) sprite = value;
        else if (!strcmp(token, "text_data")) text = value;
        else if (!strcmp(token, "mouse") && !strcmp(value, "query")) query_mouse = 1;
        else if (!strcmp(token, "overlay")) {
            if (!strcmp(value, "query")) query_overlay = 1;
            else session_overlay = !strcmp(value, "enable");
        } else if (valid) {
            if (!strcmp(token, "pixel_x") || !strcmp(token, "sprite_x") || !strcmp(token, "text_x")) x = (int)number;
            else if (!strcmp(token, "pixel_y") || !strcmp(token, "sprite_y") || !strcmp(token, "text_y")) y = (int)number;
            else if (!strcmp(token, "pixel_w") || !strcmp(token, "sprite_w")) w = (int)number;
            else if (!strcmp(token, "pixel_h") || !strcmp(token, "sprite_h")) h = (int)number;
            else if (!strcmp(token, "pixel_r")) r = (int)number;
            else if (!strcmp(token, "pixel_g")) g = (int)number;
            else if (!strcmp(token, "pixel_b")) b = (int)number;
            else if (!strcmp(token, "text_color")) text_color = (int)number;
            else if (!strcmp(token, "pixel_layer") || !strcmp(token, "sprite_layer") || !strcmp(token, "text_layer")) {
                render_layer = number >= 0 && number <= 16 ? (int)number : 0;
                layer = number >= 1 && number <= 16 ? (int)number : 1;
            }
        }
    }
    if (query_mouse) {
        char response[128];
        int size = snprintf(response, sizeof(response), "_TERM_MOUSE %d %d %u %u\n",
            session_mouse_x, session_mouse_y, session_mouse_left, session_mouse_right);
        session_write(response, (size_t)size);
        session_mouse_left = session_mouse_right = 0;
    } else if (query_overlay) {
        char response[32];
        int size = snprintf(response, sizeof(response), "_TERM_OVERLAY %d\n", session_overlay);
        session_write(response, (size_t)size);
    } else if (is_art && action) {
        if (!strcmp(action, "render")) session_art_commit(render_layer);
        else {
            uint32_t *image = session_art_buffer(layer);
            if (!strcmp(action, "clear")) {
                if (is_art == 1) {
                    if (image) memset(image, 0, SESSION_ART_PIXELS * sizeof(uint32_t));
                } else for (int py = y < 0 ? 0 : y; py < SESSION_ART_H && py < y + h; ++py)
                    for (int px = x < 0 ? 0 : x; px < SESSION_ART_W && px < x + w; ++px)
                        session_art_pixel(image, px, py, 0);
            } else if (is_art == 1 && r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                uint32_t color = 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                if (!strcmp(action, "draw") || !strcmp(action, "set")) session_art_pixel(image, x, y, color);
                else if (!strcmp(action, "rect"))
                    for (int py = y < 0 ? 0 : y; py < SESSION_ART_H && py < y + h; ++py)
                        for (int px = x < 0 ? 0 : x; px < SESSION_ART_W && px < x + w; ++px)
                            session_art_pixel(image, px, py, color);
            } else if (is_art == 2 && sprite && w > 0 && h > 0 && w <= 2048 && h <= 2048) {
                size_t size;
                unsigned char *bytes = session_base64_decode(sprite, &size);
                if (bytes && size == (size_t)w * (size_t)h * 4)
                    for (int py = 0; py < h; ++py)
                        for (int px = 0; px < w; ++px) {
                            const unsigned char *c = bytes + ((size_t)py * w + px) * 4;
                            session_art_pixel(image, x + px, y + py, ((uint32_t)c[3] << 24) | ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | c[2]);
                        }
                else fprintf(stderr, "Terminal sprite data has invalid size\n");
                free(bytes);
            } else if (is_art == 3 && text) {
                size_t size;
                unsigned char *bytes = session_base64_decode(text, &size);
                if (bytes) {
                    char *string = malloc(size + 1);
                    if (string) {
                        memcpy(string, bytes, size);
                        string[size] = 0;
                        const char *cursor = string;
                        uint32_t color = session_palette_rgb(text_color == 18 ? 1 : text_color == 17 ? 5 : session_ansi_color(text_color - 1));
                        int col = 0;
                        while (*cursor) {
                            const unsigned char *glyph = glyph_for((char)bw_text_cell(&cursor));
                            for (int gy = 0; gy < 7; ++gy)
                                for (int gx = 0; gx < 5; ++gx)
                                    if (glyph[gy] & (0x10 >> gx)) session_art_pixel(image, x + col * 6 + gx, y + gy, color);
                            ++col;
                        }
                        free(string);
                    } else perror("Terminal text");
                    free(bytes);
                }
            }
        }
    } else {
        /* Retain SDL shader, sound and outer-terminal settings. */
        fputs("\033]", stdout);
        fputs(session_osc, stdout);
        fputc(7, stdout);
        fflush(stdout);
    }
    free(copy);
}
#endif
