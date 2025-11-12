#include "termbg.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    int x;
    int y;
    int color;
} TermBgEntry;

#define TERM_BG_TRUECOLOR_FLAG (1u << 30)
#define TERM_BG_TRUECOLOR_MASK 0x00FFFFFFu

int termbg_encode_truecolor(int r, int g, int b) {
    if (r < 0)
        r = 0;
    else if (r > 255)
        r = 255;

    if (g < 0)
        g = 0;
    else if (g > 255)
        g = 255;

    if (b < 0)
        b = 0;
    else if (b > 255)
        b = 255;

    return (int)(TERM_BG_TRUECOLOR_FLAG | ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b);
}

int termbg_is_truecolor(int color) {
    if (color < 0)
        return 0;
    return (((unsigned int)color) & TERM_BG_TRUECOLOR_FLAG) != 0;
}

void termbg_decode_truecolor(int color, int *r_out, int *g_out, int *b_out) {
    if (!termbg_is_truecolor(color)) {
        if (r_out)
            *r_out = 0;
        if (g_out)
            *g_out = 0;
        if (b_out)
            *b_out = 0;
        return;
    }

    unsigned int value = (unsigned int)color & TERM_BG_TRUECOLOR_MASK;
    if (r_out)
        *r_out = (int)((value >> 16) & 0xFFu);
    if (g_out)
        *g_out = (int)((value >> 8) & 0xFFu);
    if (b_out)
        *b_out = (int)(value & 0xFFu);
}

static TermBgEntry *g_entries = NULL;
static size_t g_entry_count = 0;
static size_t g_entry_capacity = 0;
static int g_state_loaded = 0;
static int g_state_dirty = 0;

static const char *state_path(void) {
    static char path[PATH_MAX];
    if (path[0] != '\0')
        return path;

    const char *env_path = getenv("BUDOSTACK_BG_STATE");
    if (env_path != NULL && env_path[0] != '\0') {
        strncpy(path, env_path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        return path;
    }

    const char *home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        int written = snprintf(path, sizeof(path), "%s/.budostack/bg_state.txt", home);
        if (written > 0 && written < (int)sizeof(path))
            return path;
    }

    strncpy(path, "./.budostack_bg_state.txt", sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    return path;
}

static int ensure_capacity(size_t needed) {
    if (needed <= g_entry_capacity)
        return 0;

    size_t new_cap = g_entry_capacity == 0 ? 128 : g_entry_capacity * 2;
    while (new_cap < needed)
        new_cap *= 2;

    TermBgEntry *new_entries = realloc(g_entries, new_cap * sizeof(*new_entries));
    if (new_entries == NULL)
        return -1;

    g_entries = new_entries;
    g_entry_capacity = new_cap;
    return 0;
}

static size_t find_entry(int x, int y) {
    for (size_t i = 0; i < g_entry_count; ++i) {
        if (g_entries[i].x == x && g_entries[i].y == y)
            return i;
    }
    return g_entry_count;
}

static void remove_entry(size_t index) {
    if (index >= g_entry_count)
        return;
    g_entries[index] = g_entries[g_entry_count - 1];
    --g_entry_count;
}

static int ensure_directory(const char *dir_path) {
    if (dir_path == NULL || dir_path[0] == '\0')
        return 0;

    char temp[PATH_MAX];
    strncpy(temp, dir_path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    size_t len = strlen(temp);
    if (len == 0)
        return 0;

    if (temp[len - 1] == '/')
        temp[len - 1] = '\0';

    for (char *p = temp + 1; *p != '\0'; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (temp[0] != '\0') {
                if (mkdir(temp, 0700) != 0 && errno != EEXIST)
                    return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(temp, 0700) != 0 && errno != EEXIST)
        return -1;

    return 0;
}

static int ensure_parent_directory(const char *file_path) {
    char buffer[PATH_MAX];
    strncpy(buffer, file_path, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *last_slash = strrchr(buffer, '/');
    if (last_slash == NULL)
        return 0;

    if (last_slash == buffer)
        last_slash[1] = '\0';
    else
        *last_slash = '\0';

    return ensure_directory(buffer);
}

static void load_state(void) {
    if (g_state_loaded)
        return;
    g_state_loaded = 1;

    const char *path = state_path();
    FILE *fp = fopen(path, "r");
    if (!fp)
        return;

    int x, y, color;
    while (fscanf(fp, "%d %d %d", &x, &y, &color) == 3) {
        if (ensure_capacity(g_entry_count + 1) != 0)
            break;
        g_entries[g_entry_count].x = x;
        g_entries[g_entry_count].y = y;
        g_entries[g_entry_count].color = color;
        ++g_entry_count;
    }

    fclose(fp);
    g_state_dirty = 0;
}

int termbg_get(int x, int y, int *color_out) {
    if (x < 0 || y < 0)
        return 0;

    load_state();
    if (!g_entries)
        return 0;

    size_t idx = find_entry(x, y);
    if (idx == g_entry_count)
        return 0;

    if (color_out)
        *color_out = g_entries[idx].color;
    return 1;
}

void termbg_set(int x, int y, int color) {
    if (x < 0 || y < 0)
        return;

    load_state();

    size_t idx = find_entry(x, y);
    if (color < 0) {
        if (idx < g_entry_count) {
            remove_entry(idx);
            g_state_dirty = 1;
        }
        return;
    }

    if (idx < g_entry_count) {
        if (g_entries[idx].color != color) {
            g_entries[idx].color = color;
            g_state_dirty = 1;
        }
        return;
    }

    if (ensure_capacity(g_entry_count + 1) != 0)
        return;

    g_entries[g_entry_count].x = x;
    g_entries[g_entry_count].y = y;
    g_entries[g_entry_count].color = color;
    ++g_entry_count;
    g_state_dirty = 1;
}

int termbg_save(void) {
    if (!g_state_loaded || !g_state_dirty)
        return 0;

    const char *path = state_path();
    if (ensure_parent_directory(path) != 0)
        return -1;

    char tmp_path[PATH_MAX];
    int written = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (written <= 0 || written >= (int)sizeof(tmp_path))
        return -1;

    FILE *fp = fopen(tmp_path, "w");
    if (!fp)
        return -1;

    for (size_t i = 0; i < g_entry_count; ++i) {
        if (fprintf(fp, "%d %d %d\n", g_entries[i].x, g_entries[i].y, g_entries[i].color) < 0) {
            fclose(fp);
            unlink(tmp_path);
            return -1;
        }
    }

    if (fclose(fp) != 0) {
        unlink(tmp_path);
        return -1;
    }

    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        return -1;
    }

    g_state_dirty = 0;
    return 0;
}

void termbg_clear(void) {
    const char *path = state_path();
    if (path != NULL && path[0] != '\0')
        unlink(path);
    termbg_shutdown();
}

void termbg_shutdown(void) {
    free(g_entries);
    g_entries = NULL;
    g_entry_count = 0;
    g_entry_capacity = 0;
    g_state_loaded = 0;
    g_state_dirty = 0;
}
