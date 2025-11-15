#define _POSIX_C_SOURCE 200809L

#include "retroprofile.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define RETRO_MKDIR(path) _mkdir(path)
#define strcasecmp _stricmp
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
#else
#include <unistd.h>
#define RETRO_MKDIR(path) mkdir(path, 0777)
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#endif

#ifndef RETROPROFILE_STATE_PATH
#define RETROPROFILE_STATE_PATH "users/.retroprofile"
#endif

static const RetroProfile retro_profiles[] = {
    {
        "c64",
        "Commodore 64",
        "Vibrant palette tuned for crisp 8-bit sprites and SID editors.",
        {
            {0, 0, 0},       // 0 black anchor used for borders/background
            {255, 255, 255}, // 1 bright white highlight for sprites/text
            {136, 0, 0},     // 2 deep red accent typical for C64 UI
            {170, 255, 238}, // 3 cyan highlight for water/sky motifs
            {204, 68, 204},  // 4 magenta for character art/shadows
            {0, 204, 85},    // 5 emerald green for HUD elements
            {0, 0, 170},     // 6 navy blue for command areas
            {238, 238, 119}, // 7 pastel yellow for warm mid-tones
            {221, 136, 85},  // 8 tan/brown skin-tone shade
            {102, 68, 0},    // 9 dark brown for outlines
            {255, 119, 119}, // 10 light red for alerts
            {51, 51, 51},    // 11 charcoal gray for dim UI chrome
            {119, 119, 119}, // 12 mid gray for neutral panels
            {170, 255, 102}, // 13 lime highlight for energy meters
            {0, 136, 255},   // 14 azure for menu selections
            {187, 187, 187}, // 15 bright gray fallback neutral
        },
        {
            {255, 255, 255}, // default foreground: vivid white text
            {0, 0, 170},     // default background: deep blue backdrop
            {255, 255, 255}, // cursor: same white for consistency
        },
    },
    {
        "ibm5150",
        "IBM 5150 CGA",
        "High-contrast DOS tones ideal for ANSI art and BBS sessions.",
        {
            {0, 0, 0},       // 0 pure black for DOS backdrops
            {0, 0, 170},     // 1 primary blue for prompts
            {0, 170, 0},     // 2 primary green for success text
            {0, 170, 170},   // 3 cyan for selection bars
            {170, 0, 0},     // 4 strong red for critical warnings
            {170, 0, 170},   // 5 magenta for system banners
            {170, 85, 0},    // 6 brown/orange for UI dividers
            {170, 170, 170}, // 7 light gray for default text
            {85, 85, 85},    // 8 dark gray for shadowed text
            {85, 85, 255},   // 9 bright blue for hyperlinks
            {85, 255, 85},   // 10 bright green for OK states
            {85, 255, 255},  // 11 bright cyan for status panels
            {255, 85, 85},   // 12 bright red for errors
            {255, 85, 255},  // 13 bright magenta for prompts
            {255, 255, 85},  // 14 bright yellow for attention markers
            {255, 255, 255}, // 15 pure white for emphasis
        },
        {
            {170, 170, 170}, // default foreground: CGA light gray
            {0, 0, 0},       // default background: void black
            {255, 255, 255}, // cursor: white block caret
        },
    },
    {
        "vt220-amber",
        "VT220 Amber",
        "Warm monochrome amber with subtle intensity steps for long sessions.",
        {
            {86, 39, 0},    // 0 darkest amber for background
            {92, 42, 0},    // 1 slightly brighter base shadow
            {99, 45, 0},    // 2 low glow baseline
            {106, 48, 0},   // 3 step toward readable text
            {112, 51, 0},   // 4 dim text accent
            {119, 54, 0},   // 5 muted glow for separators
            {125, 57, 0},   // 6 soft amber mid-tone
            {132, 60, 0},   // 7 brighter mid-tone for UI
            {179, 90, 10},  // 8 strong amber highlight
            {193, 102, 20}, // 9 brighter highlight for active elements
            {207, 115, 30}, // 10 warm highlight for prompts
            {221, 128, 45}, // 11 default text glow
            {235, 141, 60}, // 12 bright text alt
            {242, 155, 78}, // 13 luminous amber for focus
            {247, 170, 100}, // 14 near-peak glow for emphasis
            {255, 188, 128}, // 15 brightest amber for cursor/alerts
        },
        {
            {221, 128, 45}, // default foreground: glowing amber text
            {33, 15, 0},    // default background: deep amber black
            {247, 170, 100}, // cursor: intense amber block
        },
    },
    {
        "vt220-green",
        "VT220 Green",
        "Phosphor-green ladder inspired by DEC monochrome terminals.",
        {
            {0, 0, 0},      // 0 phosphor off black
            {0, 10, 0},     // 1 faint glow baseline
            {0, 22, 0},     // 2 subtle green shadow
            {0, 34, 0},     // 3 low-intensity scanline
            {0, 46, 0},     // 4 darker mid-tone
            {0, 58, 0},     // 5 low mid-tone glow
            {0, 70, 0},     // 6 muted green ramp
            {0, 82, 0},     // 7 deeper glow before highlights
            {10, 102, 10},  // 8 soft green text base
            {20, 118, 20},  // 9 brighter base text
            {30, 134, 30},  // 10 warm green highlight
            {45, 150, 45},  // 11 standard text intensity
            {60, 166, 60},  // 12 active selection glow
            {78, 182, 78},  // 13 strong highlight
            {96, 198, 96},  // 14 bright text/foreground
            {124, 216, 124}, // 15 cursor/alert green
        },
        {
            {96, 198, 96},  // default foreground: bright green text
            {0, 0, 0},      // default background: void black
            {124, 216, 124}, // cursor: vivid green block
        },
    },
};

static const RetroProfile *retroprofile_validate(const char *key) {
    if (key == NULL)
        return NULL;
    size_t count = retroprofile_count();
    for (size_t i = 0; i < count; ++i) {
        const RetroProfile *profile = &retro_profiles[i];
        if (strcasecmp(profile->key, key) == 0)
            return profile;
    }
    return NULL;
}

size_t retroprofile_count(void) {
    return sizeof(retro_profiles) / sizeof(retro_profiles[0]);
}

const RetroProfile *retroprofile_get(size_t index) {
    if (index >= retroprofile_count())
        return NULL;
    return &retro_profiles[index];
}

const RetroProfile *retroprofile_find(const char *key) {
    return retroprofile_validate(key);
}

const RetroProfile *retroprofile_default(void) {
    return &retro_profiles[0];
}

static const char *state_path(void) {
    const char *env = getenv("BUDOSTACK_RETROPROFILE_STATE");
    if (env != NULL && env[0] != '\0')
        return env;
    return RETROPROFILE_STATE_PATH;
}

static int ensure_directory(const char *path) {
    if (path == NULL || *path == '\0')
        return -1;

    size_t path_len = strlen(path);
    if (path_len >= PATH_MAX)
        return -1;

    char buffer[PATH_MAX];
    memcpy(buffer, path, path_len + 1);

    for (size_t i = 0; i < path_len; ++i) {
        if (buffer[i] != '/'
#ifdef _WIN32
            && buffer[i] != '\\'
#endif
        )
            continue;

        buffer[i] = '\0';
#ifdef _WIN32
        if (i == 2 && buffer[1] == ':') {
            buffer[i] = path[i];
            continue;
        }
#endif
        if (buffer[0] != '\0') {
            if (RETRO_MKDIR(buffer) != 0 && errno != EEXIST)
                return -1;
        }
        buffer[i] = path[i];
    }

    return 0;
}

static int read_state(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0)
        return -1;

    const char *path = state_path();
    FILE *file = fopen(path, "r");
    if (file == NULL)
        return -1;

    if (fgets(buffer, (int)buffer_size, file) == NULL) {
        fclose(file);
        return -1;
    }

    fclose(file);

    size_t len = strcspn(buffer, "\r\n");
    buffer[len] = '\0';
    return 0;
}

const RetroProfile *retroprofile_active(void) {
    char key_buffer[64];
    if (read_state(key_buffer, sizeof(key_buffer)) != 0)
        return retroprofile_default();

    const RetroProfile *profile = retroprofile_validate(key_buffer);
    if (profile == NULL)
        return retroprofile_default();

    return profile;
}

int retroprofile_set_active(const char *key) {
    const RetroProfile *profile = retroprofile_validate(key);
    if (profile == NULL)
        return -1;

    const char *path = state_path();
    if (ensure_directory(path) != 0)
        return -1;

    FILE *file = fopen(path, "w");
    if (file == NULL)
        return -1;

    if (fprintf(file, "%s\n", profile->key) < 0) {
        fclose(file);
        return -1;
    }

    if (fclose(file) != 0)
        return -1;

    return 0;
}

int retroprofile_clear_active(void) {
    const char *path = state_path();
    if (remove(path) == 0)
        return 0;
    if (errno == ENOENT)
        return 0;
    return -1;
}

int retroprofile_color_from_active(int index, RetroColor *out_color) {
    if (out_color == NULL)
        return -1;

    const RetroProfile *profile = retroprofile_active();
    if (profile == NULL)
        return -1;

    if (index < 0 || index >= 16)
        return -1;

    *out_color = profile->colors[index];
    return 0;
}

static int retroprofile_color_equals(const RetroColor *a, const RetroColor *b) {
    if (a == NULL || b == NULL)
        return 0;
    return a->r == b->r && a->g == b->g && a->b == b->b;
}

int retroprofile_color_index(const RetroProfile *profile, RetroColor color) {
    if (profile == NULL)
        return -1;

    for (int i = 0; i < 16; ++i) {
        if (retroprofile_color_equals(&profile->colors[i], &color))
            return i;
    }

    return -1;
}

int retroprofile_active_default_foreground_index(void) {
    const RetroProfile *profile = retroprofile_active();
    if (profile == NULL)
        return -1;

    return retroprofile_color_index(profile, profile->defaults.foreground);
}
