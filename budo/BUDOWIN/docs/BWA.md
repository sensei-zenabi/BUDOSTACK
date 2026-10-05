# Native BWA modules

BUDOSTACK BUDOWIN extends upstream BWA ABI to 1.10 (`src/bwa.h`). Modules use
`BwaHostApi` for drawing, managed windows, binary file access and allocation.
The built-in Explorer, Editor and Terminal launcher modules delegate to
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
