#include <stdio.h>

typedef struct {
    const char *name;
    int r;
    int g;
    int b;
} NamedColor;

static const NamedColor base16[16] = {
    {"Black", 0, 0, 0},
    {"Maroon", 128, 0, 0},
    {"Green", 0, 128, 0},
    {"Olive", 128, 128, 0},
    {"Navy", 0, 0, 128},
    {"Purple", 128, 0, 128},
    {"Teal", 0, 128, 128},
    {"Silver", 192, 192, 192},
    {"Grey", 128, 128, 128},
    {"Red", 255, 0, 0},
    {"Lime", 0, 255, 0},
    {"Yellow", 255, 255, 0},
    {"Blue", 0, 0, 255},
    {"Magenta", 255, 0, 255},
    {"Cyan", 0, 255, 255},
    {"White", 255, 255, 255}
};

int main(void) {
    static const int cube_levels[6] = {0, 95, 135, 175, 215, 255};

    for (int i = 0; i < 16; ++i) {
        const NamedColor *c = &base16[i];
        printf("%d = %s (rgb(%d,%d,%d))\n", i, c->name, c->r, c->g, c->b);
    }

    for (int idx = 16; idx <= 231; ++idx) {
        int offset = idx - 16;
        int r_level = offset / 36;
        int g_level = (offset / 6) % 6;
        int b_level = offset % 6;
        int r = cube_levels[r_level];
        int g = cube_levels[g_level];
        int b = cube_levels[b_level];
        printf("%d = rgb(%d,%d,%d)\n", idx, r, g, b);
    }

    for (int idx = 232; idx <= 255; ++idx) {
        int value = 8 + (idx - 232) * 10;
        if (value > 255)
            value = 255;
        printf("%d = grayscale %d (rgb(%d,%d,%d))\n", idx, value, value, value, value);
    }

    return 0;
}
