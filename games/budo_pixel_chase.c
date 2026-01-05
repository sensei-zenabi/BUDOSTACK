#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "../budo/budo_graphics.h"
#include "../budo/budo_input.h"

#define BUDO_SCREEN_WIDTH 320
#define BUDO_SCREEN_HEIGHT 200
#define BUDO_LAYER 8
#define PLAYER_SIZE 10
#define TARGET_SIZE 8
#define STEP_SIZE 4

static void fill_sprite(uint8_t *buffer, int width, int height, uint8_t r, uint8_t g, uint8_t b) {
    if (!buffer || width <= 0 || height <= 0) {
        return;
    }

    size_t total = (size_t)width * (size_t)height;
    for (size_t i = 0u; i < total; i++) {
        size_t idx = i * 4u;
        buffer[idx] = r;
        buffer[idx + 1u] = g;
        buffer[idx + 2u] = b;
        buffer[idx + 3u] = 255u;
    }
}

static int clamp(int value, int min, int max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static void randomize_target(int *x, int *y) {
    if (!x || !y) {
        return;
    }

    int max_x = BUDO_SCREEN_WIDTH - TARGET_SIZE;
    int max_y = BUDO_SCREEN_HEIGHT - TARGET_SIZE;
    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }

    *x = rand() % (max_x + 1);
    *y = rand() % (max_y + 1);
}

static void sleep_frame(void) {
    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = 16000000;

    while (nanosleep(&req, &req) == -1 && errno == EINTR) {
    }
}

int main(void) {
    if (budo_input_init() != 0) {
        fprintf(stderr, "budo_pixel_chase: failed to initialize input\n");
        return EXIT_FAILURE;
    }

    if (budo_graphics_set_resolution(BUDO_SCREEN_WIDTH, BUDO_SCREEN_HEIGHT) != 0) {
        fprintf(stderr, "budo_pixel_chase: failed to set resolution\n");
        return EXIT_FAILURE;
    }
    if (budo_graphics_clear_screen(BUDO_SCREEN_WIDTH, BUDO_SCREEN_HEIGHT, BUDO_LAYER) != 0) {
        fprintf(stderr, "budo_pixel_chase: failed to clear screen\n");
        return EXIT_FAILURE;
    }
    if (budo_graphics_render_layer(BUDO_LAYER) != 0) {
        fprintf(stderr, "budo_pixel_chase: failed to render clear\n");
        return EXIT_FAILURE;
    }

    uint8_t player_sprite[PLAYER_SIZE * PLAYER_SIZE * 4u];
    uint8_t target_sprite[TARGET_SIZE * TARGET_SIZE * 4u];
    fill_sprite(player_sprite, PLAYER_SIZE, PLAYER_SIZE, 40u, 200u, 120u);
    fill_sprite(target_sprite, TARGET_SIZE, TARGET_SIZE, 240u, 200u, 40u);

    srand((unsigned int)time(NULL));

    int player_x = BUDO_SCREEN_WIDTH / 2;
    int player_y = BUDO_SCREEN_HEIGHT / 2;
    int target_x = 0;
    int target_y = 0;
    randomize_target(&target_x, &target_y);

    int prev_player_x = player_x;
    int prev_player_y = player_y;
    int prev_target_x = target_x;
    int prev_target_y = target_y;

    int running = 1;
    while (running) {
        struct budo_input_event event;
        for (int i = 0; i < 4; i++) {
            if (!budo_input_poll(&event)) {
                break;
            }

            if (event.key == BUDO_KEY_ESCAPE) {
                running = 0;
                break;
            }

            if (event.key == BUDO_KEY_UP) {
                player_y -= STEP_SIZE;
            } else if (event.key == BUDO_KEY_DOWN) {
                player_y += STEP_SIZE;
            } else if (event.key == BUDO_KEY_LEFT) {
                player_x -= STEP_SIZE;
            } else if (event.key == BUDO_KEY_RIGHT) {
                player_x += STEP_SIZE;
            } else if (event.key == BUDO_KEY_CHAR) {
                if (event.ch == 'q' || event.ch == 'Q') {
                    running = 0;
                    break;
                }
                if (event.ch == 'w' || event.ch == 'W') {
                    player_y -= STEP_SIZE;
                } else if (event.ch == 's' || event.ch == 'S') {
                    player_y += STEP_SIZE;
                } else if (event.ch == 'a' || event.ch == 'A') {
                    player_x -= STEP_SIZE;
                } else if (event.ch == 'd' || event.ch == 'D') {
                    player_x += STEP_SIZE;
                }
            }
        }

        player_x = clamp(player_x, 0, BUDO_SCREEN_WIDTH - PLAYER_SIZE);
        player_y = clamp(player_y, 0, BUDO_SCREEN_HEIGHT - PLAYER_SIZE);

        int overlap = (player_x < target_x + TARGET_SIZE) &&
                      (player_x + PLAYER_SIZE > target_x) &&
                      (player_y < target_y + TARGET_SIZE) &&
                      (player_y + PLAYER_SIZE > target_y);
        if (overlap) {
            randomize_target(&target_x, &target_y);
        }

        if (budo_graphics_clear_rect(prev_player_x, prev_player_y, PLAYER_SIZE, PLAYER_SIZE, BUDO_LAYER) != 0) {
            fprintf(stderr, "budo_pixel_chase: failed to clear player sprite\n");
            return EXIT_FAILURE;
        }
        if (budo_graphics_clear_rect(prev_target_x, prev_target_y, TARGET_SIZE, TARGET_SIZE, BUDO_LAYER) != 0) {
            fprintf(stderr, "budo_pixel_chase: failed to clear target sprite\n");
            return EXIT_FAILURE;
        }

        if (budo_graphics_draw_sprite_rgba(player_x,
                                           player_y,
                                           PLAYER_SIZE,
                                           PLAYER_SIZE,
                                           player_sprite,
                                           BUDO_LAYER) != 0) {
            fprintf(stderr, "budo_pixel_chase: failed to draw player sprite\n");
            return EXIT_FAILURE;
        }
        if (budo_graphics_draw_sprite_rgba(target_x,
                                           target_y,
                                           TARGET_SIZE,
                                           TARGET_SIZE,
                                           target_sprite,
                                           BUDO_LAYER) != 0) {
            fprintf(stderr, "budo_pixel_chase: failed to draw target sprite\n");
            return EXIT_FAILURE;
        }

        if (budo_graphics_render_layer(BUDO_LAYER) != 0) {
            fprintf(stderr, "budo_pixel_chase: failed to render layer\n");
            return EXIT_FAILURE;
        }

        prev_player_x = player_x;
        prev_player_y = player_y;
        prev_target_x = target_x;
        prev_target_y = target_y;

        sleep_frame();
    }

    budo_input_shutdown();
    if (budo_graphics_clear_rect(0, 0, BUDO_SCREEN_WIDTH, BUDO_SCREEN_HEIGHT, BUDO_LAYER) != 0) {
        fprintf(stderr, "budo_pixel_chase: failed to clear screen\n");
        return EXIT_FAILURE;
    }
    if (budo_graphics_render_layer(BUDO_LAYER) != 0) {
        fprintf(stderr, "budo_pixel_chase: failed to render clear\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
