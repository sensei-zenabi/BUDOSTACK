# BUDOWIN for BUDOSTACK

Native port of sensei-zenabi/BUDOWIN, based on upstream commit
`5203fe933edeee72e486dbba3779493b819cac50`.

Build from the BUDOSTACK root with `make`, or run `budo/budowin/build.sh`.
The executable is `budo/BUDOWIN`; sources and modules live under
`budo/budowin/`. A directory and executable cannot share the same pathname.
No DOSBox, DJGPP, SDL, or Microsoft software is required by BUDOWIN.

The startup task asks **Start BUDOWIN?**. Answer `y` or `Y` to start the GUI
in HIGH (640x480) mode. Answer `n` to use the BUDOSTACK command line.
Launch manually with `./budo/BUDOWIN` inside apps/terminal.
Escape on the desktop exits to the terminal. Window title buttons close,
minimize, and maximize individual applications.

## Applications

- File Explorer: directories, file associations, copy/cut/paste, rename,
  create, delete, and native executable launch. Linux filenames and paths
  retain their case; symlink deletion removes only the link. Recursive copy
  rejects symlinks instead of following them.
- Editor: text and RTF editing, file dialogs, search/replace, writer layout,
  and PostScript export, using the original editor implementation.
- Paint: drawing tools, palette, undo, and PCX load/save.
- Settings: native application associations and the original settings UI.
- Terminal: command editing/history, persistent cd, output capture and
  scrollback. BUDOSTACK applications release the desktop transport before
  launch and resume BUDOWIN afterwards. Ordinary commands run through
  `/bin/sh` with captured output and no interactive stdin.

Assets/modules resolve relative to the executable. Explorer and Editor
start in the BUDOSTACK user directory when BUDOSTACK_BASE is supplied,
otherwise the launch directory. Relative Paint paths use that directory.
User configuration and session files are stored under `$HOME/.budowin`.

Optional upstream PCX artwork can be placed in `PCX/` without recompiling.
This port uses the original built-in icon drawings and a native arrow cursor
when artwork is absent. Binary artwork could not be retrieved through the
GitHub connector used for this port.

## Native modules

`APPS/*.BWA` are ELF shared libraries built from `appsrc/`. The five bundled
modules are built automatically. DOS DXE/BWA binaries are incompatible;
rebuild their C sources using `-fPIC -shared`. See `docs/BWA.md` for the
native interface. The exported entry is `bwa_entry`, without a leading
underscore. The original callback ABI is retained.

Run the native and graphics integration checks with
`tests/run_budowin.sh`. They require a C compiler and Python, but no SDL.
