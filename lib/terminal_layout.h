#ifndef BUDOSTACK_TERMINAL_LAYOUT_H
#define BUDOSTACK_TERMINAL_LAYOUT_H

// Shared terminal layout constants so every application targets the same
// 80x45 (640x360 @ 8x8 font) character grid.  They are defined as macros so
// projects embedding Budostack can override them at compile time if the
// display needs to be tweaked.
#ifndef BUDOSTACK_TARGET_COLS
#define BUDOSTACK_TARGET_COLS 80
#endif

#ifndef BUDOSTACK_TARGET_ROWS
#define BUDOSTACK_TARGET_ROWS 45
#endif

void budostack_apply_terminal_layout(void);
void budostack_clamp_terminal_size(int *rows, int *cols);

#endif // BUDOSTACK_TERMINAL_LAYOUT_H
