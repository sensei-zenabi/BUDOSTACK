# Application instance architecture

Launching a desktop icon creates a new application instance, even when another
instance is visible or minimized. Icons belong to the program catalog and stay
available. Alt+Tab, minimized buttons, window frames, input regions and modal
owners identify individual runtime windows. Restoring or focusing a window never
runs its launch callback again.

## Ownership

| Lifetime | Owned data |
| --- | --- |
| Desktop | Installed program catalog, icons, shortcuts, file associations, clipboard, active runtime ID, stacking order, modal picker/confirmation UI |
| Explorer instance | Window, directory listing/path/page, selection, expanded folders, scrollbars, view and rename state |
| Editor instance | Window, document/path, caret, selection, undo/redo allocations, pending save/close action, search and view state |
| Terminal instance | Window/title, scrollback/history, PTY descriptor/process group, ANSI cells/parser, graphics host/decoder, selection and colors |
| Native BWA instance | Private module data segment, callback definition, managed window and application allocations |

`builtin_state.h` contains the host application contexts. Existing private helper
names are aliases into the currently selected context, avoiding copies of large
document and directory buffers during dispatch. `builtin_select` selects a
runtime window; it does not allocate, open or reset it. Runtime IDs 1–72 encode
built-in kind and slot, whereas native instance IDs start after the installed
module catalog range. Neither IDs nor context aliases are public SDK ABI.

The main loop keeps Explorer's page in its context, selects the input owner
before dispatch, and polls every open Terminal, including minimized/background
ones. Rendering temporarily selects each window's context and restores the
input contexts afterwards. Drawing and hit testing share the desktop stacking
order. Closing/minimizing the active window selects the highest visible sibling.
The app switcher displays a bounded slice around its selection and cycles through
all running windows.

## Native module compatibility

Loading a shared library twice with `dlopen` ordinarily reuses its writable
statics. Each launch therefore copies the module into a uniquely named, private
image in the user's BUDOWIN state directory and opens that image with
`RTLD_NOW | RTLD_LOCAL`. The temporary file is unlinked immediately after loading.
The module keeps its existing callback and host API signatures; every host call
resolves through `bwa_callback_app` to that callback's instance.

The original loaded module remains catalog metadata and supplies its launcher
icon. Launcher modules delegate to the host instance factory. `BWA_FLAG_SINGLETON`
is retained as a legacy ABI value and no longer suppresses a new launch.
Applications should release owned allocations in `callbacks.close`; Paint now
releases its image, undo, file and fill buffers there. Closed native slots unload
before reuse, and every new launch reruns `bwa_entry` against a fresh image.

This isolates application-module statics within the desktop process, not crashes
or globals in shared dependency libraries. Native modules remain trusted plugins.
Modules needing additional dynamic dependencies must use normal loader search
paths; `$ORIGIN` resolves beside the private image, not the installed BWA. A
future context-bearing ABI could replace private images for cooperating modules,
but is not needed to run existing callback-based modules independently.

## Executable programs

Explorer and executable shortcuts launch a dedicated Terminal instance with the
program's own PTY, process group, working directory and graphics endpoint. The
child executes the selected path directly, with no shell interpolation. Its
text/graphics/input remain inside that window; BUDOWIN keeps running and can
launch another copy. Program termination closes its host window. Closing a
running Terminal stops only that process group and frees its graphics resources.
PTY master descriptors have close-on-exec set so additional children do not
retain other windows' PTYs. Desktop launch no longer writes a single-window
handoff snapshot or exits/restarts the desktop. Legacy snapshot readers remain
for compatibility with earlier sessions.

## Resource boundaries

Contexts are allocated on demand and closed slots are reused. The current bounds
are 24 windows per built-in kind (executable hosts share Terminal's bound), 16
installed BWA programs, and 64 simultaneous native windows. Limit/allocation/load
failures report an error and leave existing instances intact. Unsaved document
checks on desktop exit visit every Editor and native instance. The desktop picker
and confirmation UI remain modal and retain the requesting runtime ID.

`tests/budowin_instances.c` checks isolation, dirty-close ownership, slot reuse,
window focus, exit protection, multiple real PTYs and concurrent executable
hosts. `tests/budowin_launch.c` drives the actual desktop and routes nested graphics
endpoints through a test-only socketpair broker, verifying that executable
launches keep one desktop connection alive. Existing document, UI, filesystem,
selection, native module and graphics tests remain in `tests/run_budowin.sh`.
