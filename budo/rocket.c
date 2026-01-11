#include "budo_audio.h"
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

#define MAX_ASTEROIDS 16
#define MAX_BULLETS 32
#define ASTEROID_VERTS_MAX 12
#define MAX_LEVEL 10

#define SHIP_TURN_SPEED 3.5f
#define SHIP_THRUST 150.0f
#define SHIP_FRICTION 0.98f
#define SHIP_RADIUS 10.0f

#define BULLET_SPEED 220.0f
#define BULLET_LIFE 3.0f
#define FIRE_COOLDOWN 0.18f

#define BONUS_LIFE_SCORE 5000
#define LEVEL_BANNER_TIME 2.0f

#define MENU_ITEM_COUNT 3
#define OPTIONS_ITEM_COUNT 3

struct vec2 {
    float x;
    float y;
};

struct ship_state {
    struct vec2 position;
    struct vec2 velocity;
    float angle;
    int lives;
    float invulnerable;
};

struct bullet {
    int active;
    struct vec2 position;
    struct vec2 velocity;
    float life;
};

struct asteroid {
    int active;
    struct vec2 position;
    struct vec2 velocity;
    float radius;
    int vertex_count;
    float radius_scale[ASTEROID_VERTS_MAX];
};

struct level_config {
    int asteroid_count;
    float speed_min;
    float speed_max;
    float radius_min;
    float radius_max;
    float fragment_scale;
};

struct game_settings {
    int difficulty;
    int starting_lives;
};

enum game_state {
    STATE_MENU = 0,
    STATE_OPTIONS,
    STATE_PLAY,
    STATE_GAME_OVER,
    STATE_VICTORY
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

static float frand_range(float min_v, float max_v) {
    return min_v + (max_v - min_v) * ((float)rand() / (float)RAND_MAX);
}

static struct vec2 vec2_add(struct vec2 a, struct vec2 b) {
    struct vec2 out = { a.x + b.x, a.y + b.y };
    return out;
}

static struct vec2 vec2_scale(struct vec2 v, float s) {
    struct vec2 out = { v.x * s, v.y * s };
    return out;
}

static struct vec2 vec2_rotate(struct vec2 v, float angle) {
    float c = cosf(angle);
    float s = sinf(angle);
    struct vec2 out = { v.x * c - v.y * s, v.x * s + v.y * c };
    return out;
}

static void wrap_position(struct vec2 *pos, float width, float height) {
    if (!pos) {
        return;
    }
    if (pos->x < 0.0f) {
        pos->x += width;
    } else if (pos->x >= width) {
        pos->x -= width;
    }
    if (pos->y < 0.0f) {
        pos->y += height;
    } else if (pos->y >= height) {
        pos->y -= height;
    }
}

static void build_asteroid_shape(struct asteroid *a) {
    if (!a) {
        return;
    }
    a->vertex_count = 8 + (rand() % 5);
    if (a->vertex_count > ASTEROID_VERTS_MAX) {
        a->vertex_count = ASTEROID_VERTS_MAX;
    }
    for (int i = 0; i < a->vertex_count; i++) {
        a->radius_scale[i] = frand_range(0.65f, 1.0f);
    }
}

static void spawn_asteroid(struct asteroid *a, float radius, struct vec2 position,
                           float speed_min, float speed_max) {
    if (!a) {
        return;
    }
    a->active = 1;
    a->position = position;
    a->radius = radius;
    float angle = frand_range(0.0f, 6.28318530718f);
    float speed = frand_range(speed_min, speed_max);
    a->velocity.x = cosf(angle) * speed;
    a->velocity.y = sinf(angle) * speed;
    build_asteroid_shape(a);
}

static void spawn_random_asteroid(struct asteroid *a, float radius, struct vec2 avoid,
                                  float speed_min, float speed_max) {
    if (!a) {
        return;
    }
    struct vec2 pos = { frand_range(0.0f, (float)GAME_WIDTH),
                        frand_range(0.0f, (float)GAME_HEIGHT) };
    float dx = pos.x - avoid.x;
    float dy = pos.y - avoid.y;
    if ((dx * dx + dy * dy) < (radius + 80.0f) * (radius + 80.0f)) {
        pos.x = fmodf(pos.x + (float)GAME_WIDTH * 0.5f, (float)GAME_WIDTH);
        pos.y = fmodf(pos.y + (float)GAME_HEIGHT * 0.5f, (float)GAME_HEIGHT);
    }
    spawn_asteroid(a, radius, pos, speed_min, speed_max);
}

static void draw_polyline(uint32_t *pixels, int width, int height,
                          const struct vec2 *points, int count, uint32_t color) {
    if (!pixels || !points || count < 2) {
        return;
    }
    for (int i = 0; i < count - 1; i++) {
        budo_draw_line(pixels, width, height,
                       (int)lroundf(points[i].x),
                       (int)lroundf(points[i].y),
                       (int)lroundf(points[i + 1].x),
                       (int)lroundf(points[i + 1].y),
                       color);
    }
}

static void draw_ship(uint32_t *pixels, int width, int height,
                      const struct ship_state *ship, uint32_t color) {
    if (!ship || !pixels) {
        return;
    }
    struct vec2 ship_points[4] = {
        { 0.0f, -SHIP_RADIUS },
        { SHIP_RADIUS * 0.7f, SHIP_RADIUS },
        { 0.0f, SHIP_RADIUS * 0.4f },
        { -SHIP_RADIUS * 0.7f, SHIP_RADIUS }
    };

    struct vec2 transformed[4];
    for (size_t i = 0; i < 4; i++) {
        struct vec2 rotated = vec2_rotate(ship_points[i], ship->angle);
        transformed[i].x = rotated.x + ship->position.x;
        transformed[i].y = rotated.y + ship->position.y;
    }

    draw_polyline(pixels, width, height, transformed, 4, color);
    budo_draw_line(pixels, width, height,
                   (int)lroundf(transformed[3].x),
                   (int)lroundf(transformed[3].y),
                   (int)lroundf(transformed[0].x),
                   (int)lroundf(transformed[0].y),
                   color);
}

static void draw_asteroid(uint32_t *pixels, int width, int height,
                          const struct asteroid *asteroid, uint32_t color) {
    if (!pixels || !asteroid || !asteroid->active) {
        return;
    }
    struct vec2 points[ASTEROID_VERTS_MAX];
    float step = 6.28318530718f / (float)asteroid->vertex_count;
    for (int i = 0; i < asteroid->vertex_count; i++) {
        float angle = step * (float)i;
        float radius = asteroid->radius * asteroid->radius_scale[i];
        points[i].x = asteroid->position.x + cosf(angle) * radius;
        points[i].y = asteroid->position.y + sinf(angle) * radius;
    }
    draw_polyline(pixels, width, height, points, asteroid->vertex_count, color);
    budo_draw_line(pixels, width, height,
                   (int)lroundf(points[asteroid->vertex_count - 1].x),
                   (int)lroundf(points[asteroid->vertex_count - 1].y),
                   (int)lroundf(points[0].x),
                   (int)lroundf(points[0].y),
                   color);
}

static void reset_ship(struct ship_state *ship) {
    if (!ship) {
        return;
    }
    ship->position.x = GAME_WIDTH * 0.5f;
    ship->position.y = GAME_HEIGHT * 0.5f;
    ship->velocity.x = 0.0f;
    ship->velocity.y = 0.0f;
    ship->angle = 0.0f;
    ship->invulnerable = 1.5f;
}

static float dist_sq(struct vec2 a, struct vec2 b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

static int count_active_asteroids(const struct asteroid *asteroids) {
    int count = 0;
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (asteroids[i].active) {
            count++;
        }
    }
    return count;
}

static void get_level_config(int level, int difficulty, struct level_config *out) {
    if (!out) {
        return;
    }
    if (level < 1) {
        level = 1;
    } else if (level > MAX_LEVEL) {
        level = MAX_LEVEL;
    }
    if (difficulty < 0) {
        difficulty = 0;
    } else if (difficulty > 2) {
        difficulty = 2;
    }

    int base_count = 4 + level * 2;
    if (difficulty == 0) {
        base_count -= 1;
    } else if (difficulty == 2) {
        base_count += 2;
    }
    if (base_count < 3) {
        base_count = 3;
    } else if (base_count > MAX_ASTEROIDS) {
        base_count = MAX_ASTEROIDS;
    }
    out->asteroid_count = base_count;

    float speed_base = 20.0f + (float)level * 6.0f;
    float speed_var = 15.0f + (float)level * 2.5f;
    float speed_mult = 1.0f + (float)difficulty * 0.15f;
    out->speed_min = speed_base * speed_mult;
    out->speed_max = (speed_base + speed_var) * speed_mult;

    float radius_max = 34.0f - (float)level * 1.4f;
    if (radius_max < 18.0f) {
        radius_max = 18.0f;
    }
    float radius_min = radius_max * 0.65f;
    if (radius_min < 12.0f) {
        radius_min = 12.0f;
    }
    out->radius_max = radius_max;
    out->radius_min = radius_min;

    out->fragment_scale = 0.62f - (float)level * 0.01f;
    if (out->fragment_scale < 0.45f) {
        out->fragment_scale = 0.45f;
    }
}

static float level_radius_roll(const struct level_config *config) {
    if (!config) {
        return 20.0f;
    }
    return frand_range(config->radius_min, config->radius_max);
}

static void spawn_wave(struct asteroid *asteroids, const struct level_config *config,
                       struct vec2 avoid) {
    if (!config) {
        return;
    }
    int spawned = 0;
    for (int i = 0; i < MAX_ASTEROIDS && spawned < config->asteroid_count; i++) {
        if (!asteroids[i].active) {
            float radius = level_radius_roll(config);
            spawn_random_asteroid(&asteroids[i], radius, avoid,
                                  config->speed_min, config->speed_max);
            spawned++;
        }
    }
}

static void spawn_fragment(struct asteroid *asteroids, struct vec2 position, float radius,
                           const struct level_config *config) {
    if (!config) {
        return;
    }
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (!asteroids[i].active) {
            float speed_boost = 12.0f;
            spawn_asteroid(&asteroids[i], radius, position,
                           config->speed_min + speed_boost,
                           config->speed_max + speed_boost);
            return;
        }
    }
}

static void handle_bullet_hits(struct bullet *bullets,
                               struct asteroid *asteroids,
                               int *score,
                               const struct level_config *config) {
    for (int b = 0; b < MAX_BULLETS; b++) {
        if (!bullets[b].active) {
            continue;
        }
        for (int a = 0; a < MAX_ASTEROIDS; a++) {
            if (!asteroids[a].active) {
                continue;
            }
            float radius = asteroids[a].radius;
            if (dist_sq(bullets[b].position, asteroids[a].position) <= radius * radius) {
                bullets[b].active = 0;
                asteroids[a].active = 0;
                if (score) {
                    *score += (int)radius;
                }
                if (config && radius > config->radius_min * 0.95f) {
                    float fragment_radius = radius * config->fragment_scale;
                    spawn_fragment(asteroids, asteroids[a].position, fragment_radius, config);
                    spawn_fragment(asteroids, asteroids[a].position, fragment_radius, config);
                }
                break;
            }
        }
    }
}

static void draw_centered_text(const psf_font_t *font, uint32_t *pixels,
                               int width, int height, int y,
                               const char *text, uint32_t color) {
    if (!font || !pixels || !text) {
        return;
    }
    (void)height;
    size_t len = strlen(text);
    int text_width = (int)(len * (size_t)font->width);
    int x = (width - text_width) / 2;
    psf_draw_text(font, pixels, width, height, x, y, text, color);
}

static void reset_game_state(struct ship_state *ship, struct bullet *bullets,
                             struct asteroid *asteroids, int lives) {
    if (!ship || !bullets || !asteroids) {
        return;
    }
    ship->lives = lives;
    reset_ship(ship);
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullets[i].active = 0;
    }
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        asteroids[i].active = 0;
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    psf_font_t font;
    if (psf_font_load(&font, "./fonts/system.psf") != 0) {
        fprintf(stderr, "Failed to load PSF font: %s\n", "./fonts/system.psf");
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

    SDL_Window *window = SDL_CreateWindow("Budo Rocket",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          desktop_mode.w,
                                          desktop_mode.h,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI);
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
        "./shaders/crtscreen.glsl"
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

    int audio_ready = 0;
    budo_music_t background_music = { 0 };
    budo_sound_t fire_sound = { 0 };
    int fire_sound_ready = 0;
    if (budo_audio_init(0, 0, 0, 0) == 0) {
        audio_ready = 1;
        if (budo_music_load(&background_music, "../budo/ROCKET/music.s3m") != 0) {
            fprintf(stderr, "Failed to load music: %s\n", "../budo/ROCKET/music.s3m");
        } else {
            budo_music_set_volume(66);
            if (budo_music_play(&background_music, -1) != 0) {
                fprintf(stderr, "Failed to start background music.\n");
            }
        }
        if (budo_sound_load(&fire_sound, "../budo/ROCKET/fire.wav") != 0) {
            fprintf(stderr, "Failed to load sound: %s\n", "../budo/ROCKET/fire.wav");
        } else {
            budo_sound_set_volume(&fire_sound, 128);
            fire_sound_ready = 1;
        }
    } else {
        fprintf(stderr, "Failed to initialize audio.\n");
    }

    srand((unsigned int)SDL_GetTicks());

    struct ship_state ship = {
        .position = { GAME_WIDTH * 0.5f, GAME_HEIGHT * 0.5f },
        .velocity = { 0.0f, 0.0f },
        .angle = 0.0f,
        .lives = 3,
        .invulnerable = 1.0f
    };

    struct bullet bullets[MAX_BULLETS] = { 0 };
    struct asteroid asteroids[MAX_ASTEROIDS] = { 0 };

    struct game_settings settings = {
        .difficulty = 1,
        .starting_lives = 3
    };

    struct level_config level_config = { 0 };
    int level = 1;
    int score = 0;
    int next_bonus = BONUS_LIFE_SCORE;
    float level_banner = 0.0f;
    enum game_state state = STATE_MENU;
    int menu_index = 0;
    int options_index = 0;

    int running = 1;
    Uint32 last_tick = SDL_GetTicks();
    float fire_cooldown = 0.0f;
    int frame_value = 0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                if (state == STATE_MENU) {
                    if (event.key.keysym.sym == SDLK_UP) {
                        menu_index = (menu_index + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
                    } else if (event.key.keysym.sym == SDLK_DOWN) {
                        menu_index = (menu_index + 1) % MENU_ITEM_COUNT;
                    } else if (event.key.keysym.sym == SDLK_RETURN ||
                               event.key.keysym.sym == SDLK_KP_ENTER) {
                        if (menu_index == 0) {
                            level = 1;
                            score = 0;
                            next_bonus = BONUS_LIFE_SCORE;
                            get_level_config(level, settings.difficulty, &level_config);
                            reset_game_state(&ship, bullets, asteroids, settings.starting_lives);
                            spawn_wave(asteroids, &level_config, ship.position);
                            level_banner = LEVEL_BANNER_TIME;
                            fire_cooldown = 0.0f;
                            state = STATE_PLAY;
                        } else if (menu_index == 1) {
                            state = STATE_OPTIONS;
                        } else {
                            running = 0;
                        }
                    } else if (event.key.keysym.sym == SDLK_ESCAPE) {
                        running = 0;
                    }
                } else if (state == STATE_OPTIONS) {
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        state = STATE_MENU;
                    } else if (event.key.keysym.sym == SDLK_UP) {
                        options_index = (options_index + OPTIONS_ITEM_COUNT - 1) % OPTIONS_ITEM_COUNT;
                    } else if (event.key.keysym.sym == SDLK_DOWN) {
                        options_index = (options_index + 1) % OPTIONS_ITEM_COUNT;
                    } else if (event.key.keysym.sym == SDLK_LEFT) {
                        if (options_index == 0 && settings.difficulty > 0) {
                            settings.difficulty--;
                        } else if (options_index == 1 && settings.starting_lives > 1) {
                            settings.starting_lives--;
                        }
                    } else if (event.key.keysym.sym == SDLK_RIGHT) {
                        if (options_index == 0 && settings.difficulty < 2) {
                            settings.difficulty++;
                        } else if (options_index == 1 && settings.starting_lives < 5) {
                            settings.starting_lives++;
                        }
                    } else if (event.key.keysym.sym == SDLK_RETURN ||
                               event.key.keysym.sym == SDLK_KP_ENTER) {
                        if (options_index == 2) {
                            state = STATE_MENU;
                        }
                    }
                } else if (state == STATE_PLAY) {
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        state = STATE_MENU;
                    }
                } else if (state == STATE_GAME_OVER || state == STATE_VICTORY) {
                    if (event.key.keysym.sym == SDLK_RETURN ||
                        event.key.keysym.sym == SDLK_KP_ENTER ||
                        event.key.keysym.sym == SDLK_ESCAPE) {
                        state = STATE_MENU;
                    }
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

        if (state == STATE_PLAY) {
            const Uint8 *keys = SDL_GetKeyboardState(NULL);
            if (keys[SDL_SCANCODE_LEFT]) {
                ship.angle -= SHIP_TURN_SPEED * delta;
            }
            if (keys[SDL_SCANCODE_RIGHT]) {
                ship.angle += SHIP_TURN_SPEED * delta;
            }
            ship.angle = clamp_angle(ship.angle);

            if (keys[SDL_SCANCODE_UP]) {
                struct vec2 thrust_dir = { cosf(ship.angle - 1.57079632679f),
                                           sinf(ship.angle - 1.57079632679f) };
                ship.velocity = vec2_add(ship.velocity, vec2_scale(thrust_dir, SHIP_THRUST * delta));
            }

            ship.velocity = vec2_scale(ship.velocity, SHIP_FRICTION);
            ship.position = vec2_add(ship.position, vec2_scale(ship.velocity, delta));
            wrap_position(&ship.position, (float)GAME_WIDTH, (float)GAME_HEIGHT);

            if (ship.invulnerable > 0.0f) {
                ship.invulnerable -= delta;
                if (ship.invulnerable < 0.0f) {
                    ship.invulnerable = 0.0f;
                }
            }

            fire_cooldown -= delta;
            if (fire_cooldown < 0.0f) {
                fire_cooldown = 0.0f;
            }

            if (keys[SDL_SCANCODE_SPACE] && fire_cooldown <= 0.0f) {
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (!bullets[i].active) {
                        struct vec2 dir = { cosf(ship.angle - 1.57079632679f),
                                            sinf(ship.angle - 1.57079632679f) };
                        bullets[i].active = 1;
                        bullets[i].position = ship.position;
                        bullets[i].velocity = vec2_scale(dir, BULLET_SPEED);
                        bullets[i].life = BULLET_LIFE;
                        fire_cooldown = FIRE_COOLDOWN;
                        if (fire_sound_ready) {
                            budo_sound_play(&fire_sound, 0);
                        }
                        break;
                    }
                }
            }

            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].active) {
                    continue;
                }
                bullets[i].position = vec2_add(bullets[i].position,
                                               vec2_scale(bullets[i].velocity, delta));
                if (bullets[i].position.x < 0.0f ||
                    bullets[i].position.x >= (float)GAME_WIDTH ||
                    bullets[i].position.y < 0.0f ||
                    bullets[i].position.y >= (float)GAME_HEIGHT) {
                    bullets[i].active = 0;
                    continue;
                }
                bullets[i].life -= delta;
                if (bullets[i].life <= 0.0f) {
                    bullets[i].active = 0;
                }
            }

            for (int i = 0; i < MAX_ASTEROIDS; i++) {
                if (!asteroids[i].active) {
                    continue;
                }
                asteroids[i].position = vec2_add(asteroids[i].position,
                                                 vec2_scale(asteroids[i].velocity, delta));
                wrap_position(&asteroids[i].position, (float)GAME_WIDTH, (float)GAME_HEIGHT);
            }

            handle_bullet_hits(bullets, asteroids, &score, &level_config);

            if (score >= next_bonus) {
                ship.lives++;
                next_bonus += BONUS_LIFE_SCORE;
            }

            if (ship.invulnerable <= 0.0f) {
                for (int i = 0; i < MAX_ASTEROIDS; i++) {
                    if (!asteroids[i].active) {
                        continue;
                    }
                    float radius = asteroids[i].radius + SHIP_RADIUS;
                    if (dist_sq(ship.position, asteroids[i].position) <= radius * radius) {
                        ship.lives--;
                        reset_ship(&ship);
                        break;
                    }
                }
            }

            if (ship.lives <= 0) {
                state = STATE_GAME_OVER;
            }

            if (count_active_asteroids(asteroids) == 0 && state == STATE_PLAY) {
                if (level >= MAX_LEVEL) {
                    state = STATE_VICTORY;
                } else {
                    level++;
                    get_level_config(level, settings.difficulty, &level_config);
                    spawn_wave(asteroids, &level_config, ship.position);
                    level_banner = LEVEL_BANNER_TIME;
                }
            }

            if (level_banner > 0.0f) {
                level_banner -= delta;
                if (level_banner < 0.0f) {
                    level_banner = 0.0f;
                }
            }
        }

        budo_clear_buffer(pixels, GAME_WIDTH, GAME_HEIGHT, 0x00090f13u);

        if (state == STATE_PLAY) {
            for (int i = 0; i < MAX_ASTEROIDS; i++) {
                draw_asteroid(pixels, GAME_WIDTH, GAME_HEIGHT, &asteroids[i], 0x00c0c0c0u);
            }

            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].active) {
                    continue;
                }
                budo_put_pixel(pixels, GAME_WIDTH, GAME_HEIGHT,
                               (int)lroundf(bullets[i].position.x),
                               (int)lroundf(bullets[i].position.y),
                               0x00f0f0f0u);
            }
        }

        if (state == STATE_PLAY &&
            (ship.invulnerable <= 0.0f || ((frame_value / 6) % 2 == 0))) {
            draw_ship(pixels, GAME_WIDTH, GAME_HEIGHT, &ship, 0x00ffd070u);
        }

        if (state == STATE_PLAY) {
            char hud[128];
            snprintf(hud, sizeof(hud), "ROCKET ASTEROIDS  SCORE:%d  LIVES:%d  LEVEL:%d",
                     score, ship.lives, level);
            psf_draw_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 8, 8, hud, 0x00ffffffu);
            psf_draw_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 8,
                          8 + (int)font.height,
                          "ARROWS MOVE  SPACE FIRE  ESC MENU",
                          0x0080c0ffu);
            if (level_banner > 0.0f) {
                char banner[64];
                snprintf(banner, sizeof(banner), "LEVEL %d", level);
                draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT,
                                   (int)(GAME_HEIGHT * 0.35f),
                                   banner, 0x00ffd070u);
            }
        } else if (state == STATE_MENU) {
            draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 72,
                               "ROCKET ASTEROIDS", 0x00ffd070u);
            const char *menu_items[MENU_ITEM_COUNT] = {
                "NEW GAME",
                "OPTIONS",
                "EXIT"
            };
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                uint32_t color = (i == menu_index) ? 0x00ffffffu : 0x0080c0ffu;
                draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT,
                                   140 + i * ((int)font.height + 8),
                                   menu_items[i], color);
            }
            draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 300,
                               "USE ARROWS + ENTER", 0x0080c0ffu);
        } else if (state == STATE_OPTIONS) {
            draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 72,
                               "OPTIONS", 0x00ffd070u);
            const char *difficulty_names[] = { "CADET", "CLASSIC", "ACE" };
            char line[64];
            uint32_t color = (options_index == 0) ? 0x00ffffffu : 0x0080c0ffu;
            snprintf(line, sizeof(line), "DIFFICULTY: %s", difficulty_names[settings.difficulty]);
            draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 140, line, color);
            color = (options_index == 1) ? 0x00ffffffu : 0x0080c0ffu;
            snprintf(line, sizeof(line), "STARTING LIVES: %d", settings.starting_lives);
            draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 170, line, color);
            color = (options_index == 2) ? 0x00ffffffu : 0x0080c0ffu;
            draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 210, "BACK", color);
            draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 300,
                               "LEFT/RIGHT TO ADJUST", 0x0080c0ffu);
        } else if (state == STATE_GAME_OVER) {
            draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 120,
                               "GAME OVER", 0x00ff6060u);
            char line[64];
            snprintf(line, sizeof(line), "FINAL SCORE: %d", score);
            draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 160, line, 0x00ffffffu);
            draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 210,
                               "PRESS ENTER", 0x0080c0ffu);
        } else if (state == STATE_VICTORY) {
            draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 110,
                               "MISSION COMPLETE", 0x00ffd070u);
            draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 145,
                               "YOU REACHED LEVEL 10", 0x00ffffffu);
            char line[64];
            snprintf(line, sizeof(line), "FINAL SCORE: %d", score);
            draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 180, line, 0x00ffffffu);
            draw_centered_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 230,
                               "PRESS ENTER", 0x0080c0ffu);
        }

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

    if (audio_ready) {
        budo_music_stop();
        budo_music_destroy(&background_music);
        budo_sound_destroy(&fire_sound);
        budo_audio_shutdown();
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
