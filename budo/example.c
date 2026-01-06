#include <stdio.h>

static void budo_graphics_pixel(int x, int y) {

  char payload[256];
  

  /* Parse to string */
  
  snprintf(payload, sizeof(payload),
           "pixel=draw;pixel_x=%d;pixel_y=%d;pixel_r=255;pixel_g=255;pixel_b=255",
           x, y);


  /* OSC = ESC ] ... BEL */
  
  printf("\x1b]777;%s\x07", payload);
  fflush(stdout);
  
}

static void budo_graphics_render() {
  
  printf("\x1b]777;%s\x07", "pixel=render;pixel_layer=1");    
  fflush(stdout);
  
}

//-----------------------------------------------------------------------------

int main(void) {

    
  for (int x=200; x<600; x++) {
    for (int y=150; y<250; y++) {    
      budo_graphics_pixel(x, y);
    }
  }
  
  budo_graphics_render();
        
  return 0;
}




