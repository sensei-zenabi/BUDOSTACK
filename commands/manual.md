# BUDOSTACK TASK Commands Reference

This reference manual lists the TASK scripting commands implemented in `commands/` and describes
how to call each command inside TASK scripts.

## General notes

- Commands prefixed with `_TERM` are intended for TASK scripts running inside the
  `apps/terminal` emulator. They emit OSC `777` sequences for pixel, sprite, sound,
  and input features.
- Commands without `_TERM` can be used in any TASK script. Many of them write to
  standard output so they can be captured with `RUN <command> TO $VAR`.
- Column/row coordinates are zero-based unless stated otherwise.
- CSV helpers expect semicolon (`;`) separated values with 1-based row/column indices.

## Command reference

### `_BAR`
**Usage:** `_BAR [-x <col> -y <row>] -title <text> -progress <0-100> [-color <0-18>]`

**Description:** Draws a progress bar with a title and percentage. When `-x`/`-y` are
provided, the bar is drawn at the specified column/row; otherwise it draws at the
current cursor position. `-color` selects the foreground color from the active
retro profile (0–15 = palette, 16 = default foreground, 17 = default background,
18 = cursor highlight).

### `_BEEP`
**Usage:** `_BEEP -<note> -<duration_ms>`

**Description:** Plays a tone using the terminal bell or ALSA (if available). Notes
use a letter with optional sharp/flat and octave (e.g., `A4`, `C#5`, `Db3`). Duration
is in milliseconds (e.g., `-250`).

### `_CALC`
**Usage:** `_CALC <expression>`

**Description:** Evaluates a mathematical expression and prints the numeric result.

- Operators: `+`, `-`, `*`, `x`, `/`, `^`, parentheses, unary `+`/`-`.
- Unary functions: `abs`, `acos`, `acosh`, `asin`, `asinh`, `atan`, `atanh`, `cbrt`,
  `ceil(x[, digits])`, `cos`, `cosh`, `erf`, `erfc`, `exp`, `exp2`, `expm1`, `fabs`,
  `floor(x[, digits])`, `gamma`, `ln(x[, base])`, `lgamma`, `log(x[, base])`, `log10`,
  `log1p`, `log2`, `tgamma`, `round(x[, digits])`, `sin`, `sinh`, `sqrt`, `tan`,
  `tanh`, `trunc(x[, digits])`.
- Binary functions: `atan2(x, y)`, `copysign(x, y)`, `fdim(x, y)`, `fmax(x, y[, ...])`,
  `fmin(x, y[, ...])`, `fmod(x, y)`, `hypot(x, y[, ...])`, `pow(x, y)`,
  `remainder(x, y)`.
- Binary helpers: `ldexp(x, exp)`, `scalbn(x, exp)`, `scalbln(x, exp)`.
- Ternary functions: `fma(x, y, z)`.
- Constants: `e`, `inf`, `infinity`, `nan`, `pi`, `tau`.

Notes:
- `round`, `ceil`, `floor`, `trunc` accept an optional second argument for precision.
- `log`/`ln` accept an optional base argument.
- `fmin`, `fmax`, `hypot` accept two or more arguments.

### `_CLEAR`
**Usage:** `_CLEAR`

**Description:** Clears the terminal using ANSI escape sequences.

### `_CONFIG`
**Usage:**
```
_CONFIG -read <key>
_CONFIG -write <key> <value>
```

**Description:** Reads or updates keys in the root `config.ini` file (resolved via
`$BUDOSTACK_BASE` when set, otherwise relative to the executable). `-read` prints
the current value for `<key>` and exits with a non-zero status if the key does not
exist. `-write` updates the value for `<key>` and creates it if missing.

### `_CSVFILTER`
**Usage:**
```
_CSVFILTER -file <path> -column <n> [-numeric]
          [-op <eq|ne|lt|le|gt|ge> -value <value>]...
          [-logic <and|or>]
          [-skipheader] [-keepheader] [-output <path>]
```

**Description:** Filters rows in a semicolon-separated CSV file based on one or more
comparisons. Column indices are 1-based. Use multiple `-op`/`-value` pairs to build
AND (default) or OR logic via `-logic`. When `-numeric` is set, values are treated
as numbers. `-skipheader` skips the first row from comparison; `-keepheader` prints
the header before results. `-output` writes to a file instead of stdout.

### `_CSVSTATS`
**Usage:** `_CSVSTATS -file <path> -column <n> -stat <type> [-skipheader] [-rowstart <n>] [-rowend <n>]`

**Description:** Computes statistics on a 1-based column in a semicolon-separated CSV.
Valid stats: `count`, `sum`, `mean`, `min`, `max`, `variance`, `stddev`. `-rowstart`
and `-rowend` limit the 1-based row range after header skipping.

### `_DICE`
**Usage:** `_DICE <dice>`

**Description:** Rolls standard Dungeons & Dragons dice and prints the total.
Use `<count>d<sides>` notation, e.g. `2d20`. Supported sides: `d4`, `d6`, `d8`, `d10`,
`d12`, `d20`, `d100`. Maximum count is 100.

### `_DISPLAY`
**Usage:** `_DISPLAY -x <col> -y <row> -file <path>`

**Description:** Renders an image file (PNG/BMP) at the specified column/row using
`libimage` and preserves background colors.

### `_EXE`
**Usage:** `_EXE -x <col> -y <row> [--] <command> [args...]`

**Description:** Launches an executable and renders its output at the given offset
while preserving existing background colors. Commands are resolved relative to
`$BUDOSTACK_BASE` when possible, searching `apps/`, `commands/`, and `utilities/` if
no path separator is provided. Use `--` to separate `_EXE` flags from the command.

### `_FROMCSV`
**Usage:** `_FROMCSV -file <path> -column <n> -row <n>`

**Description:** Reads a single cell from a semicolon-separated CSV (1-based row and
column) and prints it.

### `_GETHEIGHT`
**Usage:** `_GETHEIGHT`

**Description:** Prints the terminal height in rows.

### `_GETROW`
**Usage:** `_GETROW`

**Description:** Queries the current cursor position and prints the row number
(1-based, as reported by the terminal).

### `_GETWIDTH`
**Usage:** `_GETWIDTH`

**Description:** Prints the terminal width in columns.

### `_HELLO`
**Usage:** `_HELLO`

**Description:** Prints `hello world!`.

### `_IMAGE`
**Usage:** `_IMAGE -x <col> -y <row> -file <path>`

**Description:** Renders an image file (PNG/BMP) at the specified column/row using
`libimage` and preserves background colors.

### `_KEYS`
**Usage:** `_KEYS`

**Description:** Waits for a key press and prints a numeric code:

- `2` = up, `-2` = down, `1` = right, `-1` = left
- `3` = enter, `4` = space, `5` = tab, `6` = backspace/delete
- `10` = escape (also used for Ctrl+C)

### `_RECT`
**Usage:** `_RECT -x <col> -y <row> -width <pixels> -height <pixels> [-color <0-18>] [-fill on|off]`

**Description:** Draws a rectangle using background colors at the specified position.
`-fill on` draws a filled rectangle; `-fill off` draws only the border. Color indices
0–15 use the active retro palette; 16 is the default foreground, 17 is the default
background, and 18 is the cursor highlight.

### `_RETROPROFILE`
**Usage:** `_RETROPROFILE <command> [profile]`

**Description:** Manages retro color profiles.

- `list` — list available profiles.
- `show <profile>` — show palette values and a color swatch.
- `apply <profile>` — emit OSC 4/10/11/12 escape sequences to set palette/defaults.
- `reset` — request reset with OSC 104/110/111/112.

### `_TERM_CLEAN`
**Usage:** `_TERM_CLEAN -x <pixels> -y <pixels> -width <pixels> -height <pixels> [-layer <1-16>]`

**Description:** Clears a rectangular pixel region on the terminal’s pixel surface.
Layer 1 is topmost; default layer is 1.

### `_TERM_KEYBOARD`
**Usage:** `_TERM_KEYBOARD`

**Description:** Captures up to 20 key presses since the last call and returns a TASK
array literal (e.g., `{A, LEFT_ARROW}`). If no keys were pressed, returns `{}`. The
most recent events are kept if more than 20 are received.

Event names:
- Letters: `A`–`Z`
- Digits: `0`–`9`
- `ENTER`, `SPACE`, `TAB`, `BACKSPACE`, `ESC`, `CTRL_C`
- Arrow keys: `UP_ARROW`, `DOWN_ARROW`, `LEFT_ARROW`, `RIGHT_ARROW`
- Function keys: `F1`–`F12`
- Navigation: `HOME`, `END`, `PAGE_UP`, `PAGE_DOWN`, `INSERT`, `DELETE`

### `_TERM_MARGIN`
**Usage:** `_TERM_MARGIN <pixels>`

**Description:** Sets the terminal render margin in pixels.

### `_TERM_MOUSE`
**Usage:** `_TERM_MOUSE`

**Description:** Queries mouse position and button counts from the terminal emulator
and returns a TASK array literal: `{X, Y, LEFT, RIGHT}`. `X`/`Y` are pixel positions
from the top-left corner; `LEFT`/`RIGHT` are button press counts since the last call.

### `_TERM_MOUSE_SHOW`
**Usage:** `_TERM_MOUSE_SHOW <enable|disable>`

**Description:** Shows (`enable`) or hides (`disable`) the mouse cursor in the
terminal emulator.

### `_TERM_RENDER`
**Usage:** `_TERM_RENDER [--render] [-layer <1-16>]`

**Description:** Triggers rendering of the terminal pixel buffer. Use `-layer` to
render only a single layer (1–16). Omitting `-layer` renders all layers.

### `_TERM_RESOLUTION`
**Usage:** `_TERM_RESOLUTION <width> <height>`

**Description:** Sets the terminal pixel resolution. Use `0 0` to restore defaults.

**Presets:** `_TERM_RESOLUTION LOW` (640×360) or `_TERM_RESOLUTION HIGH` (800×450).

### `_TERM_SCALE`
**Usage:** `_TERM_SCALE <scale>`

**Description:** Sets pixel scaling: `1` for original resolution, `2` for double.

### `_TERM_SHADER`
**Usage:** `_TERM_SHADER <enable|disable>`

**Description:** Enables or disables terminal shader passes.

### `_TERM_SOUND_PLAY`
**Usage:** `_TERM_SOUND_PLAY <channel> <audiofile> <volume>`

**Description:** Plays a sound on the given channel (1–32). `audiofile` must be a
readable path and is resolved to an absolute path. `volume` is 0–100.

### `_TERM_SOUND_STOP`
**Usage:** `_TERM_SOUND_STOP <channel>`

**Description:** Stops playback on the given channel (1–32).

### `_TERM_SPRITE`
**Usage:**
```
_TERM_SPRITE -x <pixels> -y <pixels>
            (-file <path> | -sprite {w,h,"data"} | -data <base64> -width <px> -height <px>)
            [-layer <1-16>]
```

**Description:** Draws a sprite onto the terminal’s pixel surface. Provide a PNG/BMP
via `-file`, a literal from `_TERM_SPRITE_LOAD` via `-sprite`, or raw base64 RGBA data
via `-data` along with `-width`/`-height`. Layer 1 is topmost; default layer is 1.

### `_TERM_SPRITE_LOAD`
**Usage:** `_TERM_SPRITE_LOAD -file <path>`

**Description:** Loads a PNG/BMP sprite and prints a TASK array literal
`{width,height,"<base64 RGBA data>"}`. Capture the output and reuse it with
`_TERM_SPRITE -sprite ...` to avoid re-reading the file.

### `_TERM_TEXT`
**Usage:** `_TERM_TEXT -x <pixels> -y <pixels> -text <string> -color <1-18> [-layer <1-16>]`

**Description:** Renders UTF-8 text onto the terminal’s pixel surface using the
system font. Color indices come from the active 18-color scheme. Layer 1 is topmost.

### `_TEST`
**Usage:** `_TEST`

**Description:** Prints a diagnostic grid and current terminal size information.
Intended for validating terminal layout.

### `_TEXT`
**Usage:** `_TEXT -x <col> -y <row> -text <string> [-color <0-18>]`

**Description:** Prints text at the specified column/row while preserving background
colors. `-text` can include multiple tokens; use `+` tokens to suppress spaces
between words (e.g., `-text Hello + World` prints `HelloWorld`).

### `_TIMER`
**Usage:** `_TIMER [--start | --stop | --get | --reset]`

**Description:** Simple stopwatch persisted at `/tmp/budostack_timer.state`.
`--start` begins timing, `--stop` pauses, `--get` prints elapsed milliseconds, and
`--reset` clears the timer.

### `_TOCSV`
**Usage:** `_TOCSV -file <path> -column <n> -row <n> -value <text>`

**Description:** Writes a value into a semicolon-separated CSV cell (1-based row and
column). The file is created if it does not exist and expanded with empty cells as
needed.

### `_TOFILE`
**Usage:** `_TOFILE`

**Description:** Prints guidance for logging terminal output. The main shell handles
`_TOFILE -file <path> --start` and `_TOFILE --stop`.

### `_VERSION`
**Usage:** `_VERSION`

**Description:** Prints the current Git commit count for the repository.

### `_WAIT`
**Usage:** `_WAIT <milliseconds>`

**Description:** Sleeps for the specified number of milliseconds.
