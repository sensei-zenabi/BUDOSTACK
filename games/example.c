#include "../budo/budo_graphics.h"
#include "../budo/budo_input.h"

#define BUDO_SCREEN_WIDTH 640
#define BUDO_SCREEN_HEIGHT 360

int main(void) {

  // Set resolution
  budo_graphics_set_resolution(640, 360);

  // Let's measure the drawing speed in comparison to TASK scripts
  for (int x=1; x<50; x++) {
    for (int y=1; y<50; y++) {
      budo_graphics_draw_pixel(x, y, 255, 200, 100, 1);
    }
  }
  
  budo_graphics_render_layer(1);

  budo_graphics_clear_screen(640, 360, 1);

  return 0;

}
