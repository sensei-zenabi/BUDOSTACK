# BUDOSTACK - The Martial Art of Software
**Creator:** Ville Suoranta<br>
**Email:** ville.m.suoranta(at)gmail.com<br>
**Status:** Early Access (in development)

→ Check out website from [HERE](https://sensei-zenabi.github.io/suoranta/index.html)

## Description:
A lightweight operating "layer" built atop POSIX-compliant Linux, 
specifically designed for those who value the elegant simplicity 
and clarity found in operating systems of the 1980s. Optimized for 
maximum focus and efficiency on basic primitives of computing, 
such as file manipulation, text editing, command-line interactions, 
and efficient resource management.

Screenshots from BUDOSTACK built-in retro terminal emulator (apps/terminal).

| ![shot1](screenshots/login.png) | ![shot2](screenshots/demo.png) | ![shot3](screenshots/help.png) | ![shot4](screenshots/paint.png) |
|:---------------------------:|:---------------------------:|:---------------------------:|:---------------------------:|


## How to Install and Run?
1. Checkout the repo
2. Run ./setup.sh 
3. Run ./start.sh
4. Then type "help"

### Running outside the built-in terminal
* `./start.sh` still launches BUDOSTACK inside the retro-styled `apps/terminal` emulator with the CRT shader stack enabled by default.
* If you prefer to stay in your own GUI terminal emulator, you can run `./budostack` directly. The shell detects VTE/Konsole-style terminals and skips the resize escape sequence that used to displace the cursor, so the prompt and block cursor stay aligned.

## DOSBox Pure (libretro) frontend
BUDOSTACK now ships a minimal libretro frontend for the DOSBox Pure core. Provide the core path and a DOS game directory (or ZIP) to launch content, and the frontend will persist core data under `users/<name>/dosbox_pure/` (system + save directories).

Example usage:

```
./apps/dosbox_pure --core ./cores/dosbox_pure/dosbox_pure_libretro.so --content ./users/demo/dos_games/WOLF3D
```

Optional CRT shaders can be stacked (defaults to `shaders/crtscreen.glsl` when SDL2 is available):

```
./apps/dosbox_pure --core ./cores/dosbox_pure/dosbox_pure_libretro.so --content ./users/demo/dos_games/WOLF3D \\
  --shader ./shaders/crtscreen.glsl --shader ./shaders/noise.glsl
```

Use `--no-shader` to disable the CRT shader stack entirely.

If you drop DOSBox Pure sources into `cores/dosbox_pure/`, the makefile will build a `libdosbox_pure.a` static library and link it into the frontend automatically.

## Licence:
BUDOSTACK is distributed under GPL-2.0 license, which is a is a free 
copyleft license, that allows you to:
- Run the software for any purpose
- Study and modify the source code
- Redistribute copies, both original and modified, provided you will 
distribute them under the same GPL-2.0 terms and include the source 
code.

**Note!** Files shared under folders:
- ./fonts/
- ./shaders/
- ./sounds/

Are not distributed using the GLP-2.0 license. Instead, these folders 
contain their own LICENSE.txt files indicating their licensing conditions.
