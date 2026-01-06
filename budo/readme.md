# ./budo/

A folder reserved to contain .h/.c libraries used for BUDOSTACK C
application development.

Files:

  budo_graphics.h/.c
  
    Provides direct access to apps/terminal graphics pipeline. Enables
    fast full screen pixel-per-pixel rendering to enable creating
    legendary games like DOOM in MS-DOS back in the 1990s.
    
    Methods:
    
      budo_graphics_init(int width, int height, int r, int g, int b);
      
        Initializes screen to defined width and height resolution and 
        clears all layers 0-15 to defined r,g,b color.
        
        
      budo_graphics_pixel(int x, int y, int r, int g, int b, int layer);
      
        Stages individual r,g,b pixel to be drawn into a spesific layer
        using the rendering pipeline controlled by budo_graphics_render().
        
        
      budo_graphics_clean(int x, int y, int layer);
      
        Unstages (cleans) a pixel from a given layer from the rendering 
        pipeline controlled by budo_graphics_render(). 
      
      
      budo_graphics_text(int x, int y, const char *str, int layer);
      
        Stages ./fonts/system.psf font to be draw as pixel graphics to
        a given layer. Note! Size is dependent to the resolution set with
        budo_graphics_init(), as system.psf is 8x8 pixel font.
        
      
      budo_graphics_render(int layer);
      
        Applies all staged or unstaged pixels to the rendering pipeline
        of apps/terminal to be drawn to the screen.
      
