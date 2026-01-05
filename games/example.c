#include "../budo/budo_graphics.h"
#include "../budo/budo_input.h"

#define BUDO_SCREEN_WIDTH 640
#define BUDO_SCREEN_HEIGHT 360

int main(void) {

  // Set resolution
  budo_graphics_set_resolution(BUDO_SCREEN_WIDTH, BUDO_SCREEN_HEIGHT);

  // Let's measure the drawing speed in comparison to TASK scripts
  for (int i=0; i<100; i++) {
    budo_graphics_draw_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
  }

}
