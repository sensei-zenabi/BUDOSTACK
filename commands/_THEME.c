#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifdef __linux__
#include <linux/kd.h>
#endif

struct rgb {
    unsigned char r;
    unsigned char g;
    unsigned char b;
};

static const struct rgb c64_palette[16] = {
    {0x00, 0x00, 0x00}, /* 0 black */
    {0x68, 0x37, 0x2b}, /* 1 red */
    {0x58, 0x8d, 0x43}, /* 2 green */
    {0xb8, 0xc7, 0x6f}, /* 3 yellow */
    {0x35, 0x28, 0x79}, /* 4 blue */
    {0x6f, 0x3d, 0x86}, /* 5 purple */
    {0x70, 0xa4, 0xb2}, /* 6 cyan */
    {0xf0, 0xf0, 0xf0}, /* 7 white */
    {0x44, 0x44, 0x44}, /* 8 dark gray */
    {0x9a, 0x67, 0x59}, /* 9 light red */
    {0x9a, 0xd2, 0x84}, /* 10 light green */
    {0x6f, 0x4f, 0x25}, /* 11 orange */
    {0x6c, 0x5e, 0xb5}, /* 12 light blue */
    {0x43, 0x39, 0x00}, /* 13 brown */
    {0x6c, 0x6c, 0x6c}, /* 14 gray */
    {0x95, 0x95, 0x95}  /* 15 light gray */
};

static void emit_default_colors(void) {
    /*
     * Set Commodore 64-inspired defaults: dark blue background and light blue text.
     * Works for terminals that understand the common SGR sequences.
     */
    fputs("\033[0m\033[44m\033[1;34m", stdout);
}

static void apply_xterm_palette(FILE *stream) {
    for (int i = 0; i < 16; ++i) {
        fprintf(stream, "\033]4;%d;rgb:%02x/%02x/%02x\a",
                i, c64_palette[i].r, c64_palette[i].g, c64_palette[i].b);
    }

    /* Foreground, background, and cursor colors. */
    fprintf(stream, "\033]10;rgb:%02x/%02x/%02x\a",
            c64_palette[12].r, c64_palette[12].g, c64_palette[12].b);
    fprintf(stream, "\033]11;rgb:%02x/%02x/%02x\a",
            c64_palette[4].r, c64_palette[4].g, c64_palette[4].b);
    fprintf(stream, "\033]12;rgb:%02x/%02x/%02x\a",
            c64_palette[7].r, c64_palette[7].g, c64_palette[7].b);

    fflush(stream);
}

static bool apply_linux_console_palette(int fd) {
#ifdef __linux__
    unsigned char cmap[48];

    for (int i = 0; i < 16; ++i) {
        cmap[i] = c64_palette[i].r;
        cmap[16 + i] = c64_palette[i].g;
        cmap[32 + i] = c64_palette[i].b;
    }

    if (ioctl(fd, PIO_CMAP, cmap) == 0)
        return true;

    /*
     * Fall back to the Linux console escape sequence if the ioctl fails.
     * Format: ESC ] P <index> <rrggbb>
     */
    for (int i = 0; i < 16; ++i) {
        fprintf(stdout, "\033]P%1x%02x%02x%02x",
                i, c64_palette[i].r, c64_palette[i].g, c64_palette[i].b);
    }
    fflush(stdout);
    return true;
#else
    (void)fd;
    return false;
#endif
}

int main(void) {
    if (!isatty(STDOUT_FILENO)) {
        fputs("_THEME: stdout is not a TTY.\n", stderr);
        return EXIT_FAILURE;
    }

    const char *term = getenv("TERM");
    bool is_linux_console = term != NULL && strcmp(term, "linux") == 0;

    if (is_linux_console) {
        if (!apply_linux_console_palette(STDOUT_FILENO)) {
            fputs("_THEME: failed to apply Linux console palette.\n", stderr);
            return EXIT_FAILURE;
        }
    } else {
        apply_xterm_palette(stdout);
    }

    emit_default_colors();
    fflush(stdout);
    fputs("Commodore 64 theme applied.\n", stderr);
    return EXIT_SUCCESS;
}
