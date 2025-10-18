#ifndef TERM_BG_H
#define TERM_BG_H

int termbg_get(int x, int y, int *color_out);
void termbg_set(int x, int y, int color);
int termbg_save(void);
void termbg_shutdown(void);
void termbg_clear(void);

#endif /* TERM_BG_H */
