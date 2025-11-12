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
            {255, 255, 255},
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
