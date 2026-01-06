/*********************************************************************************************

 --- budostack_sdl_graphics.h ---
 
 Collection of sdl graphics helpers to build your own applications and games for budostack.
 
*********************************************************************************************/

#ifndef BUDO_SDL_GRAPHICS_H
#define BUDO_SDL_GRAPHICS_H

#ifdef __cplusplus
extern "C" {
#endif

struct point3;
struct point2;

static void clear_buffer(uint32_t *pixels, int width, int height, uint32_t color);
static void put_pixel(uint32_t *pixels, int width, int height, int x, int y, uint32_t color);
static void draw_line(uint32_t *pixels, int width, int height, int x0, int y0, int x1, int y1, uint32_t color);
static struct point3 rotate_point(struct point3 p, float angle_x, float angle_y);
static struct point2 project_point(struct point3 p, int width, int height, float scale);

#ifdef __cplusplus
}
#endif

#endif
