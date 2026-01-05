# ./budo/

Libraries that provide MS-DOS-inspired access to apps/terminal graphics, sound,
input, and other low-level capabilities for BUDOSTACK C application developers.

These libraries are intended for use by applications and games to access
terminal-era graphics, audio, and input features with a consistent API.

Current modules:
- `budo_graphics.h` / `budo_graphics.c`: ANSI terminal drawing helpers.
- `budo_input.h` / `budo_input.c`: Raw input polling with arrow-key support.
- `budo_sound.h` / `budo_sound.c`: Simple PC-speaker style beep helper.
