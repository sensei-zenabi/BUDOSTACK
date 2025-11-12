#ifndef TERM_BG_H
#define TERM_BG_H

int termbg_get(int x, int y, int *color_out);
void termbg_set(int x, int y, int color);
int termbg_save(void);
void termbg_shutdown(void);
void termbg_clear(void);
int termbg_encode_truecolor(int r, int g, int b);
int termbg_is_truecolor(int color);
void termbg_decode_truecolor(int color, int *r_out, int *g_out, int *b_out);

#endif /* TERM_BG_H */
