#define _POSIX_C_SOURCE 200809L

#include "budo_graphics.h"
#include "budo_input.h"
#include "budo_sound.h"

#include <stdio.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static void clamp_position(int *value, int min_value, int max_value) {
    if (*value < min_value) {
        *value = min_value;
    } else if (*value > max_value) {
        *value = max_value;
    }
}

static void sleep_ms(int delay_ms) {
    struct timespec req;
    if (delay_ms < 0) {
        delay_ms = 0;
    }
    req.tv_sec = delay_ms / 1000;
    req.tv_nsec = (long)(delay_ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
}

int main(void) {
    struct winsize size;
    int cols = 80;
    int rows = 24;
    int player_x;
    int player_y;
    int running = 1;

    if (budo_gfx_init() != 0) {
        fprintf(stderr, "example: failed to initialize graphics.\n");
        return 1;
    }
    if (budo_input_init() != 0) {
        budo_gfx_shutdown();
        fprintf(stderr, "example: failed to initialize input.\n");
        return 1;
    }

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
        if (size.ws_col > 0) {
            cols = size.ws_col;
        }
        if (size.ws_row > 0) {
            rows = size.ws_row;
        }
    }

    player_x = cols / 2;
    player_y = rows / 2;

    budo_sound_beep(1, 0);

    while (running) {
        budo_key_t key;

        while (budo_input_poll(&key)) {
            switch (key) {
            case BUDO_KEY_UP:
                player_y--;
                break;
            case BUDO_KEY_DOWN:
                player_y++;
                break;
            case BUDO_KEY_LEFT:
                player_x--;
                break;
            case BUDO_KEY_RIGHT:
                player_x++;
                break;
            case BUDO_KEY_QUIT:
                running = 0;
                break;
            case BUDO_KEY_SPACE:
                budo_sound_beep(1, 0);
                break;
            default:
                break;
            }
        }

        clamp_position(&player_x, 2, cols - 1);
        clamp_position(&player_y, 3, rows - 1);

        budo_gfx_clear();
        budo_gfx_set_color(BUDO_COLOR_CYAN, BUDO_COLOR_DEFAULT);
        budo_gfx_draw_text(2, 1, "BUDO Example: use arrows to move, space to beep, q to quit.");
        budo_gfx_reset_color();
        budo_gfx_draw_text(2, 2, "Powered by budo graphics/input/sound helpers.");
        budo_gfx_draw_text(player_x, player_y, "@");
        budo_gfx_present();

        sleep_ms(16);
    }

    budo_input_shutdown();
    budo_gfx_shutdown();
    return 0;
}
