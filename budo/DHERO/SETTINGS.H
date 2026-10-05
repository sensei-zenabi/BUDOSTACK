#include "runtime.h"
#ifndef DUNGEON_HERO_SETTINGS_H
#define DUNGEON_HERO_SETTINGS_H

#include <stdio.h>
#include <string.h>

#define SETTINGS_FILE "DATA/GAME/SETTINGS.DAT"

static int settings_loaded = 0;

static void settings_load(void)
{
    FILE *file;
    char line[80];
    int value;

    if (settings_loaded) return;
    settings_loaded = 1;

    sound_enabled = 1;
    ui_language = 0;

    file = fopen(SETTINGS_FILE, "rt");
    if (file == NULL) return;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        if (sscanf(line, "SOUND=%d", &value) == 1)
        {
            if (value == 0 || value == 1) sound_enabled = value;
        }
        else if (sscanf(line, "LANGUAGE=%d", &value) == 1)
        {
            if (value == 0 || value == 1) ui_language = value;
        }
    }

    fclose(file);
}

static void settings_save_values(int sound_value, int language_value)
{
    FILE *file;

    if (sound_value != 0) sound_value = 1;
    if (language_value != 0) language_value = 1;

    file = fopen(SETTINGS_FILE, "wt");
    if (file == NULL) return;

    fprintf(file, "; DUNGEON HERO persistent settings\n");
    fprintf(file, "SOUND=%d\n", sound_value);
    fprintf(file, "LANGUAGE=%d\n", language_value);
    fclose(file);
}

#endif
