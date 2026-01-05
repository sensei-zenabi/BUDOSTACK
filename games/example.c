#define _POSIX_C_SOURCE 200809L

#include "budo_graphics.h"
#include "budo_input.h"
#include "budo_sound.h"
#include "budo_video.h"

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

static void draw_player_block(int x, int y) {
    budo_video_put_pixel(x, y, 0xffffffff);
    budo_video_put_pixel(x + 1, y, 0xffffffff);
    budo_video_put_pixel(x, y + 1, 0xffffffff);
    budo_video_put_pixel(x + 1, y + 1, 0xffffffff);
}

int main(void) {
    struct winsize size;
    int cols = 80;
    int rows = 24;
    int player_x;
    int player_y;
    int running = 1;
    int use_sdl = 0;
    budo_video_mode_t video_mode = BUDO_VIDEO_LOW;

    if (budo_video_init(video_mode, "BUDO Example", 2) == 0) {
        use_sdl = 1;
        if (budo_input_sdl_init() != 0) {
            budo_video_shutdown();
            use_sdl = 0;
        }
        if (use_sdl && budo_sound_init(44100) != 0) {
            budo_input_sdl_shutdown();
            budo_video_shutdown();
            use_sdl = 0;
        }
    }

    if (!use_sdl) {
        if (budo_gfx_init() != 0) {
            fprintf(stderr, "example: failed to initialize graphics.\n");
            return 1;
        }
        if (budo_input_init() != 0) {
            budo_gfx_shutdown();
            fprintf(stderr, "example: failed to initialize input.\n");
            return 1;
        }
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

    if (use_sdl) {
        budo_sound_play_tone(440, 120, 60);
    } else {
        budo_sound_beep(1, 0);
    }

    while (running) {
        if (use_sdl) {
            budo_input_state_t state;
            int width = 0;
            int height = 0;

            while (budo_input_sdl_poll(&state)) {
                if (state.quit_requested) {
                    running = 0;
                }
                if (state.key_up) {
                    player_y--;
                }
                if (state.key_down) {
                    player_y++;
                }
                if (state.key_left) {
                    player_x--;
                }
                if (state.key_right) {
                    player_x++;
                }
                if (state.key_space) {
                    budo_sound_play_tone(880, 80, 70);
                    budo_video_shutdown();
                    video_mode = (video_mode == BUDO_VIDEO_LOW) ? BUDO_VIDEO_HIGH : BUDO_VIDEO_LOW;
                    if (budo_video_init(video_mode, "BUDO Example", 2) != 0) {
                        running = 0;
                    }
                }
                if (state.mouse_buttons) {
                    player_x = state.mouse_x;
                    player_y = state.mouse_y;
                }
            }

            if (budo_video_get_size(&width, &height) == 0) {
                clamp_position(&player_x, 0, width - 2);
                clamp_position(&player_y, 0, height - 2);
            }

            budo_video_clear(0xff101010u);
            draw_player_block(player_x, player_y);
            budo_video_present();
        } else {
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
            budo_gfx_draw_text(2, 1, "BUDO Example: arrows move, space beeps, q quits.");
            budo_gfx_reset_color();
            budo_gfx_draw_text(2, 2, "SDL2 optional; running in terminal fallback mode.");
            budo_gfx_draw_text(player_x, player_y, "@");
            budo_gfx_present();
        }

        sleep_ms(16);
    }

    if (use_sdl) {
        budo_sound_shutdown();
        budo_input_sdl_shutdown();
        budo_video_shutdown();
    } else {
        budo_input_shutdown();
        budo_gfx_shutdown();
    }
    return 0;
}
