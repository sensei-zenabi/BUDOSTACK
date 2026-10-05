#ifndef BUDOWIN_UI_STYLE_H
#define BUDOWIN_UI_STYLE_H

/* Host-owned style state. UI slots use VGA DAC channels (0..63).
 * Artwork stays in the independent RGB plane; never replace its palette. */
typedef struct BudoUiStyle {
    unsigned char colors[16][3];
} BudoUiStyle;

#define BUDO_UI_STYLE_CLASSIC { { \
    {0,32,32}, {0,0,0}, {18,18,18}, {42,42,42}, \
    {63,63,63}, {48,48,48}, {0,0,40}, {0,0,63}, \
    {63,63,0}, {40,0,0}, {0,40,0}, {0,40,40}, \
    {40,0,40}, {40,20,0}, {40,40,40}, {63,63,63} } }

#endif
