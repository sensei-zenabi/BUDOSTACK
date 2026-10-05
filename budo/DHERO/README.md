# Dungeon Hero

Native BUDOSTACK port of https://github.com/sensei-zenabi/dosgame1.git at
`7ff669a35987858d2b37f06de182658c4dc7e8b6`.

Build with `make clean all` or `./budo/build.sh`. The generated executable is
`budo/dhero`, following Rocket's source/build layout; generated binaries are
not committed. Run inside `apps/terminal`.

All original assets are preserved byte-for-byte in `DATA/`, including the PCX
images, Aseprite palette, ten maps, definitions, and both language dictionaries.
`original/` contains the unmodified DOS sources, build files, and documentation.
The native sources retain the game rules; `runtime.c` replaces BIOS/VGA writes,
DOS input and PC speaker access with the shared native INDEX8 transport, PSF
text rendering, POSIX timing, and optional SDL2 queued square-wave audio.
No separate window or graphics context is created. The original logical
320×200 resolution is retained in both BUDO build modes.

## Controls

- Game: arrows move, Enter interacts/attacks, Space opens inventory, Escape returns.
- Menus: arrows select/change, Enter confirms, Escape closes.
- Options: E opens the editor; sound and ENG/FIN selection persist.
- Editor: arrows move, Tab changes tool, +/- selects, Enter applies, Delete removes.
- Editor: R rotates, G changes chest gold, I changes chest item, F1 shows help.
- Editor: Page Up/Down changes level, F2 saves and resets scores, Escape exits
  (a second Escape discards unsaved edits).

`./budo/dhero --editor` opens the editor directly. The editor runs within the
same process and terminal framebuffer. Full original editor documentation is
in `original/EDITOR.MD`.

Settings and scores are stored in `DATA/GAME/SETTINGS.DAT` and `SCORES.DAT`.
Editor saves update maps in `DATA/GFX/MAP/`; these locations must be writable.
The game locates DHERO relative to its executable, so launch CWD does not matter.
On builds without SDL2, speaker sequences retain their timing but are silent.

## Verification

`./tests/run_dhero.sh` builds the port and tests the actual executable using
private socketpairs and the production shared-memory graphics transport.
Checks cover launch from another directory, native PCX/palette frames, menu,
Options, embedded editor, campaign, inventory, direct editor level switching
and help, and clean exit. `./tests/run_budo_gfx.sh` checks the shared transport.
