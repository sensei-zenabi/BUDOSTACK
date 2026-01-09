#include "budo_graphics.h"
#include "budo_shader_stack.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#define GAME_WIDTH 640
#define GAME_HEIGHT 360
#define TARGET_FPS 60

#define MAP_WIDTH 16
#define MAP_HEIGHT 16

#define FOV_RADIANS 1.0471975512f
#define PLAYER_MOVE_SPEED 2.6f
#define PLAYER_TURN_SPEED 2.4f
#define PLAYER_RADIUS 0.18f

#define ENEMY_COUNT 6
#define ENEMY_SPEED 1.2f
#define ENEMY_RESPAWN_TIME 2.5f
#define ENEMY_ATTACK_RANGE 0.7f
#define ENEMY_ATTACK_COOLDOWN 0.8f

#define FIRE_COOLDOWN 0.25f
#define HIT_DAMAGE 40
#define VIEW_SAMPLE_STEP 6

struct vec2 {
    float x;
    float y;
};

struct player_state {
    struct vec2 position;
    float angle;
    int health;
};

struct enemy {
    int active;
    struct vec2 position;
    struct vec2 velocity;
    float health;
    float respawn_timer;
    float attack_timer;
};

struct raycast_hit {
    float distance;
    int side;
    int hit;
    int map_x;
    int map_y;
};

static const char *level_map[MAP_HEIGHT] = {
    "1111111111111111",
    "1000000000000001",
    "1011110111111101",
    "1010000100000101",
    "1010111101110101",
    "1010100001010101",
    "1010101111010101",
    "1010101000010101",
    "1010111011110101",
    "1010000010000101",
    "1011111010111101",
    "1000000010000001",
    "1011111110111101",
    "1010000000100101",
    "1000000000000001",
    "1111111111111111"
};

static float clamp_angle(float angle) {
    const float two_pi = 6.28318530718f;
    while (angle < 0.0f) {
        angle += two_pi;
    }
    while (angle >= two_pi) {
        angle -= two_pi;
    }
    return angle;
}

static float vec2_length(struct vec2 v) {
    return sqrtf(v.x * v.x + v.y * v.y);
}

static struct vec2 vec2_add(struct vec2 a, struct vec2 b) {
    struct vec2 out = { a.x + b.x, a.y + b.y };
    return out;
}

static struct vec2 vec2_sub(struct vec2 a, struct vec2 b) {
    struct vec2 out = { a.x - b.x, a.y - b.y };
    return out;
}

static struct vec2 vec2_scale(struct vec2 v, float s) {
    struct vec2 out = { v.x * s, v.y * s };
    return out;
}

static struct vec2 vec2_normalize(struct vec2 v) {
    float len = vec2_length(v);
    if (len <= 0.0001f) {
        struct vec2 out = { 0.0f, 0.0f };
        return out;
    }
    return vec2_scale(v, 1.0f / len);
}

static int map_cell(int x, int y) {
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
        return 1;
    }
    return level_map[y][x] == '1';
}

static int can_move_to(struct vec2 pos) {
    int cx = (int)floorf(pos.x);
    int cy = (int)floorf(pos.y);
    return !map_cell(cx, cy);
}

static struct raycast_hit raycast(struct vec2 pos, struct vec2 dir) {
    struct raycast_hit hit = { 0 };

    int map_x = (int)floorf(pos.x);
    int map_y = (int)floorf(pos.y);

    float delta_dist_x = dir.x == 0.0f ? 1e30f : fabsf(1.0f / dir.x);
    float delta_dist_y = dir.y == 0.0f ? 1e30f : fabsf(1.0f / dir.y);

    int step_x = 0;
    int step_y = 0;
    float side_dist_x = 0.0f;
    float side_dist_y = 0.0f;

    if (dir.x < 0.0f) {
        step_x = -1;
        side_dist_x = (pos.x - (float)map_x) * delta_dist_x;
    } else {
        step_x = 1;
        side_dist_x = ((float)map_x + 1.0f - pos.x) * delta_dist_x;
    }

    if (dir.y < 0.0f) {
        step_y = -1;
        side_dist_y = (pos.y - (float)map_y) * delta_dist_y;
    } else {
        step_y = 1;
        side_dist_y = ((float)map_y + 1.0f - pos.y) * delta_dist_y;
    }

    for (int i = 0; i < 128; i++) {
        if (side_dist_x < side_dist_y) {
            side_dist_x += delta_dist_x;
            map_x += step_x;
            hit.side = 0;
        } else {
            side_dist_y += delta_dist_y;
            map_y += step_y;
            hit.side = 1;
        }

        if (map_cell(map_x, map_y)) {
            hit.hit = 1;
            hit.map_x = map_x;
            hit.map_y = map_y;
            break;
        }
    }

    if (!hit.hit) {
        hit.distance = 1000.0f;
        return hit;
    }

    if (hit.side == 0) {
        hit.distance = side_dist_x - delta_dist_x;
    } else {
        hit.distance = side_dist_y - delta_dist_y;
    }

    return hit;
}

static void draw_weapon(uint32_t *pixels, int width, int height, int frame) {
    int cx = width / 2;
    int base_y = height - 24;
    int bob = (frame / 8) % 2;
    int gun_y = base_y + bob;

    uint32_t outline = 0x00f4d27au;
    uint32_t accent = 0x00b0d0ffu;

    budo_draw_line(pixels, width, height,
                   cx - 42, gun_y + 14,
                   cx - 18, gun_y - 2,
                   outline);
    budo_draw_line(pixels, width, height,
                   cx - 18, gun_y - 2,
                   cx - 6, gun_y - 2,
                   outline);
    budo_draw_line(pixels, width, height,
                   cx - 6, gun_y - 2,
                   cx - 2, gun_y + 12,
                   outline);
    budo_draw_line(pixels, width, height,
                   cx - 2, gun_y + 12,
                   cx - 30, gun_y + 20,
                   outline);
    budo_draw_line(pixels, width, height,
                   cx - 30, gun_y + 20,
                   cx - 42, gun_y + 14,
                   outline);

    budo_draw_line(pixels, width, height,
                   cx - 6, gun_y - 6,
                   cx + 30, gun_y - 10,
                   accent);
    budo_draw_line(pixels, width, height,
                   cx + 30, gun_y - 10,
                   cx + 44, gun_y + 2,
                   accent);
    budo_draw_line(pixels, width, height,
                   cx + 44, gun_y + 2,
                   cx + 8, gun_y + 8,
                   accent);
    budo_draw_line(pixels, width, height,
                   cx + 8, gun_y + 8,
                   cx - 6, gun_y - 6,
                   accent);

    budo_draw_line(pixels, width, height,
                   cx + 18, gun_y - 4,
                   cx + 36, gun_y - 2,
                   accent);
    budo_draw_line(pixels, width, height,
                   cx + 36, gun_y - 2,
                   cx + 40, gun_y + 4,
                   accent);
    budo_draw_line(pixels, width, height,
                   cx + 40, gun_y + 4,
                   cx + 22, gun_y + 6,
                   accent);
}

static void draw_minimap(uint32_t *pixels, int width, int height,
                         const struct player_state *player,
                         const struct enemy *enemies, int enemy_count) {
    int scale = 6;
    int offset_x = 8;
    int offset_y = 8;

    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (!map_cell(x, y)) {
                continue;
            }
            int x0 = offset_x + x * scale;
            int y0 = offset_y + y * scale;
            int x1 = x0 + scale - 1;
            int y1 = y0 + scale - 1;
            budo_draw_line(pixels, width, height, x0, y0, x1, y0, 0x0070a0d0u);
            budo_draw_line(pixels, width, height, x1, y0, x1, y1, 0x0070a0d0u);
            budo_draw_line(pixels, width, height, x1, y1, x0, y1, 0x0070a0d0u);
            budo_draw_line(pixels, width, height, x0, y1, x0, y0, 0x0070a0d0u);
        }
    }

    int player_x = offset_x + (int)lroundf(player->position.x * scale);
    int player_y = offset_y + (int)lroundf(player->position.y * scale);
    budo_draw_line(pixels, width, height,
                   player_x - 2, player_y, player_x + 2, player_y, 0x00f4d27au);
    budo_draw_line(pixels, width, height,
                   player_x, player_y - 2, player_x, player_y + 2, 0x00f4d27au);

    float left_angle = player->angle - FOV_RADIANS * 0.5f;
    float right_angle = player->angle + FOV_RADIANS * 0.5f;
    struct vec2 left_dir = { cosf(left_angle), sinf(left_angle) };
    struct vec2 right_dir = { cosf(right_angle), sinf(right_angle) };

    budo_draw_line(pixels, width, height,
                   player_x, player_y,
                   player_x + (int)lroundf(left_dir.x * 6.0f),
                   player_y + (int)lroundf(left_dir.y * 6.0f),
                   0x0050d0ffu);
    budo_draw_line(pixels, width, height,
                   player_x, player_y,
                   player_x + (int)lroundf(right_dir.x * 6.0f),
                   player_y + (int)lroundf(right_dir.y * 6.0f),
                   0x0050d0ffu);

    for (int i = 0; i < enemy_count; i++) {
        if (!enemies[i].active) {
            continue;
        }
        int ex = offset_x + (int)lroundf(enemies[i].position.x * scale);
        int ey = offset_y + (int)lroundf(enemies[i].position.y * scale);
        budo_draw_line(pixels, width, height, ex - 1, ey - 1, ex + 1, ey + 1, 0x00ff7070u);
        budo_draw_line(pixels, width, height, ex + 1, ey - 1, ex - 1, ey + 1, 0x00ff7070u);
    }
}

static void spawn_enemy(struct enemy *enemy, struct vec2 spawn) {
    if (!enemy) {
        return;
    }
    enemy->active = 1;
    enemy->position = spawn;
    enemy->health = 100.0f;
    enemy->respawn_timer = 0.0f;
    enemy->attack_timer = 0.0f;

    float angle = ((float)rand() / (float)RAND_MAX) * 6.28318530718f;
    enemy->velocity.x = cosf(angle) * ENEMY_SPEED;
    enemy->velocity.y = sinf(angle) * ENEMY_SPEED;
}

static void reset_player(struct player_state *player) {
    player->position.x = 1.5f;
    player->position.y = 1.5f;
    player->angle = 1.57f;
    player->health = 100;
}

static void update_enemy(struct enemy *enemy, const struct player_state *player, float delta) {
    if (!enemy->active) {
        return;
    }

    struct vec2 to_player = vec2_sub(player->position, enemy->position);
    float distance = vec2_length(to_player);

    if (distance < 6.0f && distance > 0.1f) {
        struct vec2 desired = vec2_scale(vec2_normalize(to_player), ENEMY_SPEED);
        enemy->velocity = desired;
    }

    struct vec2 next_pos = vec2_add(enemy->position, vec2_scale(enemy->velocity, delta));
    if (map_cell((int)floorf(next_pos.x), (int)floorf(next_pos.y))) {
        float angle = ((float)rand() / (float)RAND_MAX) * 6.28318530718f;
        enemy->velocity.x = cosf(angle) * ENEMY_SPEED;
        enemy->velocity.y = sinf(angle) * ENEMY_SPEED;
    } else {
        enemy->position = next_pos;
    }

}

static int apply_enemy_damage(struct enemy *enemy, int damage) {
    if (!enemy || !enemy->active) {
        return 0;
    }
    enemy->health -= (float)damage;
    if (enemy->health <= 0.0f) {
        enemy->active = 0;
        enemy->respawn_timer = ENEMY_RESPAWN_TIME;
        return 1;
    }
    return 0;
}

static float angle_diff(float a, float b) {
    float diff = a - b;
    while (diff > 3.14159265359f) {
        diff -= 6.28318530718f;
    }
    while (diff < -3.14159265359f) {
        diff += 6.28318530718f;
    }
    return diff;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    psf_font_t font;
    if (psf_font_load(&font, "../fonts/system.psf") != 0) {
        fprintf(stderr, "Failed to load PSF font: %s\n", "../fonts/system.psf");
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_DisplayMode desktop_mode;
    if (SDL_GetCurrentDisplayMode(0, &desktop_mode) != 0) {
        fprintf(stderr, "Failed to query desktop display mode: %s\n", SDL_GetError());
        psf_font_destroy(&font);
        SDL_Quit();
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Budo Vector Shooter",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          desktop_mode.w,
                                          desktop_mode.h,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN |
                                              SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        psf_font_destroy(&font);
        SDL_Quit();
        return 1;
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        fprintf(stderr, "Failed to create GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        psf_font_destroy(&font);
        SDL_Quit();
        return 1;
    }

    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
    if (drawable_width <= 0 || drawable_height <= 0) {
        SDL_GetWindowSize(window, &drawable_width, &drawable_height);
    }

    SDL_GL_SetSwapInterval(1);

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        fprintf(stderr, "Failed to create GL texture.\n");
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        psf_font_destroy(&font);
        SDL_Quit();
        return 1;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GAME_WIDTH, GAME_HEIGHT, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    uint32_t *pixels = malloc((size_t)GAME_WIDTH * (size_t)GAME_HEIGHT * sizeof(uint32_t));
    if (!pixels) {
        fprintf(stderr, "Failed to allocate pixel buffer.\n");
        glDeleteTextures(1, &texture);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        psf_font_destroy(&font);
        SDL_Quit();
        return 1;
    }

    struct budo_shader_stack *stack = NULL;
    if (budo_shader_stack_init(&stack) != 0) {
        fprintf(stderr, "Failed to initialize shader stack.\n");
        free(pixels);
        glDeleteTextures(1, &texture);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        psf_font_destroy(&font);
        SDL_Quit();
        return 1;
    }

    const char *shader_paths[] = {
        "../shaders/crtscreen.glsl"
    };

    if (budo_shader_stack_load(stack, shader_paths, 1u) != 0) {
        fprintf(stderr, "Failed to load shaders.\n");
        budo_shader_stack_destroy(stack);
        free(pixels);
        glDeleteTextures(1, &texture);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        psf_font_destroy(&font);
        SDL_Quit();
        return 1;
    }

    srand((unsigned int)SDL_GetTicks());

    struct player_state player;
    reset_player(&player);

    struct enemy enemies[ENEMY_COUNT];
    memset(enemies, 0, sizeof(enemies));

    struct vec2 spawn_points[] = {
        { 12.5f, 1.5f },
        { 13.5f, 12.5f },
        { 1.5f, 12.5f },
        { 8.5f, 8.5f },
        { 4.5f, 10.5f },
        { 10.5f, 4.5f }
    };

    for (int i = 0; i < ENEMY_COUNT; i++) {
        spawn_enemy(&enemies[i], spawn_points[i % (int)(sizeof(spawn_points) / sizeof(spawn_points[0]))]);
    }

    int running = 1;
    Uint32 last_tick = SDL_GetTicks();
    float fire_timer = 0.0f;
    int score = 0;
    int frame_value = 0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = 0;
                }
            } else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
                    if (drawable_width <= 0 || drawable_height <= 0) {
                        SDL_GetWindowSize(window, &drawable_width, &drawable_height);
                    }
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        float delta = (float)(now - last_tick) / 1000.0f;
        if (delta > 0.05f) {
            delta = 0.05f;
        }
        last_tick = now;

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_Q]) {
            player.angle -= PLAYER_TURN_SPEED * delta;
        }
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_E]) {
            player.angle += PLAYER_TURN_SPEED * delta;
        }
        player.angle = clamp_angle(player.angle);

        struct vec2 forward = { cosf(player.angle), sinf(player.angle) };
        struct vec2 right = { cosf(player.angle + 1.57079632679f), sinf(player.angle + 1.57079632679f) };

        struct vec2 movement = { 0.0f, 0.0f };
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) {
            movement = vec2_add(movement, forward);
        }
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) {
            movement = vec2_sub(movement, forward);
        }
        if (keys[SDL_SCANCODE_A]) {
            movement = vec2_sub(movement, right);
        }
        if (keys[SDL_SCANCODE_D]) {
            movement = vec2_add(movement, right);
        }

        if (movement.x != 0.0f || movement.y != 0.0f) {
            movement = vec2_scale(vec2_normalize(movement), PLAYER_MOVE_SPEED * delta);
        }

        struct vec2 proposed = vec2_add(player.position, movement);
        struct vec2 slide_x = { proposed.x, player.position.y };
        struct vec2 slide_y = { player.position.x, proposed.y };

        if (can_move_to(slide_x)) {
            player.position.x = slide_x.x;
        }
        if (can_move_to(slide_y)) {
            player.position.y = slide_y.y;
        }

        if (player.position.x < PLAYER_RADIUS) {
            player.position.x = PLAYER_RADIUS;
        }
        if (player.position.y < PLAYER_RADIUS) {
            player.position.y = PLAYER_RADIUS;
        }
        if (player.position.x > (float)MAP_WIDTH - PLAYER_RADIUS) {
            player.position.x = (float)MAP_WIDTH - PLAYER_RADIUS;
        }
        if (player.position.y > (float)MAP_HEIGHT - PLAYER_RADIUS) {
            player.position.y = (float)MAP_HEIGHT - PLAYER_RADIUS;
        }

        fire_timer -= delta;
        if (fire_timer < 0.0f) {
            fire_timer = 0.0f;
        }

        int did_fire = 0;
        if (keys[SDL_SCANCODE_SPACE] && fire_timer <= 0.0f) {
            fire_timer = FIRE_COOLDOWN;
            did_fire = 1;
        }

        int hit_index = -1;
        float wall_distance = 1000.0f;
        if (did_fire) {
            struct raycast_hit hit = raycast(player.position, forward);
            wall_distance = hit.distance;

            float closest = wall_distance;
            for (int i = 0; i < ENEMY_COUNT; i++) {
                if (!enemies[i].active) {
                    continue;
                }
                struct vec2 diff = vec2_sub(enemies[i].position, player.position);
                float dist = vec2_length(diff);
                if (dist >= closest) {
                    continue;
                }
                float ang = atan2f(diff.y, diff.x);
                float diff_angle = fabsf(angle_diff(ang, player.angle));
                if (diff_angle < 0.08f) {
                    hit_index = i;
                    closest = dist;
                }
            }
        }

        if (hit_index >= 0) {
            score += apply_enemy_damage(&enemies[hit_index], HIT_DAMAGE);
        }

        for (int i = 0; i < ENEMY_COUNT; i++) {
            if (!enemies[i].active) {
                enemies[i].respawn_timer -= delta;
                if (enemies[i].respawn_timer <= 0.0f) {
                    spawn_enemy(&enemies[i], spawn_points[i % (int)(sizeof(spawn_points) / sizeof(spawn_points[0]))]);
                }
                continue;
            }
            update_enemy(&enemies[i], &player, delta);
        }

        for (int i = 0; i < ENEMY_COUNT; i++) {
            if (!enemies[i].active) {
                continue;
            }
            struct vec2 diff = vec2_sub(enemies[i].position, player.position);
            float dist = vec2_length(diff);
            if (dist < ENEMY_ATTACK_RANGE) {
                enemies[i].attack_timer -= delta;
                if (enemies[i].attack_timer <= 0.0f) {
                    player.health -= 6;
                    enemies[i].attack_timer = ENEMY_ATTACK_COOLDOWN;
                }
            } else {
                enemies[i].attack_timer = 0.0f;
            }
        }

        if (player.health <= 0) {
            score = 0;
            reset_player(&player);
        }

        budo_clear_buffer(pixels, GAME_WIDTH, GAME_HEIGHT, 0x00060a0fu);

        float proj_plane = ((float)GAME_WIDTH * 0.5f) / tanf(FOV_RADIANS * 0.5f);
        int step = VIEW_SAMPLE_STEP;
        int sample_count = (GAME_WIDTH + step - 1) / step;
        int prev_top = -1;
        int prev_bot = -1;
        int prev_x = -1;

        for (int s = 0; s < sample_count; s++) {
            int x = s * step;
            float ray_angle = player.angle - FOV_RADIANS * 0.5f +
                              ((float)x / (float)GAME_WIDTH) * FOV_RADIANS;
            struct vec2 ray_dir = { cosf(ray_angle), sinf(ray_angle) };
            struct raycast_hit hit = raycast(player.position, ray_dir);
            if (!hit.hit || hit.distance <= 0.001f) {
                continue;
            }
            float corrected = hit.distance * cosf(ray_angle - player.angle);
            float line_height = proj_plane / corrected;
            int line_h = (int)lroundf(line_height);
            if (line_h < 1) {
                line_h = 1;
            }
            int y0 = (GAME_HEIGHT / 2) - (line_h / 2);
            int y1 = y0 + line_h;
            if (y0 < 0) {
                y0 = 0;
            }
            if (y1 >= GAME_HEIGHT) {
                y1 = GAME_HEIGHT - 1;
            }
            uint32_t color = hit.side ? 0x00b0d0ffu : 0x00d0f0ffu;
            if (prev_x >= 0) {
                budo_draw_line(pixels, GAME_WIDTH, GAME_HEIGHT,
                               prev_x, prev_top, x, y0, color);
                budo_draw_line(pixels, GAME_WIDTH, GAME_HEIGHT,
                               prev_x, prev_bot, x, y1, color);
                budo_draw_line(pixels, GAME_WIDTH, GAME_HEIGHT,
                               x, y0, x, y1, color);
            }
            prev_x = x;
            prev_top = y0;
            prev_bot = y1;
        }

        for (int i = 0; i < ENEMY_COUNT; i++) {
            if (!enemies[i].active) {
                continue;
            }
            struct vec2 diff = vec2_sub(enemies[i].position, player.position);
            float dist = vec2_length(diff);
            float ang = atan2f(diff.y, diff.x);
            float diff_angle = angle_diff(ang, player.angle);
            if (fabsf(diff_angle) > FOV_RADIANS * 0.6f) {
                continue;
            }
            struct raycast_hit hit = raycast(player.position, vec2_normalize(diff));
            if (hit.hit && hit.distance < dist) {
                continue;
            }
            float proj_x = (0.5f + diff_angle / FOV_RADIANS) * (float)GAME_WIDTH;
            float proj_height = proj_plane / dist;
            int line_h = (int)lroundf(proj_height);
            int y0 = (GAME_HEIGHT / 2) - line_h / 2;
            int y1 = y0 + line_h;
            int x = (int)lroundf(proj_x);
            if (x < 0 || x >= GAME_WIDTH) {
                continue;
            }
            if (y0 < 0) {
                y0 = 0;
            }
            if (y1 >= GAME_HEIGHT) {
                y1 = GAME_HEIGHT - 1;
            }
            budo_draw_line(pixels, GAME_WIDTH, GAME_HEIGHT, x, y0, x, y1, 0x00ff7070u);
            budo_draw_line(pixels, GAME_WIDTH, GAME_HEIGHT, x - 1, y0 + 2, x + 1, y0 + 2, 0x00ff7070u);
        }

        budo_draw_line(pixels, GAME_WIDTH, GAME_HEIGHT,
                       GAME_WIDTH / 2 - 6, GAME_HEIGHT / 2,
                       GAME_WIDTH / 2 + 6, GAME_HEIGHT / 2,
                       0x00f0f0f0u);
        budo_draw_line(pixels, GAME_WIDTH, GAME_HEIGHT,
                       GAME_WIDTH / 2, GAME_HEIGHT / 2 - 4,
                       GAME_WIDTH / 2, GAME_HEIGHT / 2 + 4,
                       0x00f0f0f0u);

        draw_weapon(pixels, GAME_WIDTH, GAME_HEIGHT, frame_value);
        draw_minimap(pixels, GAME_WIDTH, GAME_HEIGHT, &player, enemies, ENEMY_COUNT);

        char hud[160];
        snprintf(hud, sizeof(hud), "VECTOR DOOM  HP:%d  SCORE:%d", player.health, score);
        psf_draw_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 8, GAME_HEIGHT - 2 * (int)font.height - 4,
                      hud, 0x00ffffffu);
        psf_draw_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 8, GAME_HEIGHT - (int)font.height - 2,
                      "WASD/ARROWS MOVE  QE/ARROWS TURN  SPACE FIRE  ESC QUIT", 0x0080c0ffu);

        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GAME_WIDTH, GAME_HEIGHT,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glBindTexture(GL_TEXTURE_2D, 0);

        glClear(GL_COLOR_BUFFER_BIT);
        if (budo_shader_stack_render(stack,
                                     texture,
                                     GAME_WIDTH,
                                     GAME_HEIGHT,
                                     drawable_width,
                                     drawable_height,
                                     0,
                                     frame_value) != 0) {
            fprintf(stderr, "Shader stack render failed.\n");
            running = 0;
        }

        SDL_GL_SwapWindow(window);
        frame_value++;

        Uint32 frame_ms = SDL_GetTicks() - now;
        Uint32 target_ms = 1000u / TARGET_FPS;
        if (frame_ms < target_ms) {
            SDL_Delay(target_ms - frame_ms);
        }
    }

    budo_shader_stack_destroy(stack);
    free(pixels);
    glDeleteTextures(1, &texture);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    psf_font_destroy(&font);
    SDL_Quit();

    return 0;
}
