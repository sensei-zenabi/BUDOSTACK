#define _XOPEN_SOURCE 700
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "terminal_buffer.h"

#define TERMINAL_FONT_PATH "fonts/ModernDOS8x8.ttf"
#define TERMINAL_FONT_SIZE 36
#define TERMINAL_MAX_LINES 2048
#define TERMINAL_READ_CHUNK 4096
#define TERMINAL_PADDING 12

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *font;
    int char_width;
    int char_height;
    int cols;
    int rows;
} TerminalRenderer;

static int g_running = 1;

static void update_size_from_window(TerminalRenderer *renderer);

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

static int init_renderer(TerminalRenderer *renderer)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    if (TTF_Init() == -1) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return -1;
    }

    renderer->font = TTF_OpenFont(TERMINAL_FONT_PATH, TERMINAL_FONT_SIZE);
    if (!renderer->font) {
        fprintf(stderr, "Failed to open font '%s': %s\n", TERMINAL_FONT_PATH, TTF_GetError());
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    if (TTF_SizeText(renderer->font, "M", &renderer->char_width, &renderer->char_height) == -1) {
        fprintf(stderr, "TTF_SizeText failed: %s\n", TTF_GetError());
        TTF_CloseFont(renderer->font);
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    renderer->cols = 80;
    renderer->rows = 25;

    int width = renderer->cols * renderer->char_width + TERMINAL_PADDING * 2;
    int height = renderer->rows * renderer->char_height + TERMINAL_PADDING * 2;

    renderer->window = SDL_CreateWindow(
        "Budostack Terminal",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_RESIZABLE);
    if (!renderer->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        TTF_CloseFont(renderer->font);
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    renderer->renderer = SDL_CreateRenderer(renderer->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(renderer->window);
        TTF_CloseFont(renderer->font);
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    if (SDL_SetWindowFullscreen(renderer->window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
        fprintf(stderr, "SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());
    }
    update_size_from_window(renderer);
    SDL_StartTextInput();

    return 0;
}

static void destroy_renderer(TerminalRenderer *renderer)
{
    if (!renderer) {
        return;
    }

    SDL_StopTextInput();
    if (renderer->renderer) {
        SDL_DestroyRenderer(renderer->renderer);
        renderer->renderer = NULL;
    }
    if (renderer->window) {
        SDL_DestroyWindow(renderer->window);
        renderer->window = NULL;
    }
    if (renderer->font) {
        TTF_CloseFont(renderer->font);
        renderer->font = NULL;
    }
    TTF_Quit();
    SDL_Quit();
}

static void update_size_from_window(TerminalRenderer *renderer)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(renderer->window, &width, &height);

    renderer->cols = (width - TERMINAL_PADDING * 2) / renderer->char_width;
    renderer->rows = (height - TERMINAL_PADDING * 2) / renderer->char_height;
    if (renderer->cols < 20) {
        renderer->cols = 20;
    }
    if (renderer->rows < 5) {
        renderer->rows = 5;
    }
}

static int update_child_window_size(int pty_fd, const TerminalRenderer *renderer)
{
    struct winsize ws = {0};
    ws.ws_col = (unsigned short)renderer->cols;
    ws.ws_row = (unsigned short)renderer->rows;
    if (ioctl(pty_fd, TIOCSWINSZ, &ws) == -1) {
        return -1;
    }
    return 0;
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
    SDL_SetRenderDrawColor(renderer->renderer, 16, 16, 16, 255);
    SDL_RenderClear(renderer->renderer);

    SDL_Color text_color = {230, 230, 230, 255};

    size_t total_lines = terminal_buffer_line_count(buffer);
    size_t max_visible = (size_t)renderer->rows;
    size_t start_line = 0;
    if (total_lines > max_visible) {
        start_line = total_lines - max_visible;
    }

    int y = TERMINAL_PADDING;
    for (size_t i = start_line; i < total_lines; ++i) {
        const char *line_text = terminal_buffer_get_line(buffer, i);
        if (!line_text) {
            line_text = "";
        }

        if (*line_text == '\0') {
            y += renderer->char_height;
            continue;
        }

        SDL_Surface *surface = TTF_RenderUTF8_Blended(renderer->font, line_text, text_color);
        if (!surface) {
            continue;
        }

        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer->renderer, surface);
        if (texture) {
            SDL_Rect dest = {TERMINAL_PADDING, y, surface->w, surface->h};
            SDL_RenderCopy(renderer->renderer, texture, NULL, &dest);
            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);

        y += renderer->char_height;
        if (y > renderer->rows * renderer->char_height + TERMINAL_PADDING) {
            break;
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
                    g_running = 0;
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
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    update_size_from_window(&renderer);
                    update_child_window_size(pty_fd, &renderer);
                    content_dirty = 1;
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
