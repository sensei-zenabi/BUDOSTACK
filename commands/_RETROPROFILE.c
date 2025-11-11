#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} RgbColor;

typedef struct {
    RgbColor foreground;
    RgbColor background;
    RgbColor cursor;
} RetroDefaults;

typedef struct {
    const char *key;
    const char *display_name;
    const char *description;
    RgbColor colors[16];
    RetroDefaults defaults;
} RetroProfile;

static const RetroProfile profiles[] = {
    {
        "c64",
        "Commodore 64",
        "Vibrant palette tuned for crisp 8-bit sprites and SID editors.",
        {
            {0, 0, 0},
            {255, 255, 255},
            {136, 0, 0},
            {170, 255, 238},
            {204, 68, 204},
            {0, 204, 85},
            {0, 0, 170},
            {238, 238, 119},
            {221, 136, 85},
            {102, 68, 0},
            {255, 119, 119},
            {51, 51, 51},
            {119, 119, 119},
            {170, 255, 102},
            {0, 136, 255},
            {187, 187, 187},
        },
        {
            {170, 255, 238},
            {0, 0, 170},
            {255, 255, 255},
        },
    },
    {
        "ibm5150",
        "IBM 5150 CGA",
        "High-contrast DOS tones ideal for ANSI art and BBS sessions.",
        {
            {0, 0, 0},
            {0, 0, 170},
            {0, 170, 0},
            {0, 170, 170},
            {170, 0, 0},
            {170, 0, 170},
            {170, 85, 0},
            {170, 170, 170},
            {85, 85, 85},
            {85, 85, 255},
            {85, 255, 85},
            {85, 255, 255},
            {255, 85, 85},
            {255, 85, 255},
            {255, 255, 85},
            {255, 255, 255},
        },
        {
            {170, 170, 170},
            {0, 0, 0},
            {255, 255, 255},
        },
    },
    {
        "vt220-amber",
        "VT220 Amber",
        "Warm monochrome amber with subtle intensity steps for long sessions.",
        {
            {0, 0, 0},
            {22, 10, 0},
            {45, 20, 0},
            {67, 30, 0},
            {89, 40, 0},
            {112, 50, 0},
            {134, 60, 0},
            {156, 70, 0},
            {179, 90, 10},
            {193, 102, 20},
            {207, 115, 30},
            {221, 128, 45},
            {235, 141, 60},
            {242, 155, 78},
            {247, 170, 100},
            {255, 188, 128},
        },
        {
            {221, 128, 45},
            {0, 0, 0},
            {247, 170, 100},
        },
    },
    {
        "vt220-green",
        "VT220 Green",
        "Phosphor-green ladder inspired by DEC monochrome terminals.",
        {
            {0, 0, 0},
            {0, 10, 0},
            {0, 22, 0},
            {0, 34, 0},
            {0, 46, 0},
            {0, 58, 0},
            {0, 70, 0},
            {0, 82, 0},
            {10, 102, 10},
            {20, 118, 20},
            {30, 134, 30},
            {45, 150, 45},
            {60, 166, 60},
            {78, 182, 78},
            {96, 198, 96},
            {124, 216, 124},
        },
        {
            {96, 198, 96},
            {0, 0, 0},
            {124, 216, 124},
        },
    },
};

static void usage(void) {
    fprintf(stderr,
            "Usage: _RETROPROFILE <command> [profile]\n"
            "Commands:\n"
            "  list               List available profiles.\n"
            "  show <profile>     Show palette values and a color swatch.\n"
            "  apply <profile>    Emit OSC 4/10/11/12 escapes to set palette and defaults.\n"
            "  reset              Reset palette and defaults (OSC 104/110/111/112).\n"
            "\nProfiles are case-insensitive. Redirect output from 'apply' into your shell\n"
            "if you want to persist the palette, e.g. _RETROPROFILE apply c64 > /tmp/palette && cat /tmp/palette.\n");
}

static void list_profiles(void) {
    size_t count = sizeof(profiles) / sizeof(profiles[0]);
    for (size_t i = 0; i < count; ++i) {
        const RetroProfile *profile = &profiles[i];
        printf("%-12s %s\n", profile->key, profile->display_name);
        printf("    %s\n", profile->description);
    }
}

static const RetroProfile *find_profile(const char *key) {
    if (key == NULL)
        return NULL;
    size_t count = sizeof(profiles) / sizeof(profiles[0]);
    for (size_t i = 0; i < count; ++i) {
        const RetroProfile *profile = &profiles[i];
        if (strcasecmp(profile->key, key) == 0)
            return profile;
    }
    return NULL;
}

static void show_profile(const RetroProfile *profile) {
    printf("%s (%s)\n", profile->display_name, profile->key);
    printf("%s\n\n", profile->description);
    printf("Defaults: foreground #%02X%02X%02X, background #%02X%02X%02X, cursor #%02X%02X%02X\n\n",
           profile->defaults.foreground.r,
           profile->defaults.foreground.g,
           profile->defaults.foreground.b,
           profile->defaults.background.r,
           profile->defaults.background.g,
           profile->defaults.background.b,
           profile->defaults.cursor.r,
           profile->defaults.cursor.g,
           profile->defaults.cursor.b);
    for (int i = 0; i < 16; ++i) {
        const RgbColor *color = &profile->colors[i];
        printf("%2d  #%02X%02X%02X  \x1b[48;2;%d;%d;%dm  \x1b[0m\n",
               i,
               color->r,
               color->g,
               color->b,
               color->r,
               color->g,
               color->b);
    }
}

static void emit_osc(const char *fmt, ...) {
    char buffer[128];
    va_list args;

    va_start(args, fmt);
    int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (written < 0)
        return;

    fwrite("\033]", 1, 2, stdout);
    if ((size_t)written >= sizeof(buffer)) {
        fwrite(buffer, 1, sizeof(buffer) - 1, stdout);
    } else {
        fwrite(buffer, 1, (size_t)written, stdout);
    }
    fwrite("\033\\", 1, 2, stdout);
}

static void emit_palette_sequence(const RetroProfile *profile) {
    for (int i = 0; i < 16; ++i) {
        const RgbColor *color = &profile->colors[i];
        emit_osc("4;%d;#%02X%02X%02X", i, color->r, color->g, color->b);
    }
    emit_osc("10;#%02X%02X%02X",
             profile->defaults.foreground.r,
             profile->defaults.foreground.g,
             profile->defaults.foreground.b);
    emit_osc("11;#%02X%02X%02X",
             profile->defaults.background.r,
             profile->defaults.background.g,
             profile->defaults.background.b);
    emit_osc("12;#%02X%02X%02X",
             profile->defaults.cursor.r,
             profile->defaults.cursor.g,
             profile->defaults.cursor.b);
    fflush(stdout);
    fprintf(stderr,
            "Applied '%s' palette to terminal (OSC 4/10/11/12). Use 'reset' to restore defaults.\n",
            profile->display_name);
}

static void reset_palette(void) {
    emit_osc("104;");
    emit_osc("110;");
    emit_osc("111;");
    emit_osc("112;");
    fflush(stdout);
    fprintf(stderr, "Requested terminal palette/default reset via OSC 104/110/111/112.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage();
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "list") == 0) {
        list_profiles();
        return EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "show") == 0) {
        if (argc < 3) {
            fprintf(stderr, "_RETROPROFILE: missing profile for 'show' command.\n");
            return EXIT_FAILURE;
        }
        const RetroProfile *profile = find_profile(argv[2]);
        if (profile == NULL) {
            fprintf(stderr, "_RETROPROFILE: unknown profile '%s'.\n", argv[2]);
            return EXIT_FAILURE;
        }
        show_profile(profile);
        return EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "apply") == 0) {
        if (argc < 3) {
            fprintf(stderr, "_RETROPROFILE: missing profile for 'apply' command.\n");
            return EXIT_FAILURE;
        }
        const RetroProfile *profile = find_profile(argv[2]);
        if (profile == NULL) {
            fprintf(stderr, "_RETROPROFILE: unknown profile '%s'.\n", argv[2]);
            return EXIT_FAILURE;
        }
        emit_palette_sequence(profile);
        return EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "reset") == 0) {
        reset_palette();
        return EXIT_SUCCESS;
    }

    usage();
    return EXIT_FAILURE;
}
