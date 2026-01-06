Folder to store BUDOSTACK TASK language commands.

Commands with prefix _TERM*:

- Intended to be used only in TASK scripts that are ran inside 
  apps/terminal terminal emulator.
- Are optimized for SDL2 using a resolution of 640x360.
  
Commands without prefix _TERM*: 

- Can be used in all TASK scripts without dependency to apps/terminal 
  terminal emulator.
- Are optimized for 78 columns and 42 rows.

General Guidelines for Commands:

- Written with plain C with no separate header files.
- #define _POSIX_C_SOURCE 200112L  // Enable POSIX.1-2001 features.
- Follow the active BUDOSTACK color scheme if not otherwise defined.
- Measure X and Y positions from top-left corner as glyphs or pixels.
- Write their output to stdout excluding _TERM* commands.
- Print help when command is written to command line without any arguments.
- 

Recent terminal pixel helpers:

- `_TERM_PIXELS`: Uploads a block of RGBA pixels (base64) to the terminal surface.
- `_TERM_SPRITE_CACHE`: Caches sprites for fast repeated drawing.
- `_TERM_SPRITE_DRAW`: Draws cached sprites by id.
- `_TERM_SPRITE_FREE`: Frees cached sprites.
- `_TERM_FAST`: Toggles fast render mode (disables shaders and frame limiting).
- `_TERM_BENCH`: Toggles benchmark logging for pixel throughput.
