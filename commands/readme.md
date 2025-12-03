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
