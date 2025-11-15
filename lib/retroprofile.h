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

typedef enum {
    RETROPROFILE_FORMAT_C_PREPROCESSOR = 0,
    RETROPROFILE_FORMAT_C_COMMENT,
    RETROPROFILE_FORMAT_C_STRING,
    RETROPROFILE_FORMAT_C_CHARACTER,
    RETROPROFILE_FORMAT_C_KEYWORD,
    RETROPROFILE_FORMAT_C_KEYWORD_TYPE,
    RETROPROFILE_FORMAT_C_FUNCTION,
    RETROPROFILE_FORMAT_C_NUMBER,
    RETROPROFILE_FORMAT_C_PUNCTUATION,
    RETROPROFILE_FORMAT_TEXT_HEADER,
    RETROPROFILE_FORMAT_TEXT_BULLET,
    RETROPROFILE_FORMAT_TEXT_CODE,
    RETROPROFILE_FORMAT_TEXT_BOLD,
    RETROPROFILE_FORMAT_TEXT_ITALIC,
    RETROPROFILE_FORMAT_TEXT_TAG,
    RETROPROFILE_FORMAT_EDITOR_MODIFIED,
    RETROPROFILE_FORMAT_COUNT
} RetroFormatRole;

typedef struct {
    const char *key;
    const char *display_name;
    const char *description;
    RetroColor colors[16];
    RetroDefaults defaults;
    int format[RETROPROFILE_FORMAT_COUNT];
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
int retroprofile_format_color(const RetroProfile *profile,
                              RetroFormatRole role,
                              RetroColor *out_color);
int retroprofile_active_format_color(RetroFormatRole role,
                                     RetroColor *out_color);

#ifdef __cplusplus
}
#endif

#endif /* RETROPROFILE_H */
