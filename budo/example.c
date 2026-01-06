#include "budo_shader_stack.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#define GAME_WIDTH 320
#define GAME_HEIGHT 200
#define TARGET_FPS 30

//--------------------------------------------------------------------------------------------
/* DEFINE STRUCTS */

struct point3 {
    float x;
    float y;
    float z;
};

struct point2 {
    float x;
    float y;
};

//--------------------------------------------------------------------------------------------
// PSF FONT (PSF1 / PSF2) LOADER + CPU BLITTER
// Renders monochrome glyphs directly into your uint32_t framebuffer.

#pragma pack(push, 1)
typedef struct {
    uint16_t magic;     // 0x0436 (little-endian)
    uint8_t  mode;
    uint8_t  charsize;  // bytes per glyph (== height for PSF1, width fixed 8)
} psf1_header_t;

typedef struct {
    uint32_t magic;      // 0x864ab572 (little-endian)
    uint32_t version;    // 0
    uint32_t headersize; // typically 32
    uint32_t flags;
    uint32_t length;     // glyph count
    uint32_t charsize;   // bytes per glyph
    uint32_t height;
    uint32_t width;
} psf2_header_t;
#pragma pack(pop)

typedef struct {
    uint8_t *file_buf;       // owned
    size_t   file_size;

    uint32_t glyph_count;
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_glyph;

    const uint8_t *glyphs;   // points into file_buf
} psf_font_t;

static uint8_t *read_file_all(const char *path, size_t *out_size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); return NULL; }

    uint8_t *buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(fp); return NULL; }

    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        free(buf);
        return NULL;
    }

    fclose(fp);
    *out_size = (size_t)sz;
    return buf;
}

static void psf_font_destroy(psf_font_t *f) {
    if (!f) return;
    free(f->file_buf);
    memset(f, 0, sizeof(*f));
}

static int psf_font_load(psf_font_t *f, const char *path) {
    if (!f || !path) return -1;
    memset(f, 0, sizeof(*f));

    f->file_buf = read_file_all(path, &f->file_size);
    if (!f->file_buf || f->file_size < 4) return -1;

    // Try PSF1
    if (f->file_size >= sizeof(psf1_header_t)) {
        const psf1_header_t *h1 = (const psf1_header_t*)f->file_buf;
        if (h1->magic == 0x0436) {
            f->width = 8;
            f->height = (uint32_t)h1->charsize;
            f->bytes_per_glyph = (uint32_t)h1->charsize;
            f->glyph_count = (h1->mode & 0x01) ? 512u : 256u;

            size_t header_sz = sizeof(psf1_header_t);
            size_t glyph_bytes = (size_t)f->glyph_count * (size_t)f->bytes_per_glyph;
            if (f->file_size < header_sz + glyph_bytes) {
                psf_font_destroy(f);
                return -1;
            }

            f->glyphs = f->file_buf + header_sz;
            return 0;
        }
    }

    // Try PSF2
    if (f->file_size >= sizeof(psf2_header_t)) {
        const psf2_header_t *h2 = (const psf2_header_t*)f->file_buf;
        if (h2->magic == 0x864ab572u) {
            f->glyph_count = h2->length;
            f->width = h2->width;
            f->height = h2->height;
            f->bytes_per_glyph = h2->charsize;

            size_t header_sz = (size_t)h2->headersize;
            size_t glyph_bytes = (size_t)f->glyph_count * (size_t)f->bytes_per_glyph;
            if (f->file_size < header_sz + glyph_bytes) {
                psf_font_destroy(f);
                return -1;
            }

            f->glyphs = f->file_buf + header_sz;
            return 0;
        }
    }

    psf_font_destroy(f);
    return -1;
}

// Draw a single glyph at (x,y) into RGBA8888 pixels.
// color is 0x00RRGGBB in your codebase (alpha ignored).
static void psf_draw_glyph(const psf_font_t *f,
                           uint32_t *pixels, int fb_w, int fb_h,
                           int x, int y, uint8_t glyph_index, uint32_t color) {
    if (!f || !f->glyphs || !pixels) return;
    if (fb_w <= 0 || fb_h <= 0) return;

    uint32_t gi = (uint32_t)glyph_index;
    if (gi >= f->glyph_count) gi = (uint32_t)'?';

    const uint8_t *glyph = f->glyphs + (size_t)gi * (size_t)f->bytes_per_glyph;

    uint32_t bytes_per_row = (f->width + 7u) / 8u;

    for (uint32_t row = 0; row < f->height; row++) {
        int py = y + (int)row;
        if (py < 0 || py >= fb_h) continue;

        const uint8_t *rowbits = glyph + row * bytes_per_row;

        for (uint32_t col = 0; col < f->width; col++) {
            int px = x + (int)col;
            if (px < 0 || px >= fb_w) continue;

            uint32_t byte_i = col / 8u;
            uint32_t bit_i  = 7u - (col % 8u); // PSF bit order (MSB first) is common
            uint8_t on = (rowbits[byte_i] >> bit_i) & 1u;

            if (on) {
                pixels[(size_t)py * (size_t)fb_w + (size_t)px] = color;
            }
        }
    }
}

static void psf_draw_text(const psf_font_t *f,
                          uint32_t *pixels, int fb_w, int fb_h,
                          int x, int y, const char *text, uint32_t color) {
    if (!f || !text) return;

    int pen_x = x;
    int pen_y = y;

    for (const unsigned char *p = (const unsigned char*)text; *p; p++) {
        unsigned char ch = *p;

        if (ch == '\n') {
            pen_x = x;
            pen_y += (int)f->height;
            continue;
        } else if (ch == '\r') {
            pen_x = x;
            continue;
        } else if (ch == '\t') {
            pen_x += (int)f->width * 4;
            continue;
        }

        psf_draw_glyph(f, pixels, fb_w, fb_h, pen_x, pen_y, ch, color);
        pen_x += (int)f->width;
    }
}


//--------------------------------------------------------------------------------------------
/* HELPER FUNCTIONS */

static void clear_buffer(uint32_t *pixels, int width, int height, uint32_t color) {
    if (!pixels || width <= 0 || height <= 0) {
        return;
    }
    size_t total = (size_t)width * (size_t)height;
    for (size_t i = 0; i < total; i++) {
        pixels[i] = color;
    }
}

/* Helper function to draw pixels */
static void put_pixel(uint32_t *pixels, int width, int height, int x, int y, uint32_t color) {
    if (!pixels || x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }
    pixels[(size_t)y * (size_t)width + (size_t)x] = color;
}

/* Draw a line using put_pixel */
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


//--------------------------------------------------------------------------------------------
/* MAIN LOOP */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Initialize SDL */
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    

    /* Initialize Font */
    
    psf_font_t font;
    if (psf_font_load(&font, "../fonts/system.psf") != 0) {
      fprintf(stderr, "Failed to load PSF font: %s\n", "../fonts/system.psf");
      SDL_Quit();
      return 1;
    }


    /* Configure SDL GL */
    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    
    
    /* Query desktop display mode */
    
    SDL_DisplayMode desktop_mode;
    if (SDL_GetCurrentDisplayMode(0, &desktop_mode) != 0) {
      fprintf(stderr, "Failed to query desktop display mode: %s\n", SDL_GetError());
      SDL_Quit();
      return 1;
    }
  
    
    /* Create the Application Window */

    SDL_Window *window = SDL_CreateWindow("Budo Shader Stack Demo",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          desktop_mode.w,
                                          desktop_mode.h,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI);
    
    if (!window) {
        fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    
    /* Create SDL GL context */
    
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        fprintf(stderr, "Failed to create GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    
    /* Query window drawable size */
    
    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
    if (drawable_width <= 0 || drawable_height <= 0) {
      SDL_GetWindowSize(window, &drawable_width, &drawable_height);
    }

    SDL_GL_SetSwapInterval(1);


    /* Create and Configure SDL GL texture */

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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GAME_WIDTH, GAME_HEIGHT,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);


    /* Allocate pixel buffer */

    uint32_t *pixels = malloc((size_t)GAME_WIDTH * (size_t)GAME_HEIGHT * sizeof(uint32_t));
    if (!pixels) {
        fprintf(stderr, "Failed to allocate pixel buffer.\n");
        glDeleteTextures(1, &texture);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    
    /* Initialize BUDOSTACK shader stack */

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

    
    /* Define shader paths */

    const char *shader_paths[] = {
        "../shaders/crtscreen.glsl",
        "../shaders/noise.glsl",
        "../shaders/effects.glsl"
    };
    
    
    /* Load shaders */
    
    if (budo_shader_stack_load(stack, shader_paths, 3u) != 0) {
        fprintf(stderr, "Failed to load shaders.\n");
        budo_shader_stack_destroy(stack);
        free(pixels);
        glDeleteTextures(1, &texture);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    
    /* Create the Cube */ 

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
    
    
    /* Initialize Demo */

    int running = 1;
    Uint32 last_tick = SDL_GetTicks();
    float angle = 0.0f;
    int frame_value = 0;
    
    
    /* Demo Loop */

    while (running) {
    
        /* Handle SDL events */
        
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
          switch (event.type) {
            case SDL_QUIT:
              running = 0;
              break;

            case SDL_WINDOWEVENT:
              if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                  event.window.event == SDL_WINDOWEVENT_RESIZED) {

                SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
                if (drawable_width <= 0 || drawable_height <= 0) {
                      SDL_GetWindowSize(window, &drawable_width, &drawable_height);
                }
              }
              break;

            default:
              break;
          }
        }

        
        /* Calculate delta time for FPS */
        
        Uint32 now = SDL_GetTicks();
        float delta = (float)(now - last_tick) / 1000.0f;
        last_tick = now;
        angle += delta;


        /* Clear frame buffer */
        
        clear_buffer(pixels, GAME_WIDTH, GAME_HEIGHT, 0x00101010u);

        
        /* Rotate cube */

        struct point2 projected[8];
        for (size_t i = 0; i < 8; i++) {
            struct point3 rotated = rotate_point(cube_vertices[i], angle * 0.7f, angle);
            projected[i] = project_point(rotated, GAME_WIDTH, GAME_HEIGHT, 120.0f);
        }

        for (size_t i = 0; i < 12; i++) {
            int a = edges[i][0];
            int b = edges[i][1];
            draw_line(pixels,
                      GAME_WIDTH,
                      GAME_HEIGHT,
                      (int)projected[a].x,
                      (int)projected[a].y,
                      (int)projected[b].x,
                      (int)projected[b].y,
                      0x00f0d060u);
        }

        /* Text overlay (draw AFTER cube, BEFORE uploading pixels to GL) */
        char hud[128];
        snprintf(hud, sizeof(hud), "BUDOSTACK DEMO  FPS:%d  frame:%d", TARGET_FPS, frame_value);
        psf_draw_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 8, 8, hud, 0x00FFFFFFu);
        psf_draw_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 8, 8 + (int)font.height,
                      "system.psf overlay", 0x00A0E0FFu);


        
        /* Draw cube */
        
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GAME_WIDTH, GAME_HEIGHT,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glBindTexture(GL_TEXTURE_2D, 0);

        glClear(GL_COLOR_BUFFER_BIT);
        
        
        /* Render shaders */
        
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


        /* Cap frame rate (FPS) */
        
        Uint32 frame_ms = SDL_GetTicks() - now;
        Uint32 target_ms = 1000u / TARGET_FPS;
        if (frame_ms < target_ms) {
            SDL_Delay(target_ms - frame_ms);
        }
    }


    /* Clean-Up before Exit */
     
    budo_shader_stack_destroy(stack);
    free(pixels);
    glDeleteTextures(1, &texture);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    psf_font_destroy(&font);
    SDL_Quit();
    
    return 0;
}
