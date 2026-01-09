#include "budo_graphics.h"
#include "budo_shader_stack.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#define GAME_WIDTH 640
#define GAME_HEIGHT 360
#define TARGET_FPS 60

#define MAX_ASTEROIDS 16
#define MAX_BULLETS 32
#define ASTEROID_VERTS_MAX 12

#define SHIP_TURN_SPEED 3.5f
#define SHIP_THRUST 110.0f
#define SHIP_FRICTION 0.98f
#define SHIP_RADIUS 10.0f

#define BULLET_SPEED 220.0f
#define BULLET_LIFE 1.4f
#define FIRE_COOLDOWN 0.18f

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

static void spawn_asteroid(struct asteroid *a, float radius, struct vec2 position) {
    if (!a) {
        return;
    }
    a->active = 1;
    a->position = position;
    a->radius = radius;
    a->velocity.x = frand_range(-40.0f, 40.0f);
    a->velocity.y = frand_range(-40.0f, 40.0f);
    build_asteroid_shape(a);
}

static void spawn_random_asteroid(struct asteroid *a, float radius, struct vec2 avoid) {
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
    spawn_asteroid(a, radius, pos);
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

static void spawn_wave(struct asteroid *asteroids, int count, struct vec2 avoid) {
    int spawned = 0;
    for (int i = 0; i < MAX_ASTEROIDS && spawned < count; i++) {
        if (!asteroids[i].active) {
            spawn_random_asteroid(&asteroids[i], frand_range(18.0f, 32.0f), avoid);
            spawned++;
        }
    }
}

static void spawn_fragment(struct asteroid *asteroids, struct vec2 position, float radius) {
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (!asteroids[i].active) {
            spawn_asteroid(&asteroids[i], radius, position);
            return;
        }
    }
}

static void handle_bullet_hits(struct bullet *bullets,
                               struct asteroid *asteroids,
                               int *score) {
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
                if (radius > 18.0f) {
                    spawn_fragment(asteroids, asteroids[a].position, radius * 0.65f);
                    spawn_fragment(asteroids, asteroids[a].position, radius * 0.65f);
                }
                break;
            }
        }
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

    struct ship_state ship = {
        .position = { GAME_WIDTH * 0.5f, GAME_HEIGHT * 0.5f },
        .velocity = { 0.0f, 0.0f },
        .angle = 0.0f,
        .lives = 3,
        .invulnerable = 1.0f
    };

    struct bullet bullets[MAX_BULLETS] = { 0 };
    struct asteroid asteroids[MAX_ASTEROIDS] = { 0 };

    spawn_wave(asteroids, 6, ship.position);

    int running = 1;
    Uint32 last_tick = SDL_GetTicks();
    float fire_cooldown = 0.0f;
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
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!bullets[i].active) {
                continue;
            }
            bullets[i].position = vec2_add(bullets[i].position, vec2_scale(bullets[i].velocity, delta));
            wrap_position(&bullets[i].position, (float)GAME_WIDTH, (float)GAME_HEIGHT);
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

        handle_bullet_hits(bullets, asteroids, &score);

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
            ship.lives = 3;
            score = 0;
            for (int i = 0; i < MAX_ASTEROIDS; i++) {
                asteroids[i].active = 0;
            }
            spawn_wave(asteroids, 6, ship.position);
        }

        if (count_active_asteroids(asteroids) == 0) {
            spawn_wave(asteroids, 8, ship.position);
        }

        budo_clear_buffer(pixels, GAME_WIDTH, GAME_HEIGHT, 0x00090f13u);

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

        if (ship.invulnerable <= 0.0f || ((frame_value / 6) % 2 == 0)) {
            draw_ship(pixels, GAME_WIDTH, GAME_HEIGHT, &ship, 0x00ffd070u);
        }

        char hud[128];
        snprintf(hud, sizeof(hud), "ROCKET ASTEROIDS  SCORE:%d  LIVES:%d", score, ship.lives);
        psf_draw_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 8, 8, hud, 0x00ffffffu);
        psf_draw_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 8, 8 + (int)font.height,
                      "ARROWS MOVE  SPACE FIRE  ESC QUIT", 0x0080c0ffu);

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
