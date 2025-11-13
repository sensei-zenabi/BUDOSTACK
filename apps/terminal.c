#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#if defined(__has_include)
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#define BUDOSTACK_HAVE_SDL2 1
#elif __has_include(<SDL.h>)
#include <SDL.h>
#define BUDOSTACK_HAVE_SDL2 1
#else
#define BUDOSTACK_HAVE_SDL2 0
#endif
#else
#include <SDL2/SDL.h>
#define BUDOSTACK_HAVE_SDL2 1
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#if BUDOSTACK_HAVE_SDL2

#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04
#define PSF1_MODE512 0x01
#define PSF2_MAGIC 0x864ab572u
#define PSF2_HEADER_SIZE 32u

struct psf_font {
    uint32_t glyph_count;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t glyph_size;
    uint8_t *glyphs;
};

struct terminal_cell {
    uint32_t ch;
};

struct terminal_buffer {
    size_t columns;
    size_t rows;
    size_t cursor_column;
    size_t cursor_row;
    struct terminal_cell *cells;
};

static void free_font(struct psf_font *font) {
    if (!font) {
        return;
    }
    free(font->glyphs);
    font->glyphs = NULL;
    font->glyph_count = 0;
    font->width = 0;
    font->height = 0;
    font->stride = 0;
    font->glyph_size = 0;
}

static uint32_t read_u32_le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static int load_psf_font(const char *path, struct psf_font *out_font, char *errbuf, size_t errbuf_size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Failed to open '%s': %s", path, strerror(errno));
        }
        return -1;
    }

    unsigned char header[PSF2_HEADER_SIZE];
    size_t header_read = fread(header, 1, sizeof(header), fp);
    if (header_read < 4) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "File too small to be a PSF font");
        }
        fclose(fp);
        return -1;
    }

    struct psf_font font = {0};

    if (header[0] == PSF1_MAGIC0 && header[1] == PSF1_MAGIC1) {
        if (header_read < 4) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Incomplete PSF1 header");
            }
            fclose(fp);
            return -1;
        }

        uint32_t glyph_count = (header[2] & PSF1_MODE512) ? 512u : 256u;
        uint32_t charsize = header[3];

        font.width = 8u;
        font.height = charsize;
        font.stride = 1u;
        font.glyph_size = font.height * font.stride;
        font.glyph_count = glyph_count;

        if (font.glyph_size == 0 || glyph_count == 0) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Invalid PSF1 font dimensions");
            }
            fclose(fp);
            return -1;
        }

        if ((size_t)glyph_count > SIZE_MAX / font.glyph_size) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Font too large");
            }
            fclose(fp);
            return -1;
        }

        if (fseek(fp, 4L, SEEK_SET) != 0) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Failed to seek glyph data");
            }
            fclose(fp);
            return -1;
        }

        size_t total = (size_t)glyph_count * font.glyph_size;
        font.glyphs = calloc(total, 1);
        if (!font.glyphs) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Out of memory allocating font");
            }
            fclose(fp);
            return -1;
        }

        if (fread(font.glyphs, 1, total, fp) != total) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Failed to read glyph data");
            }
            free_font(&font);
            fclose(fp);
            return -1;
        }
    } else if (read_u32_le(header) == PSF2_MAGIC) {
        if (header_read < PSF2_HEADER_SIZE) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Incomplete PSF2 header");
            }
            fclose(fp);
            return -1;
        }

        uint32_t version = read_u32_le(header + 4);
        (void)version;
        uint32_t header_size = read_u32_le(header + 8);
        uint32_t flags = read_u32_le(header + 12);
        (void)flags;
        uint32_t glyph_count = read_u32_le(header + 16);
        uint32_t glyph_size = read_u32_le(header + 20);
        uint32_t height = read_u32_le(header + 24);
        uint32_t width = read_u32_le(header + 28);

        if (glyph_count == 0 || glyph_size == 0 || height == 0 || width == 0) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Invalid PSF2 font dimensions");
            }
            fclose(fp);
            return -1;
        }

        if ((size_t)glyph_count > SIZE_MAX / glyph_size) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Font too large");
            }
            fclose(fp);
            return -1;
        }

        if (fseek(fp, (long)header_size, SEEK_SET) != 0) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Failed to seek glyph data");
            }
            fclose(fp);
            return -1;
        }

        font.width = width;
        font.height = height;
        font.stride = (width + 7u) / 8u;
        font.glyph_size = glyph_size;
        font.glyph_count = glyph_count;

        font.glyphs = calloc((size_t)glyph_count, glyph_size);
        if (!font.glyphs) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Out of memory allocating font");
            }
            fclose(fp);
            return -1;
        }

        if (fread(font.glyphs, 1, (size_t)glyph_count * glyph_size, fp) != (size_t)glyph_count * glyph_size) {
            if (errbuf && errbuf_size > 0) {
                snprintf(errbuf, errbuf_size, "Failed to read glyph data");
            }
            free_font(&font);
            fclose(fp);
            return -1;
        }
    } else {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Unsupported font format");
        }
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *out_font = font;
    return 0;
}

static int terminal_buffer_init(struct terminal_buffer *buffer, size_t columns, size_t rows) {
    buffer->columns = columns;
    buffer->rows = rows;
    buffer->cursor_column = 0u;
    buffer->cursor_row = 0u;

    size_t total_cells = columns * rows;
    buffer->cells = calloc(total_cells, sizeof(struct terminal_cell));
    if (!buffer->cells) {
        return -1;
    }
    return 0;
}

static void terminal_buffer_reset(struct terminal_buffer *buffer) {
    if (!buffer || !buffer->cells) {
        return;
    }
    memset(buffer->cells, 0, buffer->columns * buffer->rows * sizeof(struct terminal_cell));
    buffer->cursor_column = 0u;
    buffer->cursor_row = 0u;
}

static void terminal_buffer_free(struct terminal_buffer *buffer) {
    if (!buffer) {
        return;
    }
    free(buffer->cells);
    buffer->cells = NULL;
    buffer->columns = 0u;
    buffer->rows = 0u;
    buffer->cursor_column = 0u;
    buffer->cursor_row = 0u;
}

static void terminal_buffer_scroll(struct terminal_buffer *buffer) {
    if (!buffer || buffer->rows == 0u || buffer->columns == 0u) {
        return;
    }
    size_t row_size = buffer->columns * sizeof(struct terminal_cell);
    memmove(buffer->cells, buffer->cells + buffer->columns, row_size * (buffer->rows - 1u));
    memset(buffer->cells + buffer->columns * (buffer->rows - 1u), 0, row_size);
    if (buffer->cursor_row > 0u) {
        buffer->cursor_row--;
    }
}

static void terminal_put_char(struct terminal_buffer *buffer, uint32_t ch) {
    if (!buffer || !buffer->cells) {
        return;
    }

    switch (ch) {
    case '\r':
        buffer->cursor_column = 0u;
        return;
    case '\n':
        buffer->cursor_column = 0u;
        buffer->cursor_row++;
        break;
    case '\t': {
        size_t next_tab = ((buffer->cursor_column / 8u) + 1u) * 8u;
        if (next_tab >= buffer->columns) {
            buffer->cursor_column = buffer->columns - 1u;
        } else {
            buffer->cursor_column = next_tab;
        }
        return;
    }
    case '\b':
        if (buffer->cursor_column > 0u) {
            buffer->cursor_column--;
        } else if (buffer->cursor_row > 0u) {
            buffer->cursor_row--;
            buffer->cursor_column = buffer->columns ? buffer->columns - 1u : 0u;
        }
        buffer->cells[buffer->cursor_row * buffer->columns + buffer->cursor_column].ch = ' ';
        return;
    default:
        if (ch < 32u && ch != '\t') {
            return;
        }
        if (buffer->cursor_row >= buffer->rows) {
            terminal_buffer_scroll(buffer);
        }
        if (buffer->cursor_row >= buffer->rows) {
            return;
        }
        if (buffer->cursor_column >= buffer->columns) {
            buffer->cursor_column = 0u;
            buffer->cursor_row++;
            if (buffer->cursor_row >= buffer->rows) {
                terminal_buffer_scroll(buffer);
            }
        }
        if (buffer->cursor_row >= buffer->rows) {
            return;
        }
        buffer->cells[buffer->cursor_row * buffer->columns + buffer->cursor_column].ch = ch;
        buffer->cursor_column++;
        return;
    }

    if (buffer->cursor_row >= buffer->rows) {
        terminal_buffer_scroll(buffer);
    }
}

static int compute_root_directory(const char *argv0, char *out_path, size_t out_size) {
    if (!argv0 || !out_path || out_size == 0u) {
        return -1;
    }

    char resolved[PATH_MAX];
    if (!realpath(argv0, resolved)) {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) {
            return -1;
        }
        size_t len = strlen(cwd);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, cwd, len + 1u);
        return 0;
    }

    char *last_sep = strrchr(resolved, '/');
    if (!last_sep) {
        size_t len = strlen(resolved);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, resolved, len + 1u);
        return 0;
    }
    *last_sep = '\0';

    char *apps_sep = strrchr(resolved, '/');
    if (!apps_sep) {
        size_t len = strlen(resolved);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, resolved, len + 1u);
        return 0;
    }
    *apps_sep = '\0';

    size_t len = strlen(resolved);
    if (len >= out_size) {
        return -1;
    }
    memcpy(out_path, resolved, len + 1u);
    return 0;
}

static int build_path(char *dest, size_t dest_size, const char *base, const char *suffix) {
    if (!dest || dest_size == 0u || !base || !suffix) {
        return -1;
    }
    int written = snprintf(dest, dest_size, "%s/%s", base, suffix);
    if (written < 0 || (size_t)written >= dest_size) {
        return -1;
    }
    return 0;
}

static void update_pty_size(int fd, size_t columns, size_t rows) {
    if (fd < 0) {
        return;
    }
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    if (columns > 0u) {
        ws.ws_col = (unsigned short)columns;
    }
    if (rows > 0u) {
        ws.ws_row = (unsigned short)rows;
    }
    ioctl(fd, TIOCSWINSZ, &ws);
}

static pid_t spawn_budostack(const char *exe_path, int *out_master_fd) {
    if (!exe_path || !out_master_fd) {
        return -1;
    }

    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) {
        perror("posix_openpt");
        return -1;
    }

    if (grantpt(master_fd) < 0 || unlockpt(master_fd) < 0) {
        perror("grantpt/unlockpt");
        close(master_fd);
        return -1;
    }

    char *slave_name = ptsname(master_fd);
    if (!slave_name) {
        perror("ptsname");
        close(master_fd);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(master_fd);
        return -1;
    }

    if (pid == 0) {
        if (setsid() == -1) {
            perror("setsid");
            _exit(EXIT_FAILURE);
        }

        int slave_fd = open(slave_name, O_RDWR);
        if (slave_fd < 0) {
            perror("open slave pty");
            _exit(EXIT_FAILURE);
        }

        if (ioctl(slave_fd, TIOCSCTTY, 0) == -1) {
            perror("ioctl TIOCSCTTY");
            _exit(EXIT_FAILURE);
        }

        if (dup2(slave_fd, STDIN_FILENO) < 0 || dup2(slave_fd, STDOUT_FILENO) < 0 || dup2(slave_fd, STDERR_FILENO) < 0) {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }

        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }

        close(master_fd);

        const char *term_value = getenv("TERM");
        if (!term_value || term_value[0] == '\0') {
            setenv("TERM", "xterm-256color", 1);
        }

        execl(exe_path, exe_path, (char *)NULL);
        perror("execl");
        _exit(EXIT_FAILURE);
    }

    *out_master_fd = master_fd;
    return pid;
}

static ssize_t safe_write(int fd, const void *buf, size_t count) {
    const unsigned char *ptr = (const unsigned char *)buf;
    size_t remaining = count;
    while (remaining > 0) {
        ssize_t written = write(fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        ptr += (size_t)written;
        remaining -= (size_t)written;
    }
    return (ssize_t)count;
}

static void update_terminal_geometry(struct terminal_buffer *buffer, size_t columns, size_t rows) {
    if (!buffer) {
        return;
    }
    if (columns == buffer->columns && rows == buffer->rows && buffer->cells) {
        return;
    }

    struct terminal_cell *old_cells = buffer->cells;
    size_t old_columns = buffer->columns;
    size_t old_rows = buffer->rows;
    size_t old_cursor_column = buffer->cursor_column;
    size_t old_cursor_row = buffer->cursor_row;

    if (terminal_buffer_init(buffer, columns, rows) != 0) {
        buffer->cells = old_cells;
        buffer->columns = old_columns;
        buffer->rows = old_rows;
        buffer->cursor_column = old_cursor_column;
        buffer->cursor_row = old_cursor_row;
        return;
    }

    free(old_cells);
}

static SDL_Texture *create_glyph_texture(SDL_Renderer *renderer, const struct psf_font *font, uint32_t glyph_index) {
    if (!renderer || !font || !font->glyphs) {
        return NULL;
    }
    if (glyph_index >= font->glyph_count) {
        glyph_index = '?';
        if (glyph_index >= font->glyph_count) {
            glyph_index = 0u;
        }
    }

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, (int)font->width, (int)font->height, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        return NULL;
    }

    uint32_t *pixels = (uint32_t *)surface->pixels;
    size_t pitch_pixels = (size_t)surface->pitch / sizeof(uint32_t);
    const uint8_t *glyph = font->glyphs + glyph_index * font->glyph_size;

    for (uint32_t y = 0u; y < font->height; y++) {
        for (uint32_t x = 0u; x < font->width; x++) {
            size_t byte_index = y * font->stride + x / 8u;
            uint8_t mask = (uint8_t)(0x80u >> (x % 8u));
            int set = (glyph[byte_index] & mask) != 0;
            pixels[y * pitch_pixels + x] = set ? 0xFFFFFFFFu : 0x00000000u;
        }
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (texture) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
    return texture;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    char root_dir[PATH_MAX];
    if (compute_root_directory(argv[0], root_dir, sizeof(root_dir)) != 0) {
        fprintf(stderr, "Failed to resolve BUDOSTACK root directory.\n");
        return EXIT_FAILURE;
    }

    char budostack_path[PATH_MAX];
    if (build_path(budostack_path, sizeof(budostack_path), root_dir, "budostack") != 0) {
        fprintf(stderr, "Failed to resolve budostack executable path.\n");
        return EXIT_FAILURE;
    }

    if (access(budostack_path, X_OK) != 0) {
        fprintf(stderr, "Could not find executable at %s.\n", budostack_path);
        return EXIT_FAILURE;
    }

    char font_path[PATH_MAX];
    if (build_path(font_path, sizeof(font_path), root_dir, "fonts/pcw8x8.psf") != 0) {
        fprintf(stderr, "Failed to resolve font path.\n");
        return EXIT_FAILURE;
    }

    struct psf_font font = {0};
    char errbuf[256];
    if (load_psf_font(font_path, &font, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "Failed to load font: %s\n", errbuf);
        return EXIT_FAILURE;
    }

    int master_fd = -1;
    pid_t child_pid = spawn_budostack(budostack_path, &master_fd);
    if (child_pid < 0) {
        free_font(&font);
        return EXIT_FAILURE;
    }

    if (fcntl(master_fd, F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl");
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        return EXIT_FAILURE;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        return EXIT_FAILURE;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    SDL_Window *window = SDL_CreateWindow("BUDOSTACK Terminal",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          1280,
                                          720,
                                          SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        return EXIT_FAILURE;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        return EXIT_FAILURE;
    }

    SDL_Texture *glyph_textures[256];
    for (size_t i = 0u; i < 256u; i++) {
        glyph_textures[i] = create_glyph_texture(renderer, &font, (uint32_t)i);
        if (!glyph_textures[i]) {
            fprintf(stderr, "Failed to create glyph texture for %zu: %s\n", i, SDL_GetError());
            for (size_t j = 0u; j < i; j++) {
                SDL_DestroyTexture(glyph_textures[j]);
            }
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            kill(child_pid, SIGKILL);
            free_font(&font);
            close(master_fd);
            return EXIT_FAILURE;
        }
    }

    int width = 0;
    int height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) {
        fprintf(stderr, "SDL_GetRendererOutputSize failed: %s\n", SDL_GetError());
        for (size_t i = 0u; i < 256u; i++) {
            SDL_DestroyTexture(glyph_textures[i]);
        }
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        return EXIT_FAILURE;
    }

    size_t columns = (size_t)(width / (int)font.width);
    size_t rows = (size_t)(height / (int)font.height);
    if (columns == 0u) {
        columns = 1u;
    }
    if (rows == 0u) {
        rows = 1u;
    }

    struct terminal_buffer buffer = {0};
    if (terminal_buffer_init(&buffer, columns, rows) != 0) {
        fprintf(stderr, "Failed to allocate terminal buffer.\n");
        for (size_t i = 0u; i < 256u; i++) {
            SDL_DestroyTexture(glyph_textures[i]);
        }
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        kill(child_pid, SIGKILL);
        free_font(&font);
        close(master_fd);
        return EXIT_FAILURE;
    }

    update_pty_size(master_fd, columns, rows);

    SDL_StartTextInput();

    int status = 0;
    int child_exited = 0;
    unsigned char input_buffer[512];
    int running = 1;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                int new_width = event.window.data1;
                int new_height = event.window.data2;
                size_t new_columns = new_width > 0 ? (size_t)(new_width / (int)font.width) : buffer.columns;
                size_t new_rows = new_height > 0 ? (size_t)(new_height / (int)font.height) : buffer.rows;
                if (new_columns == 0u) {
                    new_columns = 1u;
                }
                if (new_rows == 0u) {
                    new_rows = 1u;
                }
                if (new_columns != buffer.columns || new_rows != buffer.rows) {
                    update_terminal_geometry(&buffer, new_columns, new_rows);
                    update_pty_size(master_fd, buffer.columns, buffer.rows);
                    terminal_buffer_reset(&buffer);
                }
            } else if (event.type == SDL_KEYDOWN) {
                SDL_Keycode sym = event.key.keysym.sym;
                SDL_Keymod mod = event.key.keysym.mod;
                unsigned char ch = 0u;
                int handled = 0;
                if ((mod & KMOD_CTRL) != 0) {
                    if (sym >= 'a' && sym <= 'z') {
                        ch = (unsigned char)(sym - 'a' + 1);
                        handled = 1;
                    } else if (sym >= 'A' && sym <= 'Z') {
                        ch = (unsigned char)(sym - 'A' + 1);
                        handled = 1;
                    }
                }
                if (!handled) {
                    switch (sym) {
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        ch = '\r';
                        handled = 1;
                        break;
                    case SDLK_BACKSPACE:
                        ch = '\b';
                        handled = 1;
                        break;
                    case SDLK_ESCAPE:
                        running = 0;
                        handled = 0;
                        break;
                    default:
                        break;
                    }
                }
                if (handled && ch != 0u) {
                    if (safe_write(master_fd, &ch, 1u) < 0) {
                        running = 0;
                    }
                }
            } else if (event.type == SDL_TEXTINPUT) {
                const char *text = event.text.text;
                size_t len = strlen(text);
                if (len > 0u) {
                    if (safe_write(master_fd, text, len) < 0) {
                        running = 0;
                    }
                }
            }
        }

        ssize_t bytes_read;
        do {
            bytes_read = read(master_fd, input_buffer, sizeof(input_buffer));
            if (bytes_read > 0) {
                for (ssize_t i = 0; i < bytes_read; i++) {
                    terminal_put_char(&buffer, input_buffer[i]);
                }
            } else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                running = 0;
                break;
            }
        } while (bytes_read > 0);

        pid_t wait_result = waitpid(child_pid, &status, WNOHANG);
        if (wait_result == child_pid) {
            child_exited = 1;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        for (size_t row = 0u; row < buffer.rows; row++) {
            for (size_t col = 0u; col < buffer.columns; col++) {
                uint32_t ch = buffer.cells[row * buffer.columns + col].ch;
                if (ch == 0u || ch >= 256u) {
                    continue;
                }
                SDL_Rect dst = {(int)(col * font.width), (int)(row * font.height), (int)font.width, (int)font.height};
                SDL_RenderCopy(renderer, glyph_textures[ch], NULL, &dst);
            }
        }

        SDL_RenderPresent(renderer);

        if (child_exited) {
            running = 0;
        }

        SDL_Delay(16);
    }

    SDL_StopTextInput();

    if (!child_exited) {
        kill(child_pid, SIGTERM);
        waitpid(child_pid, &status, 0);
    }

    terminal_buffer_free(&buffer);

    for (size_t i = 0u; i < 256u; i++) {
        SDL_DestroyTexture(glyph_textures[i]);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    free_font(&font);
    close(master_fd);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}

#else

int main(void) {
    fprintf(stderr, "BUDOSTACK terminal requires SDL2 development headers to build.\n");
    fprintf(stderr, "Please install SDL2 and rebuild to use this application.\n");
    return EXIT_FAILURE;
}

#endif
