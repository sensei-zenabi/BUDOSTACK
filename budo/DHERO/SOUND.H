#include "runtime.h"
#ifndef SOUND_H
#define SOUND_H

#include <stdlib.h>
#include <string.h>

#define SPEAKER_PIT_HZ 1193180L

/* Runtime menu options shared by the single MAIN.C translation unit. */
static int sound_enabled = 1;
static int ui_language = 0; /* 0 = ENG, 1 = FIN */

#include "SETTINGS.H"

/* Small input-state tracker used only to expose the separate editor from the
   existing two-row Options popup without coupling editor code into MAIN.C. */
static int editor_menu_active = 0;
static int editor_menu_selection = 0;
static int editor_options_active = 0;
static int editor_options_selection = 0;
static int editor_extended_pending = 0;
static int editor_missing_notice = 0;

typedef struct
{
    unsigned int frequency;
    unsigned int duration_ms;
} SpeakerNote;

static void speaker_stop(void)
{
    dhero_tone(0, 0);
}

static void speaker_tone(unsigned int frequency, unsigned int duration_ms)
{
    if (sound_enabled) dhero_tone(frequency, duration_ms);
}

static void speaker_sequence(const SpeakerNote *notes, int count)
{
    int i;
    if (!sound_enabled) { speaker_stop(); return; }
    for (i = 0; i < count; i++)
        speaker_tone(notes[i].frequency, notes[i].duration_ms);
}

static void sound_presents(void)
{
    static const SpeakerNote notes[] =
    { { 196, 90 }, { 0, 30 }, { 294, 90 }, { 392, 110 }, { 588, 180 } };
    speaker_sequence(notes, sizeof(notes) / sizeof(notes[0]));
}

static void sound_intro(void)
{
    static const SpeakerNote notes[] =
    {
        { 262, 120 }, { 330, 120 }, { 392, 160 }, { 0, 50 },
        { 330, 100 }, { 392, 100 }, { 523, 220 }, { 0, 70 },
        { 494, 100 }, { 392, 120 }, { 330, 120 }, { 262, 240 }
    };
    editor_menu_active = 1;
    editor_menu_selection = 0;
    editor_options_active = 0;
    editor_options_selection = 0;
    editor_extended_pending = 0;
    editor_missing_notice = 0;
    speaker_sequence(notes, sizeof(notes) / sizeof(notes[0]));
}

static void sound_move_player(void)
{
    static const SpeakerNote notes[] = { { 180, 9 }, { 230, 9 } };
    speaker_sequence(notes, 2);
}

static void sound_move_enemy(void)
{
    static const SpeakerNote notes[] = { { 120, 8 }, { 95, 8 } };
    speaker_sequence(notes, 2);
}

static void sound_interact(void)
{
    static const SpeakerNote notes[] = { { 440, 35 }, { 660, 55 } };
    speaker_sequence(notes, 2);
}

static void sound_enemy_discovery(void)
{
    static const SpeakerNote notes[] = { { 220, 45 }, { 440, 55 }, { 880, 80 } };
    speaker_sequence(notes, 3);
}

static void sound_finish(void)
{
    static const SpeakerNote notes[] = { { 392, 80 }, { 523, 90 }, { 659, 100 }, { 784, 180 } };
    speaker_sequence(notes, 4);
}

static void sound_battle(void)
{
    static const SpeakerNote notes[] = { { 150, 22 }, { 260, 22 }, { 130, 32 } };
    speaker_sequence(notes, 3);
}

static void sound_damage(void)
{
    static const SpeakerNote notes[] = { { 180, 35 }, { 110, 60 } };
    speaker_sequence(notes, 2);
}

static void sound_enemy_killed(void)
{
    static const SpeakerNote notes[] = { { 520, 45 }, { 390, 55 }, { 260, 90 } };
    speaker_sequence(notes, 3);
}

static void sound_level_up(void)
{
    static const SpeakerNote notes[] =
    { { 262, 65 }, { 330, 65 }, { 392, 65 }, { 523, 75 }, { 659, 75 }, { 784, 150 } };
    speaker_sequence(notes, 6);
}

#include "LANG.H"
#include "COMBAT.H"
#include "SCORE.H"

static void editor_set_mode(int mode)
{
    dhero_mode(mode);
}

static void editor_overlay_pixel(int x, int y, unsigned char color)
{
    if (x < 0 || y < 0 || x >= 320 || y >= 200) return;
    dhero_pixels_write(&color, 1, 0xA0000 + y * 320 + x);
}

static void editor_overlay_fill(int x, int y, int w, int h, unsigned char color)
{
    unsigned char row[320];
    int i;
    int yy;
    if (w <= 0 || h <= 0) return;
    for (i = 0; i < w; i++) row[i] = color;
    for (yy = 0; yy < h; yy++)
        dhero_pixels_write(row, w, 0xA0000 + (y + yy) * 320 + x);
}

static void editor_overlay_text(int row, int col, const char *text,
                                unsigned char color)
{
    dhero_text(row, col, text, color);
}

static void editor_overlay_centered_text(int row, const char *text,
                                         unsigned char color)
{
    int col;
    int len;

    len = (int)strlen(text);
    col = (40 - len) / 2;
    if (col < 0) col = 0;
    editor_overlay_text(row, col, text, color);
}

static void editor_redraw_options(int selected, const char *notice)
{
    int i;
    char line[40];

    editor_overlay_fill(36, 42, 248, 116, 0);
    for (i = 36; i < 284; i++)
    {
        editor_overlay_pixel(i, 42, 254);
        editor_overlay_pixel(i, 157, 254);
        editor_overlay_pixel(i, 44, 252);
        editor_overlay_pixel(i, 155, 252);
    }
    for (i = 42; i < 158; i++)
    {
        editor_overlay_pixel(36, i, 254);
        editor_overlay_pixel(283, i, 254);
    }

    editor_overlay_centered_text(6, lang_get("OPT_TITLE"), 255);
    snprintf(line, sizeof(line), "%c %s: %s", selected == 0 ? '>' : ' ',
            lang_get("OPT_SOUNDS"), lang_get(sound_enabled ? "OPT_ON" : "OPT_OFF"));
    editor_overlay_centered_text(8, line, selected == 0 ? 255 : 253);
    snprintf(line, sizeof(line), "%c %s: %s", selected == 1 ? '>' : ' ',
            lang_get("OPT_LANGUAGE"), lang_get(ui_language ? "OPT_LANG_FIN" : "OPT_LANG_ENG"));
    editor_overlay_centered_text(10, line, selected == 1 ? 255 : 253);
    editor_overlay_centered_text(12, lang_get("OPT_EDITOR"), 254);
    editor_overlay_centered_text(15,
                                 notice ? notice : lang_get("OPT_HELP"), 252);
}

static void editor_launch(void)
{
    int result;
    speaker_stop();
    editor_set_mode(3);
    result = dhero_editor_run();
    load_pcx("DATA/GFX/FULL/FULL000.pcx");
    editor_missing_notice = (result == -1);
    editor_redraw_options(editor_options_selection,
                          editor_missing_notice ? lang_get("OPT_EDITOR_MISSING") : NULL);
}

/* Wrapper applies only to MAIN.C calls because it is defined after the
   COMBAT.H/SCORE.H bodies above. */
static int editor_options_dhero_getch(void)
{
    int key;
    int next_sound;
    int next_language;

    if (editor_options_active)
        editor_redraw_options(editor_options_selection,
                              editor_missing_notice ? lang_get("OPT_EDITOR_MISSING") : NULL);

    key = dhero_getch();

    if (editor_extended_pending)
    {
        editor_extended_pending = 0;
        if (editor_menu_active && !editor_options_active)
        {
            if (key == 72) { editor_menu_selection--; if (editor_menu_selection < 0) editor_menu_selection = 2; }
            else if (key == 80) { editor_menu_selection++; if (editor_menu_selection > 2) editor_menu_selection = 0; }
        }
        else if (editor_options_active)
        {
            if (key == 72 || key == 80)
                editor_options_selection = 1 - editor_options_selection;
            else if (key == 75 || key == 77)
            {
                next_sound = sound_enabled;
                next_language = ui_language;
                if (editor_options_selection == 0) next_sound = !next_sound;
                else next_language = !next_language;
                settings_save_values(next_sound, next_language);
            }
        }
        editor_missing_notice = 0;
        return key;
    }

    if (key == 0 || key == 0xE0)
    {
        editor_extended_pending = 1;
        return key;
    }

    if (editor_options_active)
    {
        if (key == 'e' || key == 'E') { editor_launch(); return key; }
        if (key == 13)
        {
            next_sound = sound_enabled;
            next_language = ui_language;
            if (editor_options_selection == 0) next_sound = !next_sound;
            else next_language = !next_language;
            settings_save_values(next_sound, next_language);
        }
        if (key == 27) editor_options_active = 0;
        editor_missing_notice = 0;
        return key;
    }

    if (editor_menu_active)
    {
        if (key == 13)
        {
            if (editor_menu_selection == 1)
            {
                editor_options_active = 1;
                editor_options_selection = 0;
            }
            else editor_menu_active = 0;
        }
        else if (key == 27) editor_menu_active = 0;
    }
    return key;
}

#define dhero_getch editor_options_dhero_getch

#endif
