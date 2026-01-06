# ./budo/ — BUDOSTACK Terminal Extension Reference

This document describes how to access the **SDL2-backed graphics, input, and
sound** capabilities provided by `apps/terminal` from C code. The interface is
implemented as **ANSI escape sequences** (OSC 777 + standard CSI mouse
tracking). Any program that writes to stdout and reads stdin can use it, but the
features only activate when the program is running **inside BUDOSTACK's
`terminal` app**. Other terminals will ignore these sequences or display them as
text.

## Quick start (C usage)

1. **Run your program from `apps/terminal`.**
2. **Write OSC 777 sequences to stdout** to invoke graphics/sound features.
3. **Read stdin** for keyboard input or mouse tracking events.

Example: send a single red pixel and play a sound.

```c
#include <stdio.h>

static void term_osc(const char *payload) {
    /* OSC = ESC ] ... BEL */
    printf("\x1b]777;%s\x07", payload);
    fflush(stdout);
}

int main(void) {
    term_osc("pixel=draw;pixel_x=10;pixel_y=10;pixel_r=255;pixel_g=0;pixel_b=0");
    term_osc("pixel=render;pixel_layer=1");
    term_osc("sound=play;channel=1;volume=75;path=./sounds/click.wav");
    return 0;
}
```

> **OSC terminators:** Either BEL (`\x07`) or ST (`ESC \\`) ends an OSC
> sequence. `apps/terminal` accepts both.

---

## 1) Graphics API (OSC 777)

The terminal exposes a **pixel framebuffer** layered on top of the text grid.
Pixels are addressed in **pixel coordinates** with origin at the **top-left** of
the framebuffer. Layers are numbered **1–16**, with **16 drawn on top** of 1.

### 1.1 Pixel drawing

**Draw a single pixel** (layer 1 only):

```
ESC ] 777 ; pixel=draw ; pixel_x=<x> ; pixel_y=<y> ; pixel_r=<0-255> ; pixel_g=<0-255> ; pixel_b=<0-255> BEL
```

**Clear all custom pixels:**

```
ESC ] 777 ; pixel=clear BEL
```

**Apply pending clears / refresh a layer:**

```
ESC ] 777 ; pixel=render ; pixel_layer=<0|1-16> BEL
```

- `pixel_layer=0` applies any pending clears for all layers.
- `pixel_layer=1..16` applies pending clears for the specified layer.

> Notes:
> - `pixel=draw` currently draws into **layer 1**.
> - Use **sprites** (below) for multi-layer graphics or for bulk pixels.

### 1.2 Frame drawing (raw RGBA)

Frames are supplied as **base64-encoded raw RGBA** data with size
`width * height * 4` bytes. Use this for **full-screen redraws** or large
rectangles to reach DOS-era pixel throughput.

**Draw a frame region:**

```
ESC ] 777 ; frame=draw ; frame_x=<x> ; frame_y=<y> ; frame_w=<w> ; frame_h=<h> ; frame_data=<base64> BEL
```

> Notes:
> - Frame data is composited **after text** and before custom pixel layers.
> - For best performance, send full-frame updates (`frame_x=0`, `frame_y=0`)
>   sized to the current framebuffer resolution.

### 1.3 Sprite drawing (raw RGBA)

Sprites are supplied as **base64-encoded raw RGBA** data with size
`width * height * 4` bytes.

**Draw a sprite:**

```
ESC ] 777 ; sprite=draw ; sprite_x=<x> ; sprite_y=<y> ; sprite_w=<w> ; sprite_h=<h> ; sprite_layer=<1-16> ; sprite_data=<base64> BEL
```

**Clear a sprite rectangle:**

```
ESC ] 777 ; sprite=clear ; sprite_x=<x> ; sprite_y=<y> ; sprite_w=<w> ; sprite_h=<h> ; sprite_layer=<1-16> BEL
```

### 1.4 Text sprites (font rendered to pixels)

The terminal can render text to the pixel layers using the system PSF font
(`./fonts/system.psf`) and then composite it like a sprite.

**Draw text:**

```
ESC ] 777 ; text=draw ; text_x=<x> ; text_y=<y> ; text_layer=<1-16> ; text_color=<1-18> ; text_data=<base64(utf8)> BEL
```

`text_color` is a palette index:
- **1–16**: terminal palette entries
- **17**: default foreground
- **18**: default background

### 1.5 Resolution, scale, and margin

You can reconfigure the terminal's render layout at runtime:

**Scale text grid (integer):**
```
ESC ] 777 ; scale=<int> BEL
```

**Set margin (pixels):**
```
ESC ] 777 ; margin=<pixels> BEL
```

**Set explicit framebuffer resolution:**
```
ESC ] 777 ; resolution=<width>x<height> BEL
```

Or separately:
```
ESC ] 777 ; resolution_width=<width> ; resolution_height=<height> BEL
```

---

## 2) Input API

### 2.1 Keyboard input

Keyboard input is standard terminal input. If you need raw keystrokes, enable
raw mode with `termios` in your program. The terminal forwards keypresses
directly to your stdin.

### 2.2 Mouse input (SGR mouse tracking)

`apps/terminal` supports **SGR mouse mode**. Enable it with standard CSI
sequences:

```text
ESC [ ? 1000 h   (mouse click tracking)
ESC [ ? 1002 h   (mouse drag tracking)
ESC [ ? 1003 h   (mouse motion tracking)
ESC [ ? 1006 h   (SGR mouse mode)
```

You will then receive SGR mouse sequences on stdin:

```
ESC [ < b ; x ; y M   (press or drag)
ESC [ < b ; x ; y m   (release)
```

Disable with `... l` (lowercase L):

```text
ESC [ ? 1000 l
ESC [ ? 1002 l
ESC [ ? 1003 l
ESC [ ? 1006 l
```

### 2.3 Mouse query (OSC 777)

You can also request a **snapshot** of the mouse position and click counts:

```
ESC ] 777 ; mouse=query BEL
```

The terminal responds on stdin with a line:

```
_TERM_MOUSE <x> <y> <left_clicks> <right_clicks>
```

- Coordinates are pixel-based (same space as the framebuffer).
- Click counters reset after each query.

---

## 3) Sound API (OSC 777)

The terminal can play audio files using SDL2. Supported formats:
- **WAV** (`.wav`)
- **MP3** (`.mp3`)
- **OGG Vorbis** (`.ogg`)

There are **32 channels** (1–32). Each play replaces audio already playing on
that channel.

### 3.1 Play a sound

```
ESC ] 777 ; sound=play ; channel=<1-32> ; volume=<0-100> ; path=<file> BEL
```

- `volume` is a percentage (0–100). If omitted, volume defaults to 100.
- `path` is a filesystem path visible to the terminal process.

### 3.2 Stop a sound

```
ESC ] 777 ; sound=stop ; channel=<1-32> BEL
```

---

## 4) Implementation notes for C developers

- **Write to stdout, read from stdin.** All features are escape sequences.
- **Flush stdout** after emitting control sequences (`fflush(stdout)`).
- **Base64 encoding** is required for sprite and text payloads.
- **Layering:** layers 1–16 are rendered in descending order (16 on top).
- **Fallback:** when run in a non-BUDOSTACK terminal, OSC 777 will be ignored.

If you need a starting point, search for `terminal_handle_osc_777` in
`apps/terminal.c` to see the exact parsing logic.
