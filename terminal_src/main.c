#define _XOPEN_SOURCE 700
#include <SDL2/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <math.h>

#include <SDL2/SDL_ttf.h>

#include "terminal_buffer.h"

#define TERMINAL_FONT_PATH "fonts/ModernDOS8x8.ttf"
#define TERMINAL_FONT_POINT_SIZE 8
#define TERMINAL_MAX_LINES 2048
#define TERMINAL_READ_CHUNK 4096
#define TERMINAL_PADDING 0
#define TERMINAL_DEFAULT_COLS 80
#define TERMINAL_DEFAULT_ROWS 50
#define TERMINAL_TARGET_WIDTH 320
#define TERMINAL_TARGET_HEIGHT 200
#define TERMINAL_FALLBACK_WIDTH 640
#define TERMINAL_FALLBACK_HEIGHT 480
#define TERMINAL_BASE_CHAR_WIDTH 8.0f
#define TERMINAL_BASE_CHAR_HEIGHT 8.0f

typedef struct {
    uint32_t codepoint;
    SDL_Texture *texture;
    int width;
    int height;
} GlyphCacheEntry;

typedef struct {
    TTF_Font *handle;
    int width;
    int height;
    int line_skip;
    int ascent;
    int descent;
} TerminalFont;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TerminalFont font;
    int char_width;
    int char_height;
    int line_height;
    int cols;
    int rows;
    int logical_width;
    int logical_height;
    int target_width;
    int target_height;
    int content_width;
    int content_height;
    int content_offset_x;
    int content_offset_y;
    int window_width;
    int window_height;
    float scale_x;
    float scale_y;
    GlyphCacheEntry *glyph_cache;
    size_t glyph_cache_count;
    size_t glyph_cache_capacity;
} TerminalRenderer;

static int g_running = 1;
static int g_ttf_initialized = 0;

static SDL_Color terminal_color_from_index(uint8_t index)
{
    static const SDL_Color basic_palette[16] = {
        {0, 0, 0, 255},       {205, 0, 0, 255},     {0, 205, 0, 255},
        {205, 205, 0, 255},   {0, 0, 238, 255},     {205, 0, 205, 255},
        {0, 205, 205, 255},   {229, 229, 229, 255}, {127, 127, 127, 255},
        {255, 0, 0, 255},     {0, 255, 0, 255},     {255, 255, 0, 255},
        {92, 92, 255, 255},   {255, 0, 255, 255},   {0, 255, 255, 255},
        {255, 255, 255, 255}
    };

    SDL_Color color = {0, 0, 0, 255};
    if (index < 16) {
        return basic_palette[index];
    }

    if (index >= 16 && index <= 231) {
        int idx = index - 16;
        int r = (idx / 36) % 6;
        int g = (idx / 6) % 6;
        int b = idx % 6;
        static const int levels[6] = {0, 95, 135, 175, 215, 255};
        color.r = (Uint8)levels[r];
        color.g = (Uint8)levels[g];
        color.b = (Uint8)levels[b];
        return color;
    }

    if (index >= 232) {
        int level = 8 + ((int)index - 232) * 10;
        if (level > 255) {
            level = 255;
        }
        color.r = color.g = color.b = (Uint8)level;
        return color;
    }

    return color;
}

static void terminal_font_destroy(TerminalFont *font)
{
    if (!font) {
        return;
    }

    if (font->handle) {
        TTF_CloseFont(font->handle);
        font->handle = NULL;
    }

    font->width = 0;
    font->height = 0;
    font->line_skip = 0;
    font->ascent = 0;
    font->descent = 0;
}

static int terminal_font_load_ttf(const char *path, int point_size, TerminalFont *font)
{
    if (!path || !font || point_size <= 0) {
        return -1;
    }

    memset(font, 0, sizeof(*font));

    TTF_Font *ttf = TTF_OpenFont(path, point_size);
    if (!ttf) {
        return -1;
    }

    TTF_SetFontHinting(ttf, TTF_HINTING_MONO);
    TTF_SetFontKerning(ttf, 0);

    font->handle = ttf;
    font->ascent = TTF_FontAscent(ttf);
    font->descent = TTF_FontDescent(ttf);
    font->height = TTF_FontHeight(ttf);
    font->line_skip = TTF_FontLineSkip(ttf);

    if (font->height <= 0) {
        font->height = point_size;
    }
    if (font->line_skip <= 0) {
        font->line_skip = font->height;
    }

    int advance = 0;
    if (TTF_GlyphMetrics32(ttf, 'M', NULL, NULL, NULL, NULL, &advance) == 0 && advance > 0) {
        font->width = advance;
    } else {
        font->width = font->height;
    }

    return 0;
}

static void destroy_glyph_cache(TerminalRenderer *renderer)
{
    if (!renderer) {
        return;
    }

    for (size_t i = 0; i < renderer->glyph_cache_count; ++i) {
        if (renderer->glyph_cache[i].texture) {
            SDL_DestroyTexture(renderer->glyph_cache[i].texture);
            renderer->glyph_cache[i].texture = NULL;
        }
    }
    free(renderer->glyph_cache);
    renderer->glyph_cache = NULL;
    renderer->glyph_cache_count = 0;
    renderer->glyph_cache_capacity = 0;
}

static int ensure_glyph_cache_capacity(TerminalRenderer *renderer, size_t needed)
{
    if (renderer->glyph_cache_capacity >= needed) {
        return 0;
    }

    size_t new_capacity = renderer->glyph_cache_capacity ? renderer->glyph_cache_capacity * 2 : 128;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    GlyphCacheEntry *new_entries = realloc(renderer->glyph_cache, new_capacity * sizeof(*new_entries));
    if (!new_entries) {
        return -1;
    }

    renderer->glyph_cache = new_entries;
    renderer->glyph_cache_capacity = new_capacity;
    return 0;
}

static GlyphCacheEntry *find_glyph(TerminalRenderer *renderer, uint32_t codepoint)
{
    if (!renderer) {
        return NULL;
    }
    for (size_t i = 0; i < renderer->glyph_cache_count; ++i) {
        if (renderer->glyph_cache[i].codepoint == codepoint) {
            return &renderer->glyph_cache[i];
        }
    }
    return NULL;
}

static GlyphCacheEntry *cache_glyph(TerminalRenderer *renderer, uint32_t codepoint)
{
    if (!renderer) {
        return NULL;
    }

    if (ensure_glyph_cache_capacity(renderer, renderer->glyph_cache_count + 1) == -1) {
        return NULL;
    }

    GlyphCacheEntry *entry = &renderer->glyph_cache[renderer->glyph_cache_count++];
    entry->codepoint = codepoint;
    entry->texture = NULL;
    entry->width = renderer->char_width;
    entry->height = renderer->line_height;

    if (codepoint == 0 || !renderer->font.handle) {
        return entry;
    }

    Uint32 glyph_code = codepoint;
    if (!TTF_GlyphIsProvided32(renderer->font.handle, glyph_code)) {
        glyph_code = '?';
        if (!TTF_GlyphIsProvided32(renderer->font.handle, glyph_code)) {
            glyph_code = ' ';
        }
    }

    if (glyph_code == ' ') {
        return entry;
    }

    int minx = 0;
    int maxx = 0;
    int miny = 0;
    int maxy = 0;
    int advance = 0;
    if (TTF_GlyphMetrics32(renderer->font.handle, glyph_code, &minx, &maxx, &miny, &maxy, &advance) != 0) {
        return entry;
    }
    (void)advance;

    const int cell_width = renderer->font.width > 0 ? renderer->font.width : renderer->char_width;
    const int cell_height = renderer->font.line_skip > 0 ? renderer->font.line_skip : renderer->line_height;
    SDL_Surface *cell_surface = SDL_CreateRGBSurfaceWithFormat(0, cell_width, cell_height, 32, SDL_PIXELFORMAT_RGBA32);
    if (!cell_surface) {
        return entry;
    }

    SDL_SetSurfaceBlendMode(cell_surface, SDL_BLENDMODE_NONE);
    SDL_FillRect(cell_surface, NULL, SDL_MapRGBA(cell_surface->format, 0, 0, 0, 0));

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface *glyph_surface = TTF_RenderGlyph32_Blended(renderer->font.handle, glyph_code, white);
    if (!glyph_surface) {
        SDL_FreeSurface(cell_surface);
        return entry;
    }

    SDL_SetSurfaceBlendMode(glyph_surface, SDL_BLENDMODE_BLEND);

    SDL_Rect dest_rect;
    dest_rect.x = minx;
    int baseline = renderer->font.ascent;
    if (baseline <= 0) {
        baseline = cell_height;
    }
    if (baseline > cell_height) {
        baseline = cell_height;
    }

    dest_rect.y = baseline - maxy;
    dest_rect.w = glyph_surface->w;
    dest_rect.h = glyph_surface->h;

    SDL_Rect src_rect = {0, 0, glyph_surface->w, glyph_surface->h};

    if (dest_rect.x < 0) {
        src_rect.x = -dest_rect.x;
        dest_rect.w -= src_rect.x;
        dest_rect.x = 0;
    }
    if (dest_rect.y < 0) {
        src_rect.y = -dest_rect.y;
        dest_rect.h -= src_rect.y;
        dest_rect.y = 0;
    }
    if (dest_rect.x + dest_rect.w > cell_surface->w) {
        dest_rect.w = cell_surface->w - dest_rect.x;
    }
    if (dest_rect.y + dest_rect.h > cell_surface->h) {
        dest_rect.h = cell_surface->h - dest_rect.y;
    }

    if (dest_rect.w <= 0 || dest_rect.h <= 0) {
        SDL_FreeSurface(glyph_surface);
        SDL_FreeSurface(cell_surface);
        return entry;
    }
    src_rect.w = dest_rect.w;
    src_rect.h = dest_rect.h;

    SDL_BlitSurface(glyph_surface, &src_rect, cell_surface, &dest_rect);
    SDL_FreeSurface(glyph_surface);

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer->renderer, cell_surface);
    SDL_FreeSurface(cell_surface);
    if (!texture) {
        return entry;
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    entry->texture = texture;
    entry->width = cell_width;
    entry->height = cell_height;
    return entry;
}

static const GlyphCacheEntry *get_or_create_glyph(TerminalRenderer *renderer, uint32_t codepoint)
{
    GlyphCacheEntry *existing = find_glyph(renderer, codepoint);
    if (existing) {
        return existing;
    }
    return cache_glyph(renderer, codepoint);
}

static void cleanup_child(pid_t pid)
{
    if (pid <= 0) {
        return;
    }

    int status = 0;
    while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        continue;
    }
}

static void handle_signal(int signal_number)
{
    (void)signal_number;
    g_running = 0;
}

static void configure_fullscreen_mode(TerminalRenderer *renderer)
{
    if (!renderer || !renderer->window) {
        return;
    }

    int display_index = SDL_GetWindowDisplayIndex(renderer->window);
    if (display_index < 0) {
        display_index = 0;
    }

    const struct {
        int width;
        int height;
    } modes[] = {
        {TERMINAL_TARGET_WIDTH * 2, TERMINAL_TARGET_HEIGHT * 2},
        {TERMINAL_FALLBACK_WIDTH, TERMINAL_FALLBACK_HEIGHT},
    };

    for (size_t i = 0; i < SDL_arraysize(modes); ++i) {
        SDL_DisplayMode desired = {0};
        desired.w = modes[i].width;
        desired.h = modes[i].height;
        SDL_DisplayMode closest;
        if (!SDL_GetClosestDisplayMode(display_index, &desired, &closest)) {
            continue;
        }
        if (SDL_SetWindowDisplayMode(renderer->window, &closest) != 0) {
            continue;
        }
        if (SDL_SetWindowFullscreen(renderer->window, SDL_WINDOW_FULLSCREEN) == 0) {
            renderer->target_width = closest.w;
            renderer->target_height = closest.h;
            return;
        }
    }

    if (SDL_SetWindowFullscreen(renderer->window, SDL_WINDOW_FULLSCREEN_DESKTOP) == 0) {
        SDL_GetWindowSize(renderer->window, &renderer->target_width, &renderer->target_height);
    }
}

static int init_renderer(TerminalRenderer *renderer)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    renderer->glyph_cache = NULL;
    renderer->glyph_cache_count = 0;
    renderer->glyph_cache_capacity = 0;

    if (TTF_Init() == -1) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return -1;
    }
    g_ttf_initialized = 1;

    if (terminal_font_load_ttf(TERMINAL_FONT_PATH, TERMINAL_FONT_POINT_SIZE, &renderer->font) == -1) {
        fprintf(stderr, "Failed to load font '%s': %s\n", TERMINAL_FONT_PATH, TTF_GetError());
        TTF_Quit();
        g_ttf_initialized = 0;
        SDL_Quit();
        return -1;
    }

    renderer->cols = TERMINAL_DEFAULT_COLS;
    renderer->rows = TERMINAL_DEFAULT_ROWS;
    renderer->char_width = renderer->font.width > 0 ? renderer->font.width : (int)TERMINAL_BASE_CHAR_WIDTH;
    renderer->char_height = renderer->font.height > 0 ? renderer->font.height : (int)TERMINAL_BASE_CHAR_HEIGHT;
    renderer->line_height = renderer->font.line_skip > 0 ? renderer->font.line_skip : renderer->char_height;
    renderer->scale_x = 1.0f;
    renderer->scale_y = 1.0f;

    renderer->logical_width = renderer->cols * renderer->char_width + TERMINAL_PADDING * 2;
    renderer->logical_height = renderer->rows * renderer->line_height + TERMINAL_PADDING * 2;
    renderer->target_width = TERMINAL_FALLBACK_WIDTH;
    renderer->target_height = TERMINAL_FALLBACK_HEIGHT;

    Uint32 window_flags = SDL_WINDOW_ALLOW_HIGHDPI;
    renderer->window = SDL_CreateWindow(
        "Budostack Terminal",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        renderer->target_width,
        renderer->target_height,
        window_flags);
    if (!renderer->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        terminal_font_destroy(&renderer->font);
        if (g_ttf_initialized) {
            TTF_Quit();
            g_ttf_initialized = 0;
        }
        SDL_Quit();
        return -1;
    }

    configure_fullscreen_mode(renderer);

    renderer->renderer = SDL_CreateRenderer(renderer->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(renderer->window);
        terminal_font_destroy(&renderer->font);
        if (g_ttf_initialized) {
            TTF_Quit();
            g_ttf_initialized = 0;
        }
        SDL_Quit();
        return -1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_GetWindowSize(renderer->window, &renderer->window_width, &renderer->window_height);

    const float scale_x = (float)renderer->target_width /
        (float)(renderer->cols * renderer->font.width);
    const float scale_y = (float)renderer->target_height /
        (float)(renderer->rows * renderer->font.line_skip);

    float uniform_scale = scale_x;
    if (uniform_scale <= 0.0f || (scale_y > 0.0f && scale_y < uniform_scale)) {
        uniform_scale = scale_y;
    }
    if (uniform_scale <= 0.0f) {
        uniform_scale = 1.0f;
    }

    renderer->scale_x = uniform_scale;
    renderer->scale_y = uniform_scale;

    renderer->char_width = (int)lroundf(renderer->font.width * renderer->scale_x);
    renderer->line_height = (int)lroundf(renderer->font.line_skip * renderer->scale_y);
    if (renderer->char_width <= 0) {
        renderer->char_width = renderer->font.width > 0 ? renderer->font.width : 1;
    }
    if (renderer->line_height <= 0) {
        renderer->line_height = renderer->font.line_skip > 0 ? renderer->font.line_skip : 1;
    }

    renderer->char_height = renderer->line_height;
    renderer->logical_width = renderer->cols * renderer->char_width + TERMINAL_PADDING * 2;
    renderer->logical_height = renderer->rows * renderer->line_height + TERMINAL_PADDING * 2;

    renderer->content_width = renderer->char_width * renderer->cols + TERMINAL_PADDING * 2;
    renderer->content_height = renderer->line_height * renderer->rows + TERMINAL_PADDING * 2;
    renderer->content_offset_x = (renderer->target_width - renderer->content_width) / 2;
    renderer->content_offset_y = (renderer->target_height - renderer->content_height) / 2;
    if (renderer->content_offset_x < 0) {
        renderer->content_offset_x = 0;
    }
    if (renderer->content_offset_y < 0) {
        renderer->content_offset_y = 0;
    }

    SDL_GetWindowSize(renderer->window, &renderer->window_width, &renderer->window_height);
    SDL_StartTextInput();

    return 0;
}

static void destroy_renderer(TerminalRenderer *renderer)
{
    if (!renderer) {
        return;
    }

    SDL_StopTextInput();
    destroy_glyph_cache(renderer);
    terminal_font_destroy(&renderer->font);
    if (g_ttf_initialized) {
        TTF_Quit();
        g_ttf_initialized = 0;
    }
    if (renderer->renderer) {
        SDL_DestroyRenderer(renderer->renderer);
        renderer->renderer = NULL;
    }
    if (renderer->window) {
        SDL_DestroyWindow(renderer->window);
        renderer->window = NULL;
    }
    SDL_Quit();
}

static int read_from_child(int pty_fd, TerminalBuffer *buffer, int *content_dirty)
{
    char read_buffer[TERMINAL_READ_CHUNK];
    ssize_t total_read = 0;

    for (;;) {
        ssize_t bytes = read(pty_fd, read_buffer, sizeof(read_buffer));
        if (bytes > 0) {
            if (terminal_buffer_append(buffer, read_buffer, (size_t)bytes) == -1) {
                return -1;
            }
            total_read += bytes;
        } else {
            if (bytes == -1 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
                break;
            }
            if (bytes == -1 && errno == EIO) {
                break;
            }
            if (bytes == 0) {
                break;
            }
            if (bytes == -1 && errno == EINTR) {
                continue;
            }
            return -1;
        }
    }

    if (total_read > 0 && content_dirty) {
        *content_dirty = 1;
    }

    return 0;
}

static int send_bytes(int pty_fd, const char *data, size_t length)
{
    while (length > 0) {
        ssize_t written = write(pty_fd, data, length);
        if (written == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                SDL_Delay(1);
                continue;
            }
            return -1;
        }
        data += written;
        length -= (size_t)written;
    }
    return 0;
}

static int send_control_character(int pty_fd, char control_character)
{
    return send_bytes(pty_fd, &control_character, 1);
}

static int send_text(int pty_fd, const char *text)
{
    return send_bytes(pty_fd, text, strlen(text));
}

static int send_escape_sequence(int pty_fd, const char *sequence)
{
    return send_bytes(pty_fd, sequence, strlen(sequence));
}

static void render_terminal(TerminalRenderer *renderer, const TerminalBuffer *buffer)
{
    if (!renderer || !buffer) {
        return;
    }

    SDL_SetRenderDrawColor(renderer->renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer->renderer);

    const int rows = terminal_buffer_rows(buffer);
    const int cols = terminal_buffer_cols(buffer);
    const int content_left = renderer->content_offset_x + TERMINAL_PADDING;
    const int content_top = renderer->content_offset_y + TERMINAL_PADDING;

    SDL_Rect cell_rect = {0, 0, renderer->char_width, renderer->line_height};
    SDL_Color last_bg = {0, 0, 0, 255};
    int last_bg_valid = 0;

    for (int row = 0; row < rows; ++row) {
        cell_rect.y = content_top + row * renderer->line_height;
        for (int col = 0; col < cols; ++col) {
            cell_rect.x = content_left + col * renderer->char_width;

            const TerminalCell *cell = terminal_buffer_cell(buffer, row, col);
            if (!cell) {
                continue;
            }

            uint8_t fg_index = cell->fg;
            uint8_t bg_index = cell->bg;
            if (cell->inverse) {
                uint8_t tmp = fg_index;
                fg_index = bg_index;
                bg_index = tmp;
            }

            if (cell->bold && fg_index < 8) {
                fg_index = (uint8_t)(fg_index + 8);
            }

            SDL_Color bg_color = terminal_color_from_index(bg_index);
            if (!last_bg_valid || bg_color.r != last_bg.r || bg_color.g != last_bg.g || bg_color.b != last_bg.b) {
                SDL_SetRenderDrawColor(renderer->renderer, bg_color.r, bg_color.g, bg_color.b, 255);
                last_bg = bg_color;
                last_bg_valid = 1;
            }
            SDL_RenderFillRect(renderer->renderer, &cell_rect);

            uint32_t codepoint = cell->codepoint;
            if (codepoint == 0 || codepoint == ' ') {
                continue;
            }

            const GlyphCacheEntry *glyph = get_or_create_glyph(renderer, codepoint);
            if (!glyph || !glyph->texture) {
                continue;
            }

            SDL_Color fg_color = terminal_color_from_index(fg_index);
            SDL_SetTextureColorMod(glyph->texture, fg_color.r, fg_color.g, fg_color.b);
            SDL_SetTextureAlphaMod(glyph->texture, fg_color.a);

            SDL_RenderCopy(renderer->renderer, glyph->texture, NULL, &cell_rect);
        }
    }

    SDL_RenderPresent(renderer->renderer);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGQUIT, handle_signal);

    TerminalRenderer renderer = {0};
    if (init_renderer(&renderer) == -1) {
        return EXIT_FAILURE;
    }

    TerminalBuffer buffer;
    if (terminal_buffer_init(&buffer, TERMINAL_MAX_LINES) == -1) {
        fprintf(stderr, "Failed to initialize terminal buffer: %s\n", strerror(errno));
        destroy_renderer(&renderer);
        return EXIT_FAILURE;
    }

    struct winsize ws = {0};
    ws.ws_col = (unsigned short)renderer.cols;
    ws.ws_row = (unsigned short)renderer.rows;

    int pty_fd = -1;
    pid_t child_pid = forkpty(&pty_fd, NULL, NULL, &ws);
    if (child_pid == -1) {
        fprintf(stderr, "forkpty failed: %s\n", strerror(errno));
        terminal_buffer_destroy(&buffer);
        destroy_renderer(&renderer);
        return EXIT_FAILURE;
    }

    if (child_pid == 0) {
        char exe_path[PATH_MAX];
        if (!realpath("./budostack", exe_path)) {
            perror("realpath");
            _exit(EXIT_FAILURE);
        }

        if (setenv("TERM", "xterm-256color", 1) == -1) {
            perror("setenv TERM");
        }

        char *child_argv[] = {exe_path, NULL};
        execv(exe_path, child_argv);
        perror("execv");
        _exit(EXIT_FAILURE);
    }

    int flags = fcntl(pty_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(pty_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        fprintf(stderr, "Failed to set PTY non-blocking: %s\n", strerror(errno));
        kill(child_pid, SIGKILL);
        cleanup_child(child_pid);
        close(pty_fd);
        terminal_buffer_destroy(&buffer);
        destroy_renderer(&renderer);
        return EXIT_FAILURE;
    }

    int content_dirty = 1;

    while (g_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                g_running = 0;
                break;
            case SDL_KEYDOWN: {
                SDL_Keycode key = event.key.keysym.sym;
                const SDL_Keymod mods = event.key.keysym.mod;

                if ((mods & KMOD_CTRL) && key == SDLK_c) {
                    send_control_character(pty_fd, '\003');
                } else if ((mods & KMOD_CTRL) && key == SDLK_d) {
                    send_control_character(pty_fd, '\004');
                } else if ((mods & KMOD_CTRL) && key == SDLK_l) {
                    send_control_character(pty_fd, '\f');
                } else if (key == SDLK_BACKSPACE) {
                    send_control_character(pty_fd, '\b');
                } else if (key == SDLK_RETURN) {
                    send_control_character(pty_fd, '\n');
                } else if (key == SDLK_ESCAPE) {
                    send_control_character(pty_fd, '\x1b');
                } else if (key == SDLK_TAB) {
                    send_control_character(pty_fd, '\t');
                } else if (key == SDLK_UP) {
                    send_escape_sequence(pty_fd, "\x1b[A");
                } else if (key == SDLK_DOWN) {
                    send_escape_sequence(pty_fd, "\x1b[B");
                } else if (key == SDLK_RIGHT) {
                    send_escape_sequence(pty_fd, "\x1b[C");
                } else if (key == SDLK_LEFT) {
                    send_escape_sequence(pty_fd, "\x1b[D");
                } else if (key == SDLK_HOME) {
                    send_escape_sequence(pty_fd, "\x1b[H");
                } else if (key == SDLK_END) {
                    send_escape_sequence(pty_fd, "\x1b[F");
                } else if (key == SDLK_PAGEUP) {
                    send_escape_sequence(pty_fd, "\x1b[5~");
                } else if (key == SDLK_PAGEDOWN) {
                    send_escape_sequence(pty_fd, "\x1b[6~");
                }
                break;
            }
            case SDL_TEXTINPUT:
                send_text(pty_fd, event.text.text);
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    g_running = 0;
                }
                break;
            default:
                break;
            }
        }

        if (read_from_child(pty_fd, &buffer, &content_dirty) == -1) {
            g_running = 0;
        }

        int status = 0;
        pid_t wait_result = waitpid(child_pid, &status, WNOHANG);
        if (wait_result == child_pid) {
            g_running = 0;
        }

        if (content_dirty) {
            render_terminal(&renderer, &buffer);
            content_dirty = 0;
        }

        SDL_Delay(10);
    }

    kill(child_pid, SIGHUP);
    cleanup_child(child_pid);
    close(pty_fd);

    terminal_buffer_destroy(&buffer);
    destroy_renderer(&renderer);

    return EXIT_SUCCESS;
}
