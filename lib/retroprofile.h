#ifndef RETROPROFILE_H
#define RETROPROFILE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} RetroColor;

typedef struct {
    RetroColor foreground;
    RetroColor background;
    RetroColor cursor;
} RetroDefaults;

typedef struct {
    const char *key;
    const char *display_name;
    const char *description;
    RetroColor colors[16];
    RetroDefaults defaults;
} RetroProfile;

size_t retroprofile_count(void);
const RetroProfile *retroprofile_get(size_t index);
const RetroProfile *retroprofile_find(const char *key);
const RetroProfile *retroprofile_default(void);
const RetroProfile *retroprofile_active(void);
int retroprofile_set_active(const char *key);
int retroprofile_clear_active(void);
int retroprofile_color_from_active(int index, RetroColor *out_color);
int retroprofile_color_index(const RetroProfile *profile, RetroColor color);
int retroprofile_active_default_foreground_index(void);
const char *retroprofile_override_path(void);
int retroprofile_save_prf(const char *path, const RetroProfile *profiles, size_t profile_count);
int retroprofile_load_prf(const char *path, RetroProfile *profiles, size_t profile_count);

#ifdef __cplusplus
}
#endif

#endif /* RETROPROFILE_H */
