#include "../budo/budo_graphics.h"
#include "../budo/budo_input.h"

#include <stdlib.h>

#define WIDTH 320
#define HEIGHT 200

int main(void) {

  // Set resolution
  budo_graphics_set_resolution(WIDTH, HEIGHT);

  size_t pixel_count = (size_t)WIDTH * (size_t)HEIGHT;
  size_t buffer_size = pixel_count * 4u;
  uint8_t *pixels = malloc(buffer_size);
  if (!pixels) {
    return 1;
  }

  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      size_t index = ((size_t)y * (size_t)WIDTH + (size_t)x) * 4u;
      pixels[index + 0u] = (uint8_t)((x * 255) / (WIDTH - 1));
      pixels[index + 1u] = (uint8_t)((y * 255) / (HEIGHT - 1));
      pixels[index + 2u] = 100u;
      pixels[index + 3u] = 255u;
    }
  }

  budo_graphics_draw_frame_rgba(WIDTH, HEIGHT, pixels);
  free(pixels);

  return 0;

}
