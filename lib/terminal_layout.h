#ifndef BUDOSTACK_TERMINAL_LAYOUT_H
#define BUDOSTACK_TERMINAL_LAYOUT_H

#include <stdint.h>

// Shared terminal layout defaults so every application targets the same
// 80x60 (640x480 @ 8x8 font) character grid. They are defined as macros so
// projects embedding Budostack can override them at compile time if the
// display needs to be tweaked.
#ifndef BUDOSTACK_TARGET_COLS
#define BUDOSTACK_TARGET_COLS 80
#endif

#ifndef BUDOSTACK_TARGET_ROWS
#define BUDOSTACK_TARGET_ROWS 60
#endif

/* Clear only on first frame/resize, then rewind for a complete repaint. */
void budostack_terminal_begin_frame(void);

/* Draw a centered board with uniform integer-sized cells; never downsample. */
void budostack_draw_terminal_grid(const uint32_t *cells, int width, int height,
                                  const char *status, const char *controls);

int budostack_terminal_layout_enabled(void);
void budostack_apply_terminal_layout(void);
void budostack_clamp_terminal_size(int *rows, int *cols);
int budostack_get_target_cols(void);
int budostack_get_target_rows(void);

#endif // BUDOSTACK_TERMINAL_LAYOUT_H
