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

int main(int argc, char *argv[]) {
    const char *title = "BUDOSTACK SDL Window";
    int fullscreen = 1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-title") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "sdlWindow: missing value for -title\n");
                print_usage();
                return EXIT_FAILURE;
            }
            title = argv[i];
        } else if (strcmp(argv[i], "-fullscreen") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "sdlWindow: missing value for -fullscreen\n");
                print_usage();
                return EXIT_FAILURE;
            }
            if (parse_fullscreen(argv[i], &fullscreen) != 0) {
                fprintf(stderr,
                        "sdlWindow: invalid value for -fullscreen, expected yes or no\n");
                print_usage();
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "sdlWindow: unknown argument '%s'\n", argv[i]);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "sdlWindow: SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    Uint32 window_flags = SDL_WINDOW_SHOWN;
    int width = 1280;
    int height = 720;

    if (fullscreen) {
        window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    } else {
        window_flags |= SDL_WINDOW_RESIZABLE;
    }

    SDL_Window *window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        window_flags);

    if (window == NULL) {
        fprintf(stderr, "sdlWindow: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    Uint32 renderer_flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, renderer_flags);
    if (renderer == NULL) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }

    if (renderer == NULL) {
        fprintf(stderr, "sdlWindow: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE) != 0) {
        fprintf(stderr, "sdlWindow: SDL_SetRenderDrawColor failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    if (SDL_RenderClear(renderer) != 0) {
        fprintf(stderr, "sdlWindow: SDL_RenderClear failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
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

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}
