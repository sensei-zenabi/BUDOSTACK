#ifndef BUDOWIN_PALETTE_H
#define BUDOWIN_PALETTE_H

/* Fixed global 256-color palette shared by Paint and UI theme selection. */
static inline unsigned int budo_palette_rgb(int index)
{
    static const unsigned int base[16] = {
        0x008080, 0x000000, 0x7f7f7f, 0xaaaaaa,
        0xffffff, 0xc2c2c2, 0x0000a0, 0x0000ff,
        0xffff00, 0xa00000, 0x00a000, 0x00a0a0,
        0xa000a0, 0xa05000, 0xc8c8c8, 0xffffff
    };
    if (index < 0 || index > 255) return 0;
    if (index < 16) return base[index];
    if (index < 232) {
        int c = index - 16;
        return ((unsigned int)(c / 36 * 51) << 16) |
               ((unsigned int)(c / 6 % 6 * 51) << 8) | (unsigned int)(c % 6 * 51);
    }
    unsigned int gray = (unsigned int)((index - 232) * 255 / 23);
    return (gray << 16) | (gray << 8) | gray;
}
#endif
