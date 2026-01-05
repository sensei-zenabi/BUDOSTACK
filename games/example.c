#include "../budo/budo_graphics.h"
#include "../budo/budo_input.h"

#define WIDTH 320
#define HEIGHT 200

int main(void) {

  // Set resolution
  budo_graphics_set_resolution(WIDTH, HEIGHT);

  // Let's measure the drawing speed in comparison to TASK scripts
  for (int x=1; x<WIDTH; x++) {
    for (int y=1; y<HEIGHT; y++) {
      budo_graphics_draw_pixel(x, y, 255, 200, 100, 1);
    }
  }
  
  budo_graphics_render_layer(1);

  //budo_graphics_clear_screen(640, 360, 1);

  return 0;

}
