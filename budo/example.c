#include "lib/budo_graphics.h"
#include "lib/budo_screen.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#define GAME_WIDTH 640
#define GAME_HEIGHT 360
#define TARGET_FPS 30
#define CUBE_SIZE 220.0f

/*--------------------------------------------------------------------------------------------
 * DEFINE STRUCTS 
*/

struct point3 {
    float x;
    float y;
    float z;
};

struct point2 {
    float x;
    float y;
};


/* Rotate a 3D point around the X and Y axes.
 *
 * Rotations are applied in the following order:
 *   1) Rotation around the X axis (pitch)
 *   2) Rotation around the Y axis (yaw)
 *
 * Angles are specified in radians and follow the right-handed
 * coordinate system convention.
 *
 * The rotation is performed about the origin.
*/

static struct point3 rotate_point(struct point3 p, float angle_x, float angle_y) {
    
    /* Precompute sine/cosine for efficiency */
    
    float cx = cosf(angle_x);
    float sx = sinf(angle_x);
    float cy = cosf(angle_y);
    float sy = sinf(angle_y);

    /* Rotate around X axis */
    
    float y = p.y * cx - p.z * sx;
    float z = p.y * sx + p.z * cx;
    p.y = y;
    p.z = z;

    /* Rotate around Y axis */

    float x = p.x * cy + p.z * sy;
    z = -p.x * sy + p.z * cy;
    p.x = x;
    p.z = z;

    return p;
}


/* Project a 3D point into 2D screen space using a simple perspective model.
 *
 * The camera is assumed to be at the origin looking down the +Z axis.
 * A constant Z-offset is applied to avoid division by zero and to place
 * geometry in front of the camera.
 *
 * The resulting coordinates are centered in the viewport, with +Y pointing
 * upward in world space and downward in screen space.
*/

static struct point2 project_point(struct point3 p, int width, int height, float scale) {

    /* Shift depth to keep points in front of the camera */
    
    float depth = p.z + 3.0f;
    
    /* Perspective divide (safe against zero depth) */
    
    float inv = depth != 0.0f ? (1.0f / depth) : 1.0f;
    
    /* Map projected coordinates to screen space */
    
    struct point2 out;
    out.x = (float)width * 0.5f + p.x * scale * inv;
    out.y = (float)height * 0.5f - p.y * scale * inv;
    
    return out;
}


/*--------------------------------------------------------------------------------------------
 * MAIN LOOP 
*/

int main(int argc, char **argv) {
    (void)argc;

    if (SDL_Init(SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL timer init failed: %s\n", SDL_GetError());
        return 1;
    }
    char font_path[4096];
    psf_font_t font;
    if (budo_asset_path(font_path, sizeof(font_path), argv[0], "EXAMPLE/fonts/system.psf") < 0 ||
        psf_font_load(&font, font_path) != 0) {
        fprintf(stderr, "Failed to load application font.\n");
        SDL_Quit();
        return 1;
    }
    struct budo_screen screen;
    if (budo_screen_open(&screen, GAME_WIDTH, GAME_HEIGHT) < 0) {
        psf_font_destroy(&font);
        SDL_Quit();
        return 1;
    }
    uint32_t *pixels = malloc((size_t)GAME_WIDTH * GAME_HEIGHT * sizeof(*pixels));
    if (!pixels) {
        perror("allocate framebuffer");
        budo_screen_close(&screen);
        psf_font_destroy(&font);
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
    
    float cube_size = CUBE_SIZE;
    
    
    /* Initialize Demo */

    int running = 1;
    Uint32 last_tick = SDL_GetTicks();
    float angle = 0.0f;
    int frame_value = 0;
    
    
    /* Demo Loop */

    while (running) {
    
        /* Handle SDL events */
        
        SDL_Event event;
        while (budo_screen_poll(&screen, &event)) {
          
          switch (event.type) {
            
            case SDL_QUIT:
              running = 0;
              break;
              
            case SDL_KEYDOWN:
              if (event.key.keysym.sym == SDLK_ESCAPE) {
                running = 0;
              }
              if (event.key.keysym.sym == SDLK_UP) {
                cube_size = cube_size + 1.0f;
              }
              if (event.key.keysym.sym == SDLK_DOWN) {
                cube_size = cube_size - 1.0f;
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


        /* Clear the CPU framebuffer with a packed 32-bit RGBA color.
         * Each pixel is stored as a uint32_t, in numeric ARGB8888 format
         * (one byte per channel). The terminal converts it to RGBA bytes for display.
        */
        
        budo_clear_buffer(pixels, GAME_WIDTH, GAME_HEIGHT, 0x00101010u);

        
        /* Transform and render the cube:
         *  - Rotate each 3D vertex in model space
         *  - Project rotated vertices into 2D screen space
         *  - Rasterize cube edges as screen-space lines
        */

        struct point2 projected[8];
        for (size_t i = 0; i < 8; i++) {
            struct point3 rotated = rotate_point(cube_vertices[i], angle * 0.7f, angle);
            projected[i] = project_point(rotated, GAME_WIDTH, GAME_HEIGHT, cube_size); //120.0f
        }

        /* Draw all cube edges using the projected vertices */
        
        for (size_t i = 0; i < 12; i++) {
            int a = edges[i][0];
            int b = edges[i][1];
            budo_draw_line(pixels,
                           GAME_WIDTH,
                           GAME_HEIGHT,
                           (int)projected[a].x,
                           (int)projected[a].y,
                           (int)projected[b].x,
                           (int)projected[b].y,
                           0x00f0d060u);
        }

        
        /* Draw text into the application framebuffer. */
        
        char hud[128];
        snprintf(hud, sizeof(hud), "ROTATING CUBE DEMO  FPS:%d  frame:%d", TARGET_FPS, frame_value);
        psf_draw_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 8, 8, hud, 0x00FFFFFFu);
        psf_draw_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 8, 8 + (int)font.height,
                      "Exit with ESC", 0x00A0E0FFu);


        
        /* Publish a complete frame to apps/terminal. */
        if (!running || budo_screen_present(&screen, pixels) < 0) {
            running = 0;
        }

        frame_value++;


        /* Cap the frame rate (FPS) */
        
        Uint32 frame_ms = SDL_GetTicks() - now;
        Uint32 target_ms = 1000u / TARGET_FPS;
        if (frame_ms < target_ms) {
            SDL_Delay(target_ms - frame_ms);
        }
    }


    /* Clean-Up before Exit */
     
    free(pixels);
    budo_screen_close(&screen);
    psf_font_destroy(&font);
    SDL_Quit();
    
    return screen.disconnected ? 1 : 0;
}
