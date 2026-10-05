#include "runtime.h"
#ifndef LANG_H
#define LANG_H

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define LANG_ENG_FILE "DATA/LANG/ENG.DAT"
#define LANG_FIN_FILE "DATA/LANG/FIN.DAT"
#define LANG_MAX_ENTRIES 192
#define LANG_KEY_LEN 40
#define LANG_VALUE_LEN 96

typedef struct
{
    char key[LANG_KEY_LEN];
    char value[LANG_VALUE_LEN];
} LangEntry;

typedef struct
{
    int count;
    LangEntry entries[LANG_MAX_ENTRIES];
} LangDictionary;

static LangDictionary lang_eng;
static LangDictionary lang_fin;
static int lang_loaded = 0;

static char *lang_trim(char *text)
{
    char *end;
    while (*text == ' ' || *text == '\t') text++;
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
                          end[-1] == '\r' || end[-1] == '\n'))
        *--end = '\0';
    return text;
}

static int lang_load_file(const char *filename, LangDictionary *dict)
{
    FILE *file;
    char line[160];
    char *eq;
    char *key;
    char *value;

    dict->count = 0;
    file = fopen(filename, "r");
    if (file == NULL) return 0;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        key = lang_trim(line);
        if (*key == '\0' || *key == '#' || *key == ';') continue;
        eq = strchr(key, '=');
        if (eq == NULL) continue;
        *eq = '\0';
        value = lang_trim(eq + 1);
        key = lang_trim(key);
        if (*key == '\0' || dict->count >= LANG_MAX_ENTRIES) continue;
        strncpy(dict->entries[dict->count].key, key, LANG_KEY_LEN - 1);
        dict->entries[dict->count].key[LANG_KEY_LEN - 1] = '\0';
        strncpy(dict->entries[dict->count].value, value, LANG_VALUE_LEN - 1);
        dict->entries[dict->count].value[LANG_VALUE_LEN - 1] = '\0';
        dict->count++;
    }

    fclose(file);
    return 1;
}

static const char *lang_find(const LangDictionary *dict, const char *key)
{
    int i;
    for (i = 0; i < dict->count; i++)
        if (strcmp(dict->entries[i].key, key) == 0)
            return dict->entries[i].value;
    return NULL;
}

static void lang_init(void)
{
    if (lang_loaded) return;
    settings_load();
    lang_load_file(LANG_ENG_FILE, &lang_eng);
    lang_load_file(LANG_FIN_FILE, &lang_fin);
    lang_loaded = 1;
}

static const char *lang_get(const char *key)
{
    const char *value;

    lang_init();
    value = NULL;
    if (ui_language)
        value = lang_find(&lang_fin, key);
    if (value == NULL || *value == '\0')
        value = lang_find(&lang_eng, key);
    if (value == NULL || *value == '\0')
        return key;
    return value;
}

static int lang_has_english(const char *key)
{
    lang_init();
    return lang_find(&lang_eng, key) != NULL;
}

#endif
