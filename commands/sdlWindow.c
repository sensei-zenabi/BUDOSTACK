#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#define MAX_TITLE_LEN 256
#define SOCKET_DIR_SUFFIX "/.budostack/sdl"
#define SOCKET_NAME_FMT "%s/%llu.sock"

static volatile sig_atomic_t g_running = 1;

static void handle_signal(int signo) {
    (void)signo;
    g_running = 0;
}

static void log_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "sdlWindow: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static int ensure_runtime_dir(char *path, size_t path_size) {
    const char *home = getenv("HOME");
    if (home == NULL) {
        log_error("HOME environment variable not set");
        return -1;
    }

    if (snprintf(path, path_size, "%s%s", home, SOCKET_DIR_SUFFIX) >= (int)path_size) {
        log_error("socket directory path too long");
        return -1;
    }

    struct stat st;
    if (stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            log_error("%s exists and is not a directory", path);
            return -1;
        }
        return 0;
    }

    if (errno != ENOENT) {
        log_error("stat failed on %s: %s", path, strerror(errno));
        return -1;
    }

    if (mkdir(path, 0700) != 0) {
        log_error("failed to create %s: %s", path, strerror(errno));
        return -1;
    }

    return 0;
}

static unsigned long long generate_id(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        ts.tv_sec = time(NULL);
        ts.tv_nsec = (long)getpid();
    }

    unsigned long long id = (unsigned long long)ts.tv_sec ^ ((unsigned long long)ts.tv_nsec << 21);
    id ^= (unsigned long long)getpid();
    id ^= ((unsigned long long)rand() << 13);
    if (id == 0) {
        id = 1ULL;
    }
    return id;
}

static void close_and_unlink(int fd, const char *socket_path) {
    if (fd >= 0) {
        close(fd);
    }
    if (socket_path != NULL) {
        unlink(socket_path);
    }
}

static int parse_bool(const char *value, bool *out) {
    if (strcasecmp(value, "yes") == 0 || strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        *out = true;
        return 0;
    }
    if (strcasecmp(value, "no") == 0 || strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        *out = false;
        return 0;
    }
    return -1;
}

static void gather_text_argument(int *index, int argc, char *argv[], char **out_text) {
    free(*out_text);
    *out_text = NULL;

    size_t len = 0;
    int suppress_space = 0;

    for (int i = *index; i < argc; ++i) {
        int is_option = 0;
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-title") == 0 || strcmp(argv[i], "-fullscreen") == 0 ||
                strcmp(argv[i], "-width") == 0 || strcmp(argv[i], "-height") == 0) {
                is_option = 1;
            }
        } else if (strcasecmp(argv[i], "to") == 0 || strcasecmp(argv[i], "TO") == 0) {
            is_option = 1;
        }

        if (is_option) {
            if (len == 0) {
                log_error("missing value for -title");
                free(*out_text);
                *out_text = NULL;
                return;
            }
            *index = i - 1;
            break;
        }

        if (strcmp(argv[i], "+") == 0) {
            if (suppress_space) {
                log_error("consecutive '+' tokens in -title");
                free(*out_text);
                *out_text = NULL;
                return;
            }
            suppress_space = 1;
            continue;
        }

        size_t add_len = strlen(argv[i]);
        int need_space = len > 0 && suppress_space == 0;
        char *new_text = realloc(*out_text, len + add_len + (need_space ? 1 : 0) + 1);
        if (new_text == NULL) {
            log_error("out of memory while parsing -title");
            free(*out_text);
            *out_text = NULL;
            return;
        }
        *out_text = new_text;
        if (need_space) {
            (*out_text)[len++] = ' ';
        }
        memcpy(*out_text + len, argv[i], add_len);
        len += add_len;
        (*out_text)[len] = '\0';
        suppress_space = 0;
    }

    if (*out_text == NULL || (*out_text)[0] == '\0') {
        log_error("missing value for -title");
        free(*out_text);
        *out_text = NULL;
        return;
    }

    if (suppress_space) {
        log_error("dangling '+' at end of -title value");
        free(*out_text);
        *out_text = NULL;
    }
}

static int setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        log_error("sigaction(SIGINT) failed: %s", strerror(errno));
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        log_error("sigaction(SIGTERM) failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static void child_main(const char *socket_path, int ready_fd, const char *title, bool fullscreen,
                       int width, int height) {
    if (setup_signals() != 0) {
        (void)write(ready_fd, "0", 1);
        close(ready_fd);
        _exit(EXIT_FAILURE);
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        log_error("SDL_Init failed: %s", SDL_GetError());
        (void)write(ready_fd, "0", 1);
        close(ready_fd);
        _exit(EXIT_FAILURE);
    }

    int img_flags = IMG_INIT_PNG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags) {
        log_error("IMG_Init failed: %s", IMG_GetError());
        (void)write(ready_fd, "0", 1);
        close(ready_fd);
        SDL_Quit();
        _exit(EXIT_FAILURE);
    }

    if (TTF_Init() != 0) {
        log_error("TTF_Init failed: %s", TTF_GetError());
        (void)write(ready_fd, "0", 1);
        close(ready_fd);
        IMG_Quit();
        SDL_Quit();
        _exit(EXIT_FAILURE);
    }

    Uint32 window_flags = SDL_WINDOW_SHOWN;
    if (fullscreen) {
        window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    SDL_Window *window = SDL_CreateWindow(title ? title : "BUDOSTACK",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          width,
                                          height,
                                          window_flags);
    if (window == NULL) {
        log_error("SDL_CreateWindow failed: %s", SDL_GetError());
        (void)write(ready_fd, "0", 1);
        close(ready_fd);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        _exit(EXIT_FAILURE);
    }

    if (fullscreen) {
        if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            log_error("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
        }
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (renderer == NULL) {
        log_error("SDL_CreateRenderer failed: %s", SDL_GetError());
        (void)write(ready_fd, "0", 1);
        close(ready_fd);
        SDL_DestroyWindow(window);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        _exit(EXIT_FAILURE);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_error("socket creation failed: %s", strerror(errno));
        (void)write(ready_fd, "0", 1);
        close(ready_fd);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        _exit(EXIT_FAILURE);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    unlink(socket_path);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        log_error("bind failed: %s", strerror(errno));
        (void)write(ready_fd, "0", 1);
        close(ready_fd);
        close_and_unlink(server_fd, socket_path);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        _exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 4) != 0) {
        log_error("listen failed: %s", strerror(errno));
        (void)write(ready_fd, "0", 1);
        close(ready_fd);
        close_and_unlink(server_fd, socket_path);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        _exit(EXIT_FAILURE);
    }

    const char *font_path = NULL;
    char exe_path[PATH_MAX];
    ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (exe_len > 0) {
        exe_path[exe_len] = '\0';
        char *last_slash = strrchr(exe_path, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
            static char font_buf[PATH_MAX];
            if (snprintf(font_buf, sizeof(font_buf), "%s/../fonts/ModernDOS8x8.ttf", exe_path) < (int)sizeof(font_buf)) {
                font_path = font_buf;
            }
        }
    }

    TTF_Font *font = NULL;
    if (font_path != NULL) {
        font = TTF_OpenFont(font_path, 18);
        if (font == NULL) {
            log_error("TTF_OpenFont failed for %s: %s", font_path, TTF_GetError());
        }
    } else {
        log_error("could not locate font path");
    }

    char ready = '1';
    if (write(ready_fd, &ready, 1) != 1) {
        log_error("failed to notify parent about readiness");
    }
    close(ready_fd);

    while (g_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        int max_fd = server_fd;

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 16000;

        int sel = select(max_fd + 1, &readfds, NULL, NULL, &tv);
        if (sel < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_error("select failed: %s", strerror(errno));
            break;
        }

        if (FD_ISSET(server_fd, &readfds)) {
            int client_fd = accept(server_fd, NULL, NULL);
            if (client_fd >= 0) {
                char buffer[4096];
                ssize_t total = 0;
                while (total < (ssize_t)(sizeof(buffer) - 1)) {
                    ssize_t n = read(client_fd, buffer + total, sizeof(buffer) - 1 - total);
                    if (n > 0) {
                        total += n;
                        if (buffer[total - 1] == '\n') {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                buffer[total] = '\0';

                const char *response = "OK";
                if (total > 0) {
                    if (strncmp(buffer, "DRAW_SPRITE|", 12) == 0) {
                        char *cursor = buffer + 12;
                        char *x_token = strsep(&cursor, "|");
                        char *y_token = strsep(&cursor, "|");
                        char *path_token = cursor;
                        if (x_token && y_token && path_token) {
                            int x = atoi(x_token);
                            int y = atoi(y_token);
                            size_t len = strlen(path_token);
                            if (len > 0 && path_token[len - 1] == '\n') {
                                path_token[len - 1] = '\0';
                            }
                            SDL_Surface *surface = IMG_Load(path_token);
                            if (surface == NULL) {
                                log_error("IMG_Load failed for %s: %s", path_token, IMG_GetError());
                                response = "ERR";
                            } else {
                                SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
                                if (texture == NULL) {
                                    log_error("SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
                                    response = "ERR";
                                } else {
                                    SDL_Rect dst = {x, y, surface->w, surface->h};
                                    if (SDL_RenderCopy(renderer, texture, NULL, &dst) != 0) {
                                        log_error("SDL_RenderCopy failed: %s", SDL_GetError());
                                        response = "ERR";
                                    }
                                    SDL_DestroyTexture(texture);
                                }
                                SDL_FreeSurface(surface);
                            }
                        } else {
                            response = "ERR";
                        }
                    } else if (strncmp(buffer, "TEXT|", 5) == 0) {
                        char *cursor = buffer + 5;
                        char *x_token = strsep(&cursor, "|");
                        char *y_token = strsep(&cursor, "|");
                        char *text_token = cursor;
                        if (x_token && y_token && text_token) {
                            int x = atoi(x_token);
                            int y = atoi(y_token);
                            size_t len = strlen(text_token);
                            if (len > 0 && text_token[len - 1] == '\n') {
                                text_token[len - 1] = '\0';
                            }
                            if (font == NULL) {
                                log_error("font not available for text rendering");
                                response = "ERR";
                            } else {
                                SDL_Color color = {255, 255, 255, 255};
                                SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text_token, color);
                                if (surface == NULL) {
                                    log_error("TTF_RenderUTF8_Blended failed: %s", TTF_GetError());
                                    response = "ERR";
                                } else {
                                    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
                                    if (texture == NULL) {
                                        log_error("SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
                                        response = "ERR";
                                    } else {
                                        SDL_Rect dst = {x, y, surface->w, surface->h};
                                        if (SDL_RenderCopy(renderer, texture, NULL, &dst) != 0) {
                                            log_error("SDL_RenderCopy failed: %s", SDL_GetError());
                                            response = "ERR";
                                        }
                                        SDL_DestroyTexture(texture);
                                    }
                                    SDL_FreeSurface(surface);
                                }
                            }
                        } else {
                            response = "ERR";
                        }
                    } else if (strncmp(buffer, "RENDER", 6) == 0) {
                        SDL_RenderPresent(renderer);
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                        SDL_RenderClear(renderer);
                    } else if (strncmp(buffer, "QUIT", 4) == 0) {
                        g_running = 0;
                    } else {
                        response = "ERR";
                    }
                }

                (void)write(client_fd, response, strlen(response));
                close(client_fd);
            }
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                g_running = 0;
            }
        }
    }

    close_and_unlink(server_fd, socket_path);
    if (font) {
        TTF_CloseFont(font);
    }
    TTF_Quit();
    IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    _exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    char *title = NULL;
    bool have_title = false;
    bool fullscreen = false;
    bool have_fullscreen = false;
    int width = 1280;
    int height = 720;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-title") == 0) {
            if (++i >= argc) {
                log_error("missing value for -title");
                goto fail;
            }
            have_title = true;
            gather_text_argument(&i, argc, argv, &title);
            if (title == NULL) {
                goto fail;
            }
        } else if (strcmp(argv[i], "-fullscreen") == 0) {
            if (++i >= argc) {
                log_error("missing value for -fullscreen");
                goto fail;
            }
            if (parse_bool(argv[i], &fullscreen) != 0) {
                log_error("invalid value for -fullscreen: %s", argv[i]);
                goto fail;
            }
            have_fullscreen = true;
        } else if (strcmp(argv[i], "-width") == 0) {
            if (++i >= argc) {
                log_error("missing value for -width");
                goto fail;
            }
            char *end;
            long w = strtol(argv[i], &end, 10);
            if (*end != '\0' || w <= 0 || w > 10000) {
                log_error("invalid width value: %s", argv[i]);
                goto fail;
            }
            width = (int)w;
        } else if (strcmp(argv[i], "-height") == 0) {
            if (++i >= argc) {
                log_error("missing value for -height");
                goto fail;
            }
            char *end;
            long h = strtol(argv[i], &end, 10);
            if (*end != '\0' || h <= 0 || h > 10000) {
                log_error("invalid height value: %s", argv[i]);
                goto fail;
            }
            height = (int)h;
        } else if (strcasecmp(argv[i], "to") == 0 || strcasecmp(argv[i], "TO") == 0) {
            break;
        } else {
            log_error("unknown argument: %s", argv[i]);
            goto fail;
        }
    }

    if (!have_title) {
        log_error("-title is required");
        goto fail;
    }
    (void)have_fullscreen;

    char runtime_dir[PATH_MAX];
    if (ensure_runtime_dir(runtime_dir, sizeof(runtime_dir)) != 0) {
        goto fail;
    }

    srand((unsigned int)(time(NULL) ^ getpid()));
    unsigned long long id = generate_id();

    char socket_path[PATH_MAX];
    if (snprintf(socket_path, sizeof(socket_path), SOCKET_NAME_FMT, runtime_dir, id) >= (int)sizeof(socket_path)) {
        log_error("socket path too long");
        goto fail;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        log_error("pipe failed: %s", strerror(errno));
        goto fail;
    }

    pid_t pid = fork();
    if (pid < 0) {
        log_error("fork failed: %s", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        goto fail;
    }

    if (pid == 0) {
        close(pipefd[0]);
        child_main(socket_path, pipefd[1], title, fullscreen, width, height);
        _exit(EXIT_FAILURE);
    }

    close(pipefd[1]);
    char ready_flag = '0';
    ssize_t r = read(pipefd[0], &ready_flag, 1);
    close(pipefd[0]);

    if (r != 1 || ready_flag != '1') {
        log_error("failed to start SDL window process");
        goto fail;
    }

    printf("%llu\n", id);
    fflush(stdout);

    free(title);
    return EXIT_SUCCESS;

fail:
    free(title);
    return EXIT_FAILURE;
}
