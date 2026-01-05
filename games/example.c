#define _POSIX_C_SOURCE 200809L

#include "budo_input.h"
#include "budo_sound.h"
#include "budo_video.h"

#include <stdint.h>
#include <stdio.h>
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

static void draw_player_block(int x, int y, uint32_t color) {
    budo_video_put_pixel(x, y, color);
    budo_video_put_pixel(x + 1, y, color);
    budo_video_put_pixel(x, y + 1, color);
    budo_video_put_pixel(x + 1, y + 1, color);
}

int main(void) {
    int player_x;
    int player_y;
    int running = 1;
    int use_tone = 0;
    budo_video_mode_t video_mode = BUDO_VIDEO_LOW;
    int width = 0;
    int height = 0;

    if (budo_video_init(video_mode, "BUDO Example", 1) != 0) {
        fprintf(stderr, "example: failed to initialize video.\n");
        return 1;
    }
    if (budo_input_init() != 0) {
        budo_video_shutdown();
        fprintf(stderr, "example: failed to initialize input.\n");
        return 1;
    }
    budo_input_enable_mouse(1);

    if (budo_sound_init(44100) == 0) {
        use_tone = 1;
    }

    if (budo_video_get_size(&width, &height) != 0) {
        budo_input_shutdown();
        budo_video_shutdown();
        fprintf(stderr, "example: failed to query video size.\n");
        return 1;
    }

    player_x = width / 2;
    player_y = height / 2;

    if (use_tone) {
        budo_sound_play_tone(440, 120, 60);
    } else {
        budo_sound_beep(1, 0);
    }

    while (running) {
        budo_input_state_t state;

        while (budo_input_poll_state(&state)) {
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
                if (use_tone) {
                    budo_sound_play_tone(880, 80, 70);
                } else {
                    budo_sound_beep(1, 0);
                }
                video_mode = (video_mode == BUDO_VIDEO_LOW) ? BUDO_VIDEO_HIGH : BUDO_VIDEO_LOW;
                budo_video_shutdown();
                if (budo_video_init(video_mode, "BUDO Example", 1) != 0) {
                    running = 0;
                }
                budo_video_get_size(&width, &height);
            }
            if (state.mouse_buttons) {
                player_x = state.mouse_x - 1;
                player_y = state.mouse_y * 2 - 2;
            }
        }

        clamp_position(&player_x, 0, width - 2);
        clamp_position(&player_y, 0, height - 2);

        budo_video_clear(0xff101010u);
        draw_player_block(player_x, player_y, 0xffe0e0e0u);
        budo_video_present();

        sleep_ms(16);
    }

    if (use_tone) {
        budo_sound_shutdown();
    }
    budo_input_enable_mouse(0);
    budo_input_shutdown();
    budo_video_shutdown();
    return 0;
}
