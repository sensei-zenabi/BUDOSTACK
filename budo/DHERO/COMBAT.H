#include "runtime.h"
#ifndef DUNGEON_HERO_COMBAT_H
#define DUNGEON_HERO_COMBAT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMBAT_VGA    0xA0000
#define COMBAT_WIDTH  320
#define COMBAT_HEIGHT 200

static void combat_put_pixel(int x, int y, unsigned char color)
{
    if (x < 0 || y < 0 || x >= COMBAT_WIDTH || y >= COMBAT_HEIGHT) return;
    dhero_pixels_write(&color, 1, COMBAT_VGA + y * COMBAT_WIDTH + x);
}

static void combat_fill_rect(int x, int y, int w, int h,
                             unsigned char color)
{
    unsigned char row[COMBAT_WIDTH];
    int i;
    int yy;

    if (w <= 0 || h <= 0 || x < 0 || y < 0 ||
        x + w > COMBAT_WIDTH || y + h > COMBAT_HEIGHT) return;

    for (i = 0; i < w; i++) row[i] = color;
    for (yy = 0; yy < h; yy++)
        dhero_pixels_write(row, w, COMBAT_VGA + (y + yy) * COMBAT_WIDTH + x);
}

static void combat_box(int x, int y, int w, int h)
{
    int i;

    combat_fill_rect(x, y, w, h, 0);
    for (i = x; i < x + w; i++)
    {
        combat_put_pixel(i, y, 255);
        combat_put_pixel(i, y + h - 1, 255);
        combat_put_pixel(i, y + 2, 252);
        combat_put_pixel(i, y + h - 3, 252);
    }
    for (i = y; i < y + h; i++)
    {
        combat_put_pixel(x, i, 255);
        combat_put_pixel(x + w - 1, i, 255);
    }
}

static void combat_text(int row, int col, const char *text,
                        unsigned char color)
{
    dhero_text(row, col, text, color);
}

static void combat_wait_enter(void)
{
    int key;

    while (1)
    {
        key = dhero_getch();
        if (key == 13) return;
        if (key == 0 || key == 0xE0) dhero_getch();
    }
}

/*
   Two-stage combat model:

   1. Hit check: roll 1d6 against the defender's DEFENSE.
      A roll greater than or equal to DEFENSE hits.

   2. Damage: on a hit, roll another 1d6. The attacker's ATTACK value is
      the maximum damage, so damage = min(d6, ATTACK).

   The same resolver is used for hero and enemy attacks.
*/
static int combat_roll_dialog(const char *attacker,
                              int attack_max,
                              const char *defender,
                              int defense)
{
    int hit_die;
    int damage_die;
    int damage;
    char line[48];

    if (attack_max < 0) attack_max = 0;
    if (defense < 0) defense = 0;

    combat_box(28, 34, 264, 132);
    combat_text(5, 15, lang_get("COMBAT_TITLE"), 255);

    snprintf(line, sizeof(line), lang_get("COMBAT_ATTACKER"), attacker, attack_max);
    combat_text(7, 5, line, 254);
    snprintf(line, sizeof(line), lang_get("COMBAT_DEFENDER"), defender, defense);
    combat_text(9, 5, line, 254);

    combat_text(12, 6, lang_get("COMBAT_PRESS_HIT"), 255);
    combat_wait_enter();

    hit_die = (rand() % 6) + 1;

    combat_box(28, 34, 264, 132);
    combat_text(5, 15, lang_get("COMBAT_TITLE"), 255);
    snprintf(line, sizeof(line), lang_get("COMBAT_ATTACKER"), attacker, attack_max);
    combat_text(7, 5, line, 253);
    snprintf(line, sizeof(line), lang_get("COMBAT_DEFENDER"), defender, defense);
    combat_text(9, 5, line, 253);
    snprintf(line, sizeof(line), lang_get("COMBAT_HIT_ROLL"), hit_die, defense);
    combat_text(11, 5, line, 254);

    if (hit_die < defense || attack_max <= 0)
    {
        combat_text(14, 8, lang_get("COMBAT_MISS"), 255);
        combat_text(16, 8, lang_get("COMBAT_CONTINUE"), 252);
        combat_wait_enter();
        return 0;
    }

    combat_text(13, 8, lang_get("COMBAT_HIT_CHECK_OK"), 255);
    combat_text(15, 5, lang_get("COMBAT_PRESS_DAMAGE"), 255);
    combat_wait_enter();

    damage_die = (rand() % 6) + 1;
    damage = damage_die;
    if (damage > attack_max) damage = attack_max;

    combat_box(28, 34, 264, 132);
    combat_text(5, 15, lang_get("COMBAT_TITLE"), 255);
    snprintf(line, sizeof(line), lang_get("COMBAT_ATTACKER"), attacker, attack_max);
    combat_text(7, 5, line, 253);
    snprintf(line, sizeof(line), lang_get("COMBAT_DEFENDER"), defender, defense);
    combat_text(9, 5, line, 253);
    snprintf(line, sizeof(line), lang_get("COMBAT_HIT_ROLL"), hit_die, defense);
    combat_text(11, 5, line, 254);
    snprintf(line, sizeof(line), lang_get("COMBAT_DAMAGE_ROLL"),
            damage_die, attack_max, damage);
    combat_text(13, 5, line, 254);
    snprintf(line, sizeof(line), lang_get("COMBAT_HIT"), damage);
    combat_text(15, 8, line, 255);
    combat_text(16, 8, lang_get("COMBAT_CONTINUE"), 252);
    combat_wait_enter();

    return damage;
}

#endif
