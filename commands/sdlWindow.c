#include <SDL2/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: sdlWindow [-title <title>] [-fullscreen yes|no]\n");
}

static int parse_fullscreen(const char *value, int *fullscreen) {
    if (value == NULL || fullscreen == NULL) {
        return -1;
    }

    char lowered[8];
    size_t length = strlen(value);
    if (length >= sizeof(lowered)) {
        return -1;
    }

    for (size_t i = 0; i <= length; ++i) {
        lowered[i] = (char)tolower((unsigned char)value[i]);
    }

    if (strcmp(lowered, "yes") == 0 || strcmp(lowered, "true") == 0 ||
        strcmp(lowered, "1") == 0) {
        *fullscreen = 1;
        return 0;
    }

    if (strcmp(lowered, "no") == 0 || strcmp(lowered, "false") == 0 ||
        strcmp(lowered, "0") == 0) {
        *fullscreen = 0;
        return 0;
    }

    return -1;
}

static int parse_title(int argc, char *argv[], int *index, char **title_out) {
    if (argv == NULL || index == NULL || title_out == NULL) {
        return -1;
    }

    int start = *index + 1;
    if (start >= argc) {
        return -1;
    }

    int end = start;
    size_t total_length = 0;

    while (end < argc) {
        if (strcmp(argv[end], "-title") == 0 || strcmp(argv[end], "-fullscreen") == 0) {
            break;
        }
        if (argv[end][0] == '-' && argv[end][1] != '\0') {
            break;
        }
        total_length += strlen(argv[end]) + 1;
        ++end;
    }

    if (end == start) {
        return -1;
    }

    char *buffer = malloc(total_length);
    if (buffer == NULL) {
        return -1;
    }

    size_t offset = 0;
    for (int i = start; i < end; ++i) {
        size_t part_length = strlen(argv[i]);
        memcpy(buffer + offset, argv[i], part_length);
        offset += part_length;
        if (i + 1 < end) {
            buffer[offset] = ' ';
            ++offset;
        }
    }
    buffer[offset] = '\0';

    *index = end - 1;
    *title_out = buffer;
    return 0;
}

int main(int argc, char *argv[]) {
    const char *title = "BUDOSTACK SDL Window";
    char *owned_title = NULL;
    int fullscreen = 1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-title") == 0) {
            char *parsed_title = NULL;
            if (parse_title(argc, argv, &i, &parsed_title) != 0) {
                fprintf(stderr, "sdlWindow: missing or invalid value for -title\n");
                print_usage();
                free(parsed_title);
                free(owned_title);
                return EXIT_FAILURE;
            }
            free(owned_title);
            owned_title = parsed_title;
            title = owned_title;
        } else if (strcmp(argv[i], "-fullscreen") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "sdlWindow: missing value for -fullscreen\n");
                print_usage();
                free(owned_title);
                return EXIT_FAILURE;
            }
            if (parse_fullscreen(argv[i], &fullscreen) != 0) {
                fprintf(stderr,
                        "sdlWindow: invalid value for -fullscreen, expected yes or no\n");
                print_usage();
                free(owned_title);
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "sdlWindow: unknown argument '%s'\n", argv[i]);
            print_usage();
            free(owned_title);
            return EXIT_FAILURE;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "sdlWindow: SDL_Init failed: %s\n", SDL_GetError());
        free(owned_title);
        return EXIT_FAILURE;
    }

    int result = EXIT_FAILURE;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    int sdl_initialized = 1;

    Uint32 window_flags = SDL_WINDOW_SHOWN;
    int width = 1280;
    int height = 720;

    if (fullscreen) {
        window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    } else {
        window_flags |= SDL_WINDOW_RESIZABLE;
    }

    window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        window_flags);

    if (window == NULL) {
        fprintf(stderr, "sdlWindow: SDL_CreateWindow failed: %s\n", SDL_GetError());
        goto cleanup;
    }

    Uint32 renderer_flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
    renderer = SDL_CreateRenderer(window, -1, renderer_flags);
    if (renderer == NULL) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }

    if (renderer == NULL) {
        fprintf(stderr, "sdlWindow: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        goto cleanup;
    }

    if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE) != 0) {
        fprintf(stderr, "sdlWindow: SDL_SetRenderDrawColor failed: %s\n", SDL_GetError());
        goto cleanup;
    }

    if (SDL_RenderClear(renderer) != 0) {
        fprintf(stderr, "sdlWindow: SDL_RenderClear failed: %s\n", SDL_GetError());
        goto cleanup;
    }

    SDL_RenderPresent(renderer);

    fprintf(stdout, "sdlWindow: window created. Close the window or press ESC to exit.\n");
    fflush(stdout);

    int running = 1;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = 0;
            } else if (event.type == SDL_WINDOWEVENT &&
                       event.window.event == SDL_WINDOWEVENT_CLOSE &&
                       event.window.windowID == SDL_GetWindowID(window)) {
                running = 0;
            }
        }
        SDL_Delay(16);
    }

    result = EXIT_SUCCESS;

cleanup:
    if (renderer != NULL) {
        SDL_DestroyRenderer(renderer);
    }
    if (window != NULL) {
        SDL_DestroyWindow(window);
    }
    if (sdl_initialized) {
        SDL_Quit();
    }
    free(owned_title);

    return result;
}
