# BUDOWIN for BUDOSTACK

Native port of sensei-zenabi/BUDOWIN, based on upstream commit
`5203fe933edeee72e486dbba3779493b819cac50`.

Build from the BUDOSTACK root with `make`, or run `budo/BUDOWIN/build.sh`.
The executable is `budo/budowin`; sources and modules live under
`budo/BUDOWIN/`. A directory and executable cannot share the same pathname.
No DOSBox, DJGPP, SDL, or Microsoft software is required by BUDOWIN.

The startup task asks **Start BUDOWIN?**. Answer `y` or `Y` to start the GUI
in HIGH (640x480) mode. Answer `n` to use the BUDOSTACK command line.
Launch manually with `./budo/budowin` inside apps/terminal.
Escape on the desktop exits to the terminal. Window title buttons close,
minimize, and maximize individual applications.

## Applications

- File Explorer: directories, file associations, copy/cut/paste, rename,
  create, delete, and native executable launch. Linux filenames and paths
  retain their case; symlink deletion removes only the link. Recursive copy
  rejects symlinks instead of following them.
- Editor: text and RTF editing, file dialogs, search/replace, writer layout,
  and PostScript export, using the original editor implementation.
- Paint: drawing tools, all 256 palette entries, palette-aware undo, and
  exact indexed PCX load/save. Use the palette arrow buttons or `[` / `]`
  to browse 16 colors at a time.
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
when artwork is absent. All supported PCX artwork (desktop, icons, cursor and native-app icons)
retains its original RGB values. Pixels matching an icon or cursor's top-left
RGB value are transparent. Each image has independent colors, so a 256-color
image does not change the UI or another image's palette. Presentation uses
RGB composition; the classic UI continues to use its system color indices.

## Shared selection and desktop shortcuts

Desktop launchers, shortcuts and File Explorer share the same selection rules.
Click an entry to select it. Only its 32x32 icon background changes; labels
and grid spacing retain their colors. A focus outline stays inside the icon.
Ctrl-click toggles individual entries. Shift-click selects a range from the
selection anchor; Ctrl+Shift-click adds a range. Ctrl+A selects all entries
matching the current filter, including entries on other pages. Click empty
space or press Escape to clear selection. Drag from empty space to select a
rectangle; Ctrl-drag toggles entries and Shift-drag adds them.

Arrow keys move selection. Home/End select the first/last entry; Page Up/Down
move by a page. Hold Shift to extend selection, Ctrl to move focus without
changing selection, or Ctrl+Shift to add a range. Space selects the focused
entry; Ctrl+Space toggles it. Enter opens it, F2 renames it, and Delete uses
the existing permanent-deletion confirmation.

Right-click an entry to open its context menu. Right-clicking a selected
entry preserves the group; an unselected entry becomes the sole selection.
Choose **Create Shortcut** (also available in the File menu) to place shortcuts
for selected documents and executables on the desktop. Folders are skipped;
duplicate targets are skipped. Shortcuts occupy free desktop grid slots and
persist in `$HOME/.budowin/shortcuts.state`.

Use the same click, Ctrl/Shift, Ctrl+A, keyboard and rectangle selection on
the desktop, including application launchers. Right-click to choose **Delete**
for selected shortcuts; application launchers are retained. This removes only shortcuts, preserving
the original files. Double-click or choose **Open** to open a document with
its configured association or run an executable from its own directory.
Shortcuts retain absolute target paths; moving or deleting a target does not
update its shortcut automatically.

## Editor search and editing

Find (`Ctrl+F`) and Replace (`Ctrl+H` or `Ctrl+R`) open a persistent
Find / Replace dialog. Search text, replacement text and options remain
available when it is reopened. Search supports case-sensitive matching,
whole words and optional wraparound, with visible match highlighting.
`F3` finds the next occurrence; `Shift+F3` finds the previous occurrence.
The dialog moves away from a match that would otherwise be hidden.

In the dialog, `Tab` switches fields and selects their text; `Ctrl+A`
selects the active field. Arrow keys, Home, End, Backspace and Delete edit
fields in place. `Enter` finds next, `Ctrl+P` finds previous, `Ctrl+R` or
`Ctrl+Enter` replaces the reviewed match, and `Ctrl+Shift+Enter` replaces
all. The first Replace action finds a match if none is currently reviewed.
`Ctrl+L`, `Ctrl+W` and `Ctrl+B` toggle case, whole-word and wrap options.
Escape or the dialog close button closes the dialog.

`Ctrl+Z` and `Ctrl+Y` undo and redo up to 32 editing steps. Continuous typing is grouped into words. Replace All is
one undo step and reports its replacement count. Every resulting line is
checked against the document capacity before Replace All changes anything.
New documents and file loads start a new history. Search currently matches
literal text within individual lines; it does not interpret regular
expressions or search across line breaks.

## Shared File workflows and scrolling

All five bundled applications use the same window, menu, button and
scrollbar style. Editor and Paint share the desktop file picker and
Save/Discard/Cancel prompts. New, Open, Close and desktop exit protect
unsaved documents; Save As confirms replacement. Editor saves text or RTF
according to the selected filename extension, and exports PostScript.
Paint prompts for a filename on its first save and supports PCX.

The picker supports full paths, type filters, folder creation, Up,
keyboard selection and editable filenames. Tab switches the list/name
field; Ctrl+A selects the name; arrow keys, Home, End, Backspace and Delete
edit it. Directory listings are allocated dynamically. Failed reads and
oversized text/RTF documents preserve the editor buffer. Writes use
checked temporary files and atomic replacement.

Scrollbars support arrow steps, track paging and draggable proportional
thumbs. Editor supports both axes and rows within wrapped lines; wrapping
hides the horizontal scrollbar. Paint scrolls its canvas, Settings scrolls
associations, Explorer scrolls pages, and Terminal scrolls output. Mouse
wheel scrolling leaves the editor caret in place.

Explorer's File menu exposes Open, Up, New Folder, Rename, Delete, Create
Shortcut and Close, with confirmation for deletion. Terminal offers Save Output,
Clear Output and Close. Settings exposes Close; association edits persist
through their existing host service rather than a document Save action.
The host file picker uses the shared selection engine for its single-file
list. Native applications can reuse `sdk/selection.h` and `sdk/ui.h` for
selectable lists and grids; see `docs/BWA.md`.

## Native modules

`APPS/*.BWA` are ELF shared libraries built from `appsrc/`. The five bundled
modules are built automatically. DOS DXE/BWA binaries are incompatible;
rebuild their C sources using `-fPIC -shared`. See `docs/BWA.md` for the
native interface. The exported entry is `bwa_entry`, without a leading
underscore. The original callback ABI is retained.

Run the native and graphics integration checks with
`tests/run_budowin.sh`. They require a C compiler and Python, but no SDL.

## Interaction feedback

Command buttons, window title buttons and bundled checkbox/toggle controls
show hover/pressed feedback and activate on release inside the original
control. Dragging outside cancels; returning inside before release permits
activation. Drawing, selection, window dragging and scrollbars continue to
start on press. Disabled command buttons ignore clicks. Native apps can
request disabled, focused and default button rendering through ABI 1.12.
Full keyboard focus traversal is a separate feature.

Active windows have blue title bars with white text; inactive windows have
light gray title bars with black text. Hovered controls use bright blue with
white labels; pressed/selected controls use dark blue. Disabled controls are
flat gray; default buttons have a double outline.
The pointer changes to an I-beam over Editor/search/file-name/Terminal input,
a crosshair over Paint's canvas, and a diagonal resize arrow over the existing
bottom-right resize grip. Blocking document reads/writes and Explorer paste
show an hourglass; this feedback does not make file operations asynchronous.
Custom arrow artwork remains available outside these contexts. Contextual
pointers occupy at most 11x11 pixels around their hotspot, sized against the
5x7 font in its 6x9 text cell. The blinking edit caret now fills that cell and
inverts the glyph beneath it; search/file-name insertion bars are 2x9 pixels.
Dropdown menus keep the arrow pointer above the underlying edit area.

Hover for 600 ms to see tips for command/frame buttons, Paint tools, scroll
arrows, resize grips and truncated file/shortcut names. Tips stay on screen,
wrap long names, and disappear on movement or button press. Scroll arrows
and tracks repeat after a 350 ms initial delay, then at 60 ms intervals while
held over their original target. Leaving suspends repeat; returning restarts
the initial delay. Releasing stops repeat.
