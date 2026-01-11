#include "budo_graphics.h"
#include "budo_shader_stack.h"

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
    (void)argv;

    /* Initialize SDL */
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    

    /* Initialize Font */
    
    psf_font_t font;
    if (psf_font_load(&font, "./fonts/system.psf") != 0) {
      fprintf(stderr, "Failed to load PSF font: %s\n", "./fonts/system.psf");
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
    
    
    /* Define VSync:
     *
     *   SDL_GL_SetSwapInterval(0);   // uncapped framerate
     *   SDL_GL_SetSwapInterval(1);   // standard VSync
     *   SDL_GL_SetSwapInterval(-1);  // adaptive VSync 
    */
    
    SDL_GL_SetSwapInterval(1);


    /* Create and initialize the main RGBA texture used as the game framebuffer.
     * This texture will be updated each frame and rendered to the screen. 
    */

    GLuint texture = 0;
    glGenTextures(1, &texture);
    
    /* Texture creation failure is fatal: rendering cannot continue */
    
    if (texture == 0) {
        fprintf(stderr, "Failed to create GL texture.\n");
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }


    /* Configure texture for pixel-perfect rendering:
     * - Nearest filtering avoids smoothing (important for low-res / retro visuals)
     * - Clamp-to-edge prevents sampling artifacts at borders
    */ 
    
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    
    /* Allocate GPU storage for the framebuffer texture.
     * Data is provided later via glTexSubImage2D.
    */
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GAME_WIDTH, GAME_HEIGHT, 0, 
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);


    /* Allocate CPU-side pixel buffer matching the game resolution.
     * This buffer is used to compose each frame before uploading to the GPU texture.
     * Allocation failure is fatal, as rendering cannot proceed without it.
    */

    uint32_t *pixels = malloc((size_t)GAME_WIDTH * (size_t)GAME_HEIGHT * sizeof(uint32_t));
    if (!pixels) {
        fprintf(stderr, "Failed to allocate pixel buffer.\n");
        
        /* Clean up previously acquired graphics resources */
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

    
    /* Define BUDOSTACK shader paths */

    const char *shader_paths[] = {
      "./shaders/crtscreen.glsl"
    };
    
    
    /* Load BUDOSTACK shaders */
    
    if (budo_shader_stack_load(stack, shader_paths, 1u) != 0) {
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
        while (SDL_PollEvent(&event)) {
          
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


        /* Clear the CPU framebuffer with a packed 32-bit RGBA color.
         * Each pixel is stored as a uint32_t, matching SDL/OpenGL 32bpp formats
         * (one byte per channel). Channel interpretation is defined by the
         * GL_RGBA / GL_UNSIGNED_BYTE upload.
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

        
        /* Text overlay (draw AFTER cube, BEFORE uploading pixels to GL) */
        
        char hud[128];
        snprintf(hud, sizeof(hud), "ROTATING CUBE DEMO  FPS:%d  frame:%d", TARGET_FPS, frame_value);
        psf_draw_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 8, 8, hud, 0x00FFFFFFu);
        psf_draw_text(&font, pixels, GAME_WIDTH, GAME_HEIGHT, 8, 8 + (int)font.height,
                      "Exit with ESC", 0x00A0E0FFu);


        
        /* Upload the CPU-side framebuffer to the GPU texture.
         * Pixel data is tightly packed (1-byte alignment) and matches the
         * GL_RGBA / GL_UNSIGNED_BYTE texture format.
        */
        
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GAME_WIDTH, GAME_HEIGHT,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glBindTexture(GL_TEXTURE_2D, 0);

        /* Clear the default framebuffer before rendering the textured quad */
        
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

        
        /* Present the rendered frame by swapping the back buffer to the screen.
         * Swap timing is controlled by the configured swap interval (VSync).
        */
        
        SDL_GL_SwapWindow(window);
        
        
        /* Frame value is used by BUDOSTACK noise shader */
        
        frame_value++;


        /* Cap the frame rate (FPS) */
        
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
