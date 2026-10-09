# Native BWA modules

BUDOSTACK BUDOWIN extends upstream BWA ABI to 1.15 (`src/bwa.h`). Modules use
`BwaHostApi` for drawing, managed windows, binary file access and allocation.
The built-in Explorer, Editor, Terminal, Reader, Help and Recycle Bin launcher modules delegate to
host-managed application implementations; Paint and Settings own their
callbacks and managed windows.

Build a module on Linux:

```sh
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -fPIC -shared \
  appsrc/paint.c -o APPS/PAINT.BWA
```

Export `int bwa_entry(const BwaHostApi *, BwaAppDefinition *)`. Modules are
loaded with `dlopen(RTLD_NOW | RTLD_LOCAL)` and `dlsym("bwa_entry")`.
The `.BWA` suffix is retained for discovery; files contain native ELF code.
DOS DJGPP DXE modules must be rebuilt, and ABI compatibility does not imply
binary compatibility across platforms or architectures.

Callbacks receive ASCII keys, including Ctrl+A..Z as 1..26. Extended DOS
scan codes are delivered as `0x100 | scan_code` to external modules. Pointer
coordinates are logical VGA pixels. Right button uses bit 2 and left bit 1.
Host-managed callbacks retain their original window/focus/menu behavior.
Relative file service paths resolve to the user workspace, not the module
or asset directory. Absolute POSIX paths are supported.

## Exact graphics colors (ABI 1.9)

`fill_rect_rgb(x, y, w, h, rgb)` draws clipped opaque rectangles with exact
`0x00RRGGBB` colors. Use it for palette-based image runs or arbitrary RGB
artwork; it never changes the system palette. Existing indexed drawing and
system color services remain available. The new callback is appended, so
modules built for earlier ABI 1.x versions retain their field offsets. A
module using this service must require `host->abi_minor >= 9`.

## Shared desktop UI (ABI 1.10)

Include `sdk/ui.h` for shared menus and scrollbars. `BudoScrollbar` stores
geometry, range, page size, position, and drag state. Configure it before
`budo_scroll_draw`, and feed pointer down/move/up events through
`budo_scroll_pointer`. Copy its clamped position back to the application
viewport. Controls use semantic colors and host drawing services.
`budo_menu_draw` and `budo_menu_hit` share 16-pixel menu rows and hover
feedback with the host-backed applications. `get_pointer_state` provides
current logical pointer coordinates for rendering hover states.

## Shared item selection (ABI 1.11)

`sdk/selection.h` implements the selection model used by Desktop, File
Explorer and the single-file picker. Each view owns a `BudoSelection`,
selection bytes and an optional drag snapshot. IDs index this storage;
pass visible IDs in display order so ranges and Ctrl+A exclude filtered
items. A null order means consecutive IDs starting at zero. The model
supports arbitrary capacities, including dynamic lists larger than 256.
Clear selection after changing item IDs to avoid stale focus or anchors.

```c
static unsigned char selected[100], snapshot[100];
static BudoSelection selection;
/* Once, when creating the view: */
budo_selection_init(&selection, selected, snapshot, 100);
/* On pointer down, after mapping the pointer to an item ID: */
budo_selection_click(&selection, visible_ids, visible_count, hit_id,
                      budo_selection_modifiers(host), buttons == 2);
```

Plain click selects one item, Ctrl-click toggles, Shift-click selects a
range and Ctrl+Shift-click adds a range. Context clicks retain a selected
group or select an unselected item alone. Use `budo_selection_all`,
`budo_selection_space` and `budo_selection_move` for Ctrl+A, Space and DOS
navigation scan codes. Supply the view's column count and page size.
Ctrl-navigation moves focus, Shift extends from the anchor, and
Ctrl+Shift extends without clearing previous selection.

For rectangles, call `budo_selection_drag_begin` on empty-space pointer
down, then `budo_selection_drag_update` with visible item rectangles on
move/up. Clip pointer coordinates to the view. Set `dragging = 0` after
the final update. Updates restore the initial snapshot before applying
the rectangle, so shrinking it removes earlier intersections. Shift
adds, and Ctrl toggles against the original snapshot.

`sdk/ui.h` supplies `budo_selection_icon_draw` and
`budo_selection_drag_draw`. Paint the icon background before artwork and
its focus outline afterwards. Desktop and Explorer pass 32x32 icon bounds;
labels and grid gutters are outside these bounds. List views can use the
same model with their own row renderer and single-file contexts pass zero
modifiers to keep one selected item.

`BwaHostApi.get_keyboard_modifiers()` is appended in ABI 1.11. It returns
Shift (`0x03`) and Ctrl (`0x04`) using the host's current keyboard state.
`budo_selection_modifiers(host)` checks the ABI version and returns zero
on earlier hosts. Applications requiring modified selection should check
`host->abi_minor >= 11` at entry. Existing ABI 1.x field offsets remain
unchanged. Opening, dragging items and deleting are application actions;
selection itself does not mutate files or invoke application callbacks.

## Shared file dialogs

`file_dialog(BUDO_FILE_OPEN | BUDO_FILE_SAVE_AS, initial_path)` opens the
host-owned picker. Set `BwaAppDefinition.file_selected(mode, path)` to
handle the result. Return nonzero after a successful load/save; return
zero to retain the picker with an error. The host confirms Save As
replacement before dispatching. Callbacks run with their requesting
application as the host API context. A result callback may start another
file dialog; the host preserves it. The picker provides directory
navigation, folder creation, supported-type filters, full-path editing,
keyboard navigation and a draggable scrollbar. Cancelling sends a cancel
response to `confirm_result` so an app can clear pending transitions.

`confirm_dialog(kind, title, message)` opens a shared Save/Discard/Cancel
or Replace/Cancel prompt. Set `confirm_result(kind, response)` and handle
`BUDO_RESPONSE_SAVE`, `BUDO_RESPONSE_DISCARD`, and `BUDO_RESPONSE_CANCEL`.
For overwrite prompts, SAVE means Replace. These dialogs are asynchronous;
do not perform a destructive transition until its response arrives.

Set `request_close()` to protect unsaved work. Return nonzero to permit
close, or zero after requesting confirmation. Once Save or Discard is
resolved, call `window_close()` with the document clean. Title buttons,
File > Close, Escape and desktop exit consult this hook. The existing
`callbacks.close` notification runs only after close is permitted.

The host appends these services to `BwaHostApi` and appends the new result
hooks after `BwaAppDefinition.callbacks`, preserving older field offsets.
Modules using them must require `host->abi_minor >= 10`. Older modules
remain loadable, but must adopt these services to use the shared dialogs.

Mouse wheel events arrive as extended scan codes 201 (up) and 202 (down),
with the usual `0x100` prefix for native modules. Page Up/Down remain 73/81,
so applications can scroll a viewport without moving its text caret.
Binary writes use temporary files, checked flush/close and atomic rename;
a failed write does not truncate the destination.

## Application styling contract and future UI Style settings

New bundled BWAs must use host-managed `window_create` windows and
`sdk/ui.h` controls. The host draws the frame, title bar, border/bevel and
minimize/maximize/close glyphs, and handles movement, resizing and focus.
Use `budo_menu_bar_item`, `budo_menu_items_draw` (disabled/checkable items),
`budo_menu_draw`, and shared scrollbars for application menus. Use host
buttons, sunken panels, file dialogs and confirmations instead of private
copies. Query semantic colors/metrics at draw time; do not cache RGB theme
values or use raw palette indices for UI. Arbitrary artwork RGB is separate.
This is a supported component toolkit and authoring contract, not an
automatic restyling mechanism for third-party apps drawing their own chrome.

The host owns `BudoUiStyle` in `src/ui_style.h`; its default is the classic
style. `desktop_apply_ui_style` validates a complete style before changing
UI palette slots. Existing windows and SDK controls resolve the same slots
and therefore adopt a changed palette together without reopening apps.
The artwork palette and exact PCX RGB plane remain untouched.

A future Settings UI Style page should call host services to apply/persist
this state and select wallpaper. Add those services by appending to the ABI;
do not duplicate theme state in Settings or individual BWAs. Wallpaper is
already host-owned (`desktop_background` and its RGB plane), independent
of UI colors. A wallpaper replacement should load and validate a candidate
before replacing the current image and persist its path with the theme.
Settings pages should reuse the existing shared menu, panels and scrollbars.
The page navigation, theme persistence and wallpaper-selection UI are future
work; no UI Style settings page is exposed by this change.

## Interaction feedback (ABI 1.12)

The host appends `draw_button_state`, `pointer_region` and `get_time_ms`.
Earlier ABI 1.x offsets are unchanged. Check `abi_minor >= 12` before using
these fields. `sdk/ui.h` supplies `budo_button_draw` and
`budo_scroll_pointer_host` with fallbacks for older hosts.

`draw_button_state(x, y, w, h, label, flags)` supports
`BUDO_BUTTON_PRESSED`, `BUDO_BUTTON_DISABLED`, `BUDO_BUTTON_FOCUSED`,
`BUDO_BUTTON_DEFAULT` and `BUDO_BUTTON_IMMEDIATE`. Focus/default flags are
rendering requests; they do not add keyboard navigation. The original
`draw_standard_button` remains supported and receives hover/capture feedback.
Nonempty command labels register a release-to-activate target. Empty labels
remain decorative panels. Disabled registered commands do not dispatch clicks.

The host captures command presses and dispatches the existing `mouse_down`
handler on a valid release. A following `mouse_up` can occur in the same frame.
Use `BUDO_BUTTON_IMMEDIATE` for controls that require physical press events,
such as scroll arrows. Canvas drawing and unregistered areas retain physical
mouse events. While held, native apps receive `mouse_move` every frame to
support timed repetition even when the pointer is stationary.

`pointer_region(x, y, w, h, cursor, tooltip)` registers a contextual area during
an application's draw callback. Regions are rebuilt each frame; the last
visible covering region takes precedence. Available cursors are
`BUDO_CURSOR_ARROW`, `BUDO_CURSOR_TEXT`, `BUDO_CURSOR_RESIZE`,
`BUDO_CURSOR_CROSSHAIR` and `BUDO_CURSOR_BUSY`. A null/empty tooltip suppresses
tips. OR the cursor with `BUDO_CURSOR_OVERLAY` for a popup extending beyond
the active window; inactive app regions retain ordinary window ownership.
Regions are non-command areas: do not overlay a command button with a
pointer region to customize its tooltip, because the region blocks command
capture. Menu popup regions deliberately use this behavior.

`get_time_ms()` returns monotonic milliseconds. Use
`budo_scroll_pointer_host(host, bar, x, y, event)` for production input and
`budo_scroll_pointer_at(bar, x, y, event, now_ms)` for deterministic timing tests.
Reconfigure geometry/content without clearing the scrollbar's held/drag state;
clear that state when reopening an application after closing during a gesture.

## Keyboard and editable associations (ABI 1.13)

Appended host services preserve the offsets of all earlier services:

- `get_keyboard_layout()` returns `0` for ENG or `1` for Finnish/Swedish NORD.
- `set_keyboard_layout(int nordic)` persists a layout; zero indicates failure
  and preserves the previous layout. NORD is the default without saved state.
- `delete_file_association(const char *extension)` removes and persists a row;
  zero preserves the previous table on failure.

`set_file_association()` now also creates missing extensions. Extensions must
start with a dot and contain 1–7 alphanumeric characters; application IDs may
contain alphanumeric characters, underscores and hyphens. IDs can reference an
application that is temporarily unavailable. An empty ID leaves a row unassigned.
The saved table is authoritative, so removed default associations stay removed.
Keyboard events retain the one-byte CP850 Nordic repertoire; host text drawing
also understands its UTF-8 equivalents in POSIX filenames.

## Documents and UI palette roles (ABI 1.14)

Services append to `BwaHostApi` without changing earlier offsets. Modules that
use them must require `host->abi_minor >= 14`.

| Service | Result |
| --- | --- |
| `open_reader_file(path)` | Opens a new read-only host document instance; returns zero on failure. |
| `set_system_color(role, palette_index)` | Saves a role assignment (index 0–255), then applies it globally; zero preserves the previous assignment. |
| `reset_system_colors()` | Saves and applies default role assignments; zero preserves the current theme. |

`get_system_color(role)` returns the assigned global palette index. The fixed
256-entry palette in `sdk/palette.h` is shared with Paint. Theme changes alter
role assignments, never palette RGB values or independent PCX/RGB artwork.
`BUDO_SYS_COLOR_COUNT` is 25; appended roles cover document surfaces, frames,
hover/pressed controls, desktop labels, terminal defaults, fallback file/folder
icons, cursor ink, status bars and selection. `ui-colors.state` stores one byte
per role. Existing `terminal-colors.state` defaults migrate when no UI state exists.

`launch_host_app()` also accepts `BWA_HOST_APP_READER`, `BWA_HOST_APP_HELP`
and `BWA_HOST_APP_RECYCLE_BIN`. Reader uses the Editor document formats and
capacity limits, with all editing and save commands blocked. Help opens
`documents/help.txt` from the repository through Reader. Reader and Recycle Bin
share the host Editor/Explorer instance pools while retaining independent state.

Recycle storage is user-owned under `.budowin/RecycleBin`. Each `entry-*`
wrapper contains a NUL-terminated absolute original path in `origin` and the
original file, directory or symlink at `payload`. Restore rejects existing
paths; recursive copy/delete never follows symlinks. Cross-filesystem moves
copy the complete payload before removing the source. Empty deletes wrappers
permanently after confirmation. Explorer deletion uses this store; desktop
shortcuts continue to preserve their target files.

## PC speaker effects (ABI 1.15)

`play_tones(const BwaTone *notes, unsigned int count)` queues a monophonic
square-wave sequence in the owning terminal tab without waiting for playback.
It returns nonzero when queued, zero when audio is unavailable or invalid.
A new sequence replaces the previous one; `play_tones(NULL, 0)` stops it.
Up to eight notes are accepted, with frequencies 0–4000 Hz, durations
1–1000 ms each and at most 2000 ms total. Frequency zero is a silent rest.
The terminal synthesizes PCM at its audio device's rate and channel count,
using a dedicated effect channel per tab. No WAV assets or separate audio
device are needed. Disconnecting a graphics client stops its tab's effect.

This service is appended to the host ABI. Check `abi_minor >= 15` and the
callback before accessing it; Chess remains playable on ABI 1.14 hosts.
Graphics protocol version 1 keeps its 56-byte packets: ACK format bit 0
advertises tone support, request type 8 carries a note count in `width` and
up to eight packed words from `height` (low 16 bits Hz, high 16 bits ms).
Reply type 9 returns an errno value in `format`, or zero on success.
Clients reject unsupported hosts locally; unavailable audio does not close
an otherwise valid graphics connection.
