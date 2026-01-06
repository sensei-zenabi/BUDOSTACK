#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
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

#if BUDOSTACK_HAVE_SDL2

#define DEFAULT_WIDTH 160
#define DEFAULT_HEIGHT 120
#define GRID_COLUMNS 40
#define GRID_ROWS 30
#define MAX_SNAKE_CELLS (GRID_COLUMNS * GRID_ROWS)
#define MOVE_INTERVAL_MS 100u
#define WINDOW_SCALE 4

struct snake_cell {
    int x;
    int y;
};

static int parse_env_int(const char *value, int fallback) {
    if (!value || value[0] == '\0') {
        return fallback;
    }
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);
    if (errno != 0 || endptr == value || *endptr != '\0') {
        return fallback;
    }
    if (parsed <= 0 || parsed > INT_MAX) {
        return fallback;
    }
    return (int)parsed;
}

static void fill_rect(uint8_t *buffer,
                      int width,
                      int height,
                      int stride,
                      int x,
                      int y,
                      int w,
                      int h,
                      uint8_t r,
                      uint8_t g,
                      uint8_t b) {
    if (!buffer || width <= 0 || height <= 0 || stride <= 0) {
        return;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > width) {
        x1 = width;
    }
    if (y1 > height) {
        y1 = height;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    for (int py = y0; py < y1; py++) {
        uint8_t *row = buffer + (size_t)py * (size_t)stride;
        for (int px = x0; px < x1; px++) {
            size_t offset = (size_t)px * 4u;
            row[offset] = r;
            row[offset + 1u] = g;
            row[offset + 2u] = b;
            row[offset + 3u] = 255u;
        }
    }
}

static int write_frame(int fd, const uint8_t *buffer, size_t size) {
    size_t offset = 0u;
    while (offset < size) {
        ssize_t written = write(fd, buffer + offset, size - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            return -1;
        }
        offset += (size_t)written;
    }
    return 0;
}

static void place_food(struct snake_cell *food, const struct snake_cell *snake, size_t length) {
    if (!food) {
        return;
    }
    int attempts = GRID_COLUMNS * GRID_ROWS;
    while (attempts-- > 0) {
        int x = rand() % GRID_COLUMNS;
        int y = rand() % GRID_ROWS;
        int collision = 0;
        for (size_t i = 0u; i < length; i++) {
            if (snake[i].x == x && snake[i].y == y) {
                collision = 1;
                break;
            }
        }
        if (!collision) {
            food->x = x;
            food->y = y;
            return;
        }
    }
    food->x = GRID_COLUMNS / 2;
    food->y = GRID_ROWS / 2;
}

static const char *sdlsnake_external_command(const char *argv0) {
    static char command[PATH_MAX];
    const char *fallback = "./games/sdlsnake";
    const char *source = argv0 && argv0[0] != '\0' ? argv0 : fallback;
    if (strchr(source, ';')) {
        source = fallback;
    }
    if (snprintf(command, sizeof(command), "%s", source) >= (int)sizeof(command)) {
        snprintf(command, sizeof(command), "%s", fallback);
    }
    return command;
}

static void sdlsnake_request_external(const char *command, int width, int height) {
    char sequence[256];
    int written = snprintf(sequence,
                           sizeof(sequence),
                           "\x1b]777;external=%s;external_size=%dx%d\x07",
                           command,
                           width,
                           height);
    if (written <= 0 || (size_t)written >= sizeof(sequence)) {
        return;
    }
    (void)write(STDOUT_FILENO, sequence, (size_t)written);
}

int main(int argc, char **argv) {
    int use_fifo = 0;
    const char *fifo_path = getenv("BUDOSTACK_FRAMEBUFFER");
    if (fifo_path && fifo_path[0] != '\0') {
        use_fifo = 1;
    }

    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        printf("Usage: %s [--help]\n", argv[0] ? argv[0] : "sdlsnake");
        printf("Runs a low-resolution snake game.\n");
        printf("When launched via apps/terminal --external, renders into the FIFO framebuffer.\n");
        printf("Otherwise, opens a local SDL window.\n");
        return EXIT_SUCCESS;
    }

    int width = parse_env_int(getenv("BUDOSTACK_FRAMEBUFFER_WIDTH"), DEFAULT_WIDTH);
    int height = parse_env_int(getenv("BUDOSTACK_FRAMEBUFFER_HEIGHT"), DEFAULT_HEIGHT);
    int stride = parse_env_int(getenv("BUDOSTACK_FRAMEBUFFER_STRIDE"), width * 4);

    if (!use_fifo) {
        const char *external_capable = getenv("BUDOSTACK_EXTERNAL_CAPABLE");
        if (external_capable && external_capable[0] != '\0') {
            sdlsnake_request_external(sdlsnake_external_command(argv[0]), width, height);
            return EXIT_SUCCESS;
        }
    }

    if (width <= 0 || height <= 0 || stride < width * 4) {
        fprintf(stderr, "sdlsnake: invalid framebuffer dimensions.\n");
        return EXIT_FAILURE;
    }

    int fd = -1;
    if (use_fifo) {
        fd = open(fifo_path, O_WRONLY);
        if (fd < 0) {
            perror("sdlsnake: open framebuffer");
            return EXIT_FAILURE;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "sdlsnake: SDL_Init failed: %s\n", SDL_GetError());
        if (fd >= 0) {
            close(fd);
        }
        return EXIT_FAILURE;
    }

    int window_width = width * WINDOW_SCALE;
    int window_height = height * WINDOW_SCALE;
    if (window_width <= 0 || window_height <= 0) {
        window_width = DEFAULT_WIDTH * WINDOW_SCALE;
        window_height = DEFAULT_HEIGHT * WINDOW_SCALE;
    }

    SDL_Window *window = SDL_CreateWindow("sdlsnake",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          window_width,
                                          window_height,
                                          use_fifo ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "sdlsnake: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        if (fd >= 0) {
            close(fd);
        }
        return EXIT_FAILURE;
    }

    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    if (!use_fifo) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer) {
            fprintf(stderr, "sdlsnake: SDL_CreateRenderer failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            if (fd >= 0) {
                close(fd);
            }
            return EXIT_FAILURE;
        }
        if (SDL_RenderSetLogicalSize(renderer, width, height) != 0) {
            fprintf(stderr, "sdlsnake: SDL_RenderSetLogicalSize failed: %s\n", SDL_GetError());
        }
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!texture) {
            fprintf(stderr, "sdlsnake: SDL_CreateTexture failed: %s\n", SDL_GetError());
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            if (fd >= 0) {
                close(fd);
            }
            return EXIT_FAILURE;
        }
    }

    if ((size_t)height > SIZE_MAX / (size_t)stride) {
        fprintf(stderr, "sdlsnake: framebuffer size overflow.\n");
        if (texture) {
            SDL_DestroyTexture(texture);
        }
        if (renderer) {
            SDL_DestroyRenderer(renderer);
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        if (fd >= 0) {
            close(fd);
        }
        return EXIT_FAILURE;
    }
    size_t buffer_size = (size_t)height * (size_t)stride;
    uint8_t *buffer = malloc(buffer_size);
    if (!buffer) {
        fprintf(stderr, "sdlsnake: failed to allocate framebuffer.\n");
        if (texture) {
            SDL_DestroyTexture(texture);
        }
        if (renderer) {
            SDL_DestroyRenderer(renderer);
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        if (fd >= 0) {
            close(fd);
        }
        return EXIT_FAILURE;
    }

    int cell_width = width / GRID_COLUMNS;
    int cell_height = height / GRID_ROWS;
    if (cell_width <= 0) {
        cell_width = 1;
    }
    if (cell_height <= 0) {
        cell_height = 1;
    }

    struct snake_cell snake[MAX_SNAKE_CELLS];
    size_t snake_length = 5u;
    for (size_t i = 0u; i < snake_length; i++) {
        snake[i].x = GRID_COLUMNS / 2 - (int)i;
        snake[i].y = GRID_ROWS / 2;
    }

    struct snake_cell food;
    srand((unsigned int)time(NULL));
    place_food(&food, snake, snake_length);

    int dir_x = 1;
    int dir_y = 0;
    Uint32 last_move = SDL_GetTicks();
    int running = 1;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_KEYDOWN) {
                SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
                    running = 0;
                } else if (key == SDLK_UP && dir_y == 0) {
                    dir_x = 0;
                    dir_y = -1;
                } else if (key == SDLK_DOWN && dir_y == 0) {
                    dir_x = 0;
                    dir_y = 1;
                } else if (key == SDLK_LEFT && dir_x == 0) {
                    dir_x = -1;
                    dir_y = 0;
                } else if (key == SDLK_RIGHT && dir_x == 0) {
                    dir_x = 1;
                    dir_y = 0;
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        if (now - last_move >= MOVE_INTERVAL_MS) {
            last_move = now;
            for (size_t i = snake_length - 1u; i > 0u; i--) {
                snake[i] = snake[i - 1u];
            }
            snake[0].x += dir_x;
            snake[0].y += dir_y;

            if (snake[0].x < 0) {
                snake[0].x = GRID_COLUMNS - 1;
            } else if (snake[0].x >= GRID_COLUMNS) {
                snake[0].x = 0;
            }
            if (snake[0].y < 0) {
                snake[0].y = GRID_ROWS - 1;
            } else if (snake[0].y >= GRID_ROWS) {
                snake[0].y = 0;
            }

            if (snake[0].x == food.x && snake[0].y == food.y) {
                if (snake_length < MAX_SNAKE_CELLS) {
                    snake[snake_length] = snake[snake_length - 1u];
                    snake_length++;
                }
                place_food(&food, snake, snake_length);
            }

            for (size_t i = 1u; i < snake_length; i++) {
                if (snake[0].x == snake[i].x && snake[0].y == snake[i].y) {
                    snake_length = 5u;
                    for (size_t j = 0u; j < snake_length; j++) {
                        snake[j].x = GRID_COLUMNS / 2 - (int)j;
                        snake[j].y = GRID_ROWS / 2;
                    }
                    dir_x = 1;
                    dir_y = 0;
                    place_food(&food, snake, snake_length);
                    break;
                }
            }
        }

        memset(buffer, 0, buffer_size);

        fill_rect(buffer,
                  width,
                  height,
                  stride,
                  food.x * cell_width,
                  food.y * cell_height,
                  cell_width,
                  cell_height,
                  255u,
                  50u,
                  50u);

        for (size_t i = 0u; i < snake_length; i++) {
            fill_rect(buffer,
                      width,
                      height,
                      stride,
                      snake[i].x * cell_width,
                      snake[i].y * cell_height,
                      cell_width,
                      cell_height,
                      50u,
                      220u,
                      90u);
        }

        if (use_fifo) {
            if (write_frame(fd, buffer, buffer_size) != 0) {
                running = 0;
            }
        } else {
            if (SDL_UpdateTexture(texture, NULL, buffer, stride) != 0) {
                fprintf(stderr, "sdlsnake: SDL_UpdateTexture failed: %s\n", SDL_GetError());
                running = 0;
            } else {
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);
            }
        }

        SDL_Delay(10);
    }

    free(buffer);
    if (texture) {
        SDL_DestroyTexture(texture);
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (fd >= 0) {
        close(fd);
    }
    return EXIT_SUCCESS;
}

#else

int main(void) {
    fprintf(stderr, "sdlsnake requires SDL2 development headers to build.\n");
    return EXIT_FAILURE;
}

#endif
