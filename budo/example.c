#include "budo_shader_stack.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#define WINDOW_WIDTH 320
#define WINDOW_HEIGHT 200
#define TARGET_FPS 60

struct point3 {
    float x;
    float y;
    float z;
};

struct point2 {
    float x;
    float y;
};

static void clear_buffer(uint32_t *pixels, int width, int height, uint32_t color) {
    if (!pixels || width <= 0 || height <= 0) {
        return;
    }
    size_t total = (size_t)width * (size_t)height;
    for (size_t i = 0; i < total; i++) {
        pixels[i] = color;
    }
}

static void put_pixel(uint32_t *pixels, int width, int height, int x, int y, uint32_t color) {
    if (!pixels || x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }
    pixels[(size_t)y * (size_t)width + (size_t)x] = color;
}

static void draw_line(uint32_t *pixels, int width, int height,
                      int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        put_pixel(pixels, width, height, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static struct point3 rotate_point(struct point3 p, float angle_x, float angle_y) {
    float cx = cosf(angle_x);
    float sx = sinf(angle_x);
    float cy = cosf(angle_y);
    float sy = sinf(angle_y);

    float y = p.y * cx - p.z * sx;
    float z = p.y * sx + p.z * cx;
    p.y = y;
    p.z = z;

    float x = p.x * cy + p.z * sy;
    z = -p.x * sy + p.z * cy;
    p.x = x;
    p.z = z;
    return p;
}

static struct point2 project_point(struct point3 p, int width, int height, float scale) {
    float depth = p.z + 3.0f;
    float inv = depth != 0.0f ? (1.0f / depth) : 1.0f;
    struct point2 out;
    out.x = (float)width * 0.5f + p.x * scale * inv;
    out.y = (float)height * 0.5f - p.y * scale * inv;
    return out;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window *window = SDL_CreateWindow("Budo Shader Stack Demo",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          WINDOW_WIDTH,
                                          WINDOW_HEIGHT,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        fprintf(stderr, "Failed to create GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(1);

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        fprintf(stderr, "Failed to create GL texture.\n");
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WINDOW_WIDTH, WINDOW_HEIGHT,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    uint32_t *pixels = malloc((size_t)WINDOW_WIDTH * (size_t)WINDOW_HEIGHT * sizeof(uint32_t));
    if (!pixels) {
        fprintf(stderr, "Failed to allocate pixel buffer.\n");
        glDeleteTextures(1, &texture);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
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
        SDL_Quit();
        return 1;
    }

    const char *shader_paths[] = {
        "../shaders/crt-geom.glsl",
        "../shaders/crt-guest.glsl"
    };
    if (budo_shader_stack_load(stack, shader_paths, 2u) != 0) {
        fprintf(stderr, "Failed to load shaders.\n");
        budo_shader_stack_destroy(stack);
        free(pixels);
        glDeleteTextures(1, &texture);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    struct point3 cube_vertices[8] = {
        { -1.0f, -1.0f, -1.0f },
        {  1.0f, -1.0f, -1.0f },
        {  1.0f,  1.0f, -1.0f },
        { -1.0f,  1.0f, -1.0f },
        { -1.0f, -1.0f,  1.0f },
        {  1.0f, -1.0f,  1.0f },
        {  1.0f,  1.0f,  1.0f },
        { -1.0f,  1.0f,  1.0f }
    };

    int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };

    int running = 1;
    Uint32 last_tick = SDL_GetTicks();
    float angle = 0.0f;
    int frame_value = 0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
        }

        Uint32 now = SDL_GetTicks();
        float delta = (float)(now - last_tick) / 1000.0f;
        last_tick = now;
        angle += delta;

        clear_buffer(pixels, WINDOW_WIDTH, WINDOW_HEIGHT, 0x00101010u);

        struct point2 projected[8];
        for (size_t i = 0; i < 8; i++) {
            struct point3 rotated = rotate_point(cube_vertices[i], angle * 0.7f, angle);
            projected[i] = project_point(rotated, WINDOW_WIDTH, WINDOW_HEIGHT, 120.0f);
        }

        for (size_t i = 0; i < 12; i++) {
            int a = edges[i][0];
            int b = edges[i][1];
            draw_line(pixels,
                      WINDOW_WIDTH,
                      WINDOW_HEIGHT,
                      (int)projected[a].x,
                      (int)projected[a].y,
                      (int)projected[b].x,
                      (int)projected[b].y,
                      0x00f0d060u);
        }

        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glBindTexture(GL_TEXTURE_2D, 0);

        glClear(GL_COLOR_BUFFER_BIT);
        if (budo_shader_stack_render(stack,
                                     texture,
                                     WINDOW_WIDTH,
                                     WINDOW_HEIGHT,
                                     WINDOW_WIDTH,
                                     WINDOW_HEIGHT,
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
    SDL_Quit();
    return 0;
}
