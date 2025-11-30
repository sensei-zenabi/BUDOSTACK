# Library usage notes

- Update this file whenever a library in `lib/` is added, removed, or when consumer files change their includes.
- Remove entries for deleted libraries and add new consumers as they appear to keep this list accurate for future maintenance.

## Recent verification notes

- Confirmed no code paths reference the former `libdraw.c` helpers; rendering for `display` and `paint` relies on `libimage.h` and bespoke drawing logic instead.

## Include and usage map

| Library file | Files including or using it |
| --- | --- |
| `lib/lib_csv_print.c` | `utilities/csvprint.c`
| `lib/libconsole.c` | `main.c`
| `lib/libedit.c` | `apps/edit.c`
| `lib/libimage.h` | `commands/_IMAGE.c`, `commands/_DISPLAY.c`, `utilities/display.c`, `lib/libimage.c`
| `lib/libtable.c` | `apps/table.c`
| `lib/retroprofile.h` | `commands/_TEXT.c`, `commands/_RETROPROFILE.c`, `commands/_COLORS.c`, `commands/_RECT.c`, `commands/_BAR.c`, `lib/retroprofile.c`
| `lib/termbg.h` | `commands/_EXE.c`, `commands/_TEXT.c`, `commands/_DISPLAY.c`, `commands/_RECT.c`, `commands/_BAR.c`, `commands/_IMAGE.c`, `apps/cls.c`, `lib/libimage.c`, `lib/termbg.c`
| `lib/terminal_layout.h` | `apps/exchange.c`, `apps/edit.c`, `apps/spectrum.c`, `apps/inet.c`, `apps/table.c`, `apps/paint.c`, `lib/terminal_layout.c`
| `lib/dr_mp3.h` | `apps/terminal.c`
| `lib/stb_image.h` | `commands/_TERM_SPRITE.c`, `apps/paint.c`, `lib/libimage.c`
| `lib/stb_image_write.h` | `apps/paint.c`
| `lib/stb_truetype.h` | `utilities/ttftopsf.c`
| `lib/stb_vorbis.h` | `apps/terminal.c`
