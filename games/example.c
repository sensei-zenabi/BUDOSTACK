#include "../budo/budo_graphics.h"
#include "../budo/budo_input.h"

#define BUDO_SCREEN_WIDTH 640
#define BUDO_SCREEN_HEIGHT 360

int main(void) {

  // Set resolution
  budo_graphics_set_resolution(BUDO_SCREEN_WIDTH, BUDO_SCREEN_HEIGHT, 0);

  // Let's measure the drawing speed in comparison to TASK scripts
  for (int x=1; x<10; x++) {
    for (int y=1; y<10; y++) {
      budo_graphics_draw_pixel(x, y, 255, 200, 100, 0);
    }
  }

  return 0;

}
