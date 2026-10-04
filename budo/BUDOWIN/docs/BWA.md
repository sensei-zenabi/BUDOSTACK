# Native BWA modules

BUDOSTACK BUDOWIN extends upstream BWA ABI to 1.9 (`src/bwa.h`). Modules use
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
