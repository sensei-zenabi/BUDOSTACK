# BUDO pixel applications

`budo/` contains native pixel applications displayed inside `apps/terminal`.
Each application keeps its assets in a corresponding folder:

- `example.c` → `example`, with assets in `EXAMPLE/`
- `rocket.c` → `rocket`, with assets in `ROCKET/`

Build with `make clean all` from the repository root, or `./budo/build.sh`.
SDL2 development files are required. SDL_image and SDL_mixer remain optional;
Rocket music and sound need SDL_mixer with S3M support. The demos use SDL for
asset loading, timers and audio, and do not create their own windows or GL
contexts. Graphics use the terminal's shaders and overlay settings.

Inside the BUDOSTACK terminal:

```
cd budo
./example
./rocket
```

Launching `./budo/example` or `./budo/rocket` from another working directory also
works: asset paths resolve relative to the executable. Launching outside
`apps/terminal` reports an error instead of opening another window.

Example: arrows adjust cube size, Escape exits.
Rocket: arrows and Enter navigate menus; arrows move, Space fires, Escape
returns to the menu. Select EXIT to return to the shell. Alt+1 through Alt+5
still switch terminal tabs. Focus loss and tab changes clear held keys.
Applications may continue running on inactive tabs, but only the active tab
receives keyboard/mouse input and is displayed. Exit or crash closes the
connection and restores the text display without resizing the text grid.

## Application interface

`../lib/budo_gfx.h` defines the versioned C interface and host API. Applications
open a logical framebuffer, poll input, present complete frames and close.
Formats are numeric ARGB8888 (alpha ignored) and INDEX8 with a configurable
256-entry ARGB8888 palette. 320×200 VGA-style frames are supported; both existing
demos retain 640×360 to preserve their layout and gameplay. This is a native
pixel graphics mode, not DOS binary or VGA hardware emulation.

The terminal exports a private Unix `SOCK_SEQPACKET` endpoint per tab through
`BUDOSTACK_GFX_SOCKET`. Protocol v1 uses fixed-width control/event fields,
SCM_RIGHTS to transfer an unlinked shared-memory file, and two frame slots.
The client publishes a slot and waits for acknowledgement after the terminal
copies it into its own RGBA buffer. Acknowledged slots may be reused; the
terminal never renders from a slot being written. Input is queued while the
client waits, with bounded timeouts and queue capacity. Dimensions, protocol
version, shared-file size and frame slots are validated by the host.

The terminal owns all OpenGL resources. Screens use nearest-neighbour filtering. With the overlay disabled they fill
the entire native display, including 1280×1024 screens. With the overlay enabled
they fill the configured display area and use its offsets (`OVERLAY_WIDTH`,
`OVERLAY_HEIGHT`, `OVERLAY_OFFSET_X`, `OVERLAY_OFFSET_Y` in `config.ini`).
Horizontal and vertical scaling are independent, so the image fills the target
area even when its aspect ratio differs from the application framebuffer.

`lib/budo_screen.h` is an SDL convenience adapter used by these demos. It
translates transport events to SDL events and maintains held-key state.
`lib/budo_graphics.h` provides software drawing and PSF font helpers.
`lib/budo_audio.h` provides optional SDL_mixer audio. The standalone shader
library remains available, but embedded applications use terminal shaders.

## Verification

Run `./tests/run_budo_gfx.sh` from the repository root. The transport checks use
real private Unix endpoints by default. In a sandbox that blocks Unix socket
creation, set `BUDO_GFX_TEST_SOCKETPAIR=1`: only endpoint establishment is
substituted; FD transfer, shared memory, packets and disconnects are unchanged.
The offscreen OpenGL checks run the actual demo executables using inherited
socketpairs, exercise input/assets/exit/crash, and compare GPU texture data with
submitted frames. Set `SDL_VIDEODRIVER=x11` if offscreen GL is unavailable.
