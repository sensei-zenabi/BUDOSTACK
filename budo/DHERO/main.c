#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "PCX.H"
#include "DATA.H"
#include "SOUND.H"

#define MENU_IMAGE       "DATA/GFX/FULL/FULL000.pcx"
#define CHAR_IMAGE       "DATA/GFX/SPRITES/CHAR000.PCX"
#define CHAR_SPEC        "DATA/GFX/SPRITES/CHAR000.DAT"
#define ENEMY_DEFS_FILE  "DATA/GAME/ENEMIES.DAT"
#define ITEM_DEFS_FILE   "DATA/GAME/ITEMS.DAT"

#define SCREEN_WIDTH     320
#define SCREEN_HEIGHT    200
#define VGA_MEMORY       0xA0000
#define VIEW_COLS        20
#define VIEW_ROWS        10
#define HUD_Y            160

#define SELECTOR_X       116
#define SELECTOR_WIDTH   8
#define SELECTOR_HEIGHT  7
#define SELECTOR_COLOR   255

/* Menu selection-arrow vertical center positions in pixels.
   Adjust these three values to align the arrow with FULL000.pcx text. */
#define MENU_ARROW_NEW_GAME_Y  106
#define MENU_ARROW_OPTIONS_Y   125
#define MENU_ARROW_EXIT_Y      143

#define MENU_NEW_GAME    0
#define MENU_OPTIONS     1
#define MENU_EXIT        2
#define MENU_COUNT       3

#define KEY_ENTER        13
#define KEY_ESC          27
#define KEY_SPACE        32
#define KEY_UP           72
#define KEY_DOWN         80
#define KEY_LEFT         75
#define KEY_RIGHT        77

#define DIR_DOWN         0
#define DIR_RIGHT        1
#define DIR_LEFT         2
#define DIR_UP           3

#define MAX_RUNTIME_ENEMIES DATA_MAX_ENEMIES
#define LOS_SCALE        16
#define LOS_CENTER       8
#define LOS_INSET        2
#define INVENTORY_ROWS   8

typedef struct
{
    int x;
    int y;
    int hp;
    int alive;
    int direction;
    int def_index;
    int alerted;
    int last_seen_x;
    int last_seen_y;
} RuntimeEnemy;

typedef struct
{
    int level;
    int experience;
    int gold;
    int base_max_hp;
    int hp;
    int base_attack;
    int base_defense;
    int weapon_attack;
    int armor_defense;
    int armor_max_hp;
    char weapon[DATA_NAME_LEN];
    char armor[DATA_NAME_LEN];
    int inventory[DATA_MAX_ITEM_TYPES];
    time_t start_time;
} GameState;

static const int menu_y[MENU_COUNT] =
{
    MENU_ARROW_NEW_GAME_Y,
    MENU_ARROW_OPTIONS_Y,
    MENU_ARROW_EXIT_Y
};
static unsigned char selector_background[SELECTOR_HEIGHT][SELECTOR_WIDTH];
static const unsigned char selector_bitmap[SELECTOR_HEIGHT] =
{
    0x10, 0x18, 0xFC, 0xFE, 0xFC, 0x18, 0x10
};

void set_video_mode(int mode)
{
    dhero_mode(mode);
}

static void put_pixel(int x, int y, unsigned char color)
{
    if (x < 0 || y < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
        return;
    dhero_pixels_write(&color, 1, VGA_MEMORY + y * SCREEN_WIDTH + x);
}

static void fill_rect(int x, int y, int w, int h, unsigned char color)
{
    unsigned char row[SCREEN_WIDTH];
    int i;
    int yy;

    if (w <= 0 || h <= 0 || x < 0 || y < 0 ||
        x + w > SCREEN_WIDTH || y + h > SCREEN_HEIGHT)
        return;

    for (i = 0; i < w; i++) row[i] = color;
    for (yy = 0; yy < h; yy++)
        dhero_pixels_write(row, w, VGA_MEMORY + (y + yy) * SCREEN_WIDTH + x);
}

static void draw_box(int x, int y, int w, int h)
{
    int i;

    fill_rect(x, y, w, h, 0);
    for (i = x; i < x + w; i++)
    {
        put_pixel(i, y, 254);
        put_pixel(i, y + h - 1, 254);
    }
    for (i = y; i < y + h; i++)
    {
        put_pixel(x, i, 254);
        put_pixel(x + w - 1, i, 254);
    }
    for (i = x + 2; i < x + w - 2; i++)
    {
        put_pixel(i, y + 2, 252);
        put_pixel(i, y + h - 3, 252);
    }
}

static void bios_text(int row, int col, const char *text, unsigned char color)
{
    dhero_text(row, col, text, color);
}

static void bios_centered_text(int row, const char *text,
                               unsigned char color)
{
    int col;
    int len;

    len = (int)strlen(text);
    col = (40 - len) / 2;
    if (col < 0) col = 0;
    bios_text(row, col, text, color);
}

static const char *item_display_name(const char *name)
{
    static char key[LANG_KEY_LEN];
    snprintf(key, sizeof(key), "ITEM_%s", name);
    if (lang_has_english(key)) return lang_get(key);
    return name;
}

static const char *enemy_display_name(const char *name)
{
    static char key[LANG_KEY_LEN];
    snprintf(key, sizeof(key), "ENEMY_%s", name);
    if (lang_has_english(key)) return lang_get(key);
    return name;
}

static void save_selector_background(int center_y)
{
    int row;
    int top;

    top = center_y - (SELECTOR_HEIGHT / 2);
    for (row = 0; row < SELECTOR_HEIGHT; row++)
        dhero_pixels_read(VGA_MEMORY + (top + row) * SCREEN_WIDTH + SELECTOR_X,
                  SELECTOR_WIDTH, selector_background[row]);
}

static void restore_selector_background(int center_y)
{
    int row;
    int top;

    top = center_y - (SELECTOR_HEIGHT / 2);
    for (row = 0; row < SELECTOR_HEIGHT; row++)
        dhero_pixels_write(selector_background[row], SELECTOR_WIDTH,
                  VGA_MEMORY + (top + row) * SCREEN_WIDTH + SELECTOR_X);
}

static void draw_selector(int center_y)
{
    int row;
    int col;
    int top;
    unsigned char color;

    top = center_y - (SELECTOR_HEIGHT / 2);
    color = SELECTOR_COLOR;
    for (row = 0; row < SELECTOR_HEIGHT; row++)
        for (col = 0; col < SELECTOR_WIDTH; col++)
            if (selector_bitmap[row] & (0x80 >> col))
                dhero_pixels_write(&color, 1,
                          VGA_MEMORY + (top + row) * SCREEN_WIDTH +
                          SELECTOR_X + col);
}

static void move_selector(int old_selection, int new_selection)
{
    restore_selector_background(menu_y[old_selection]);
    save_selector_background(menu_y[new_selection]);
    draw_selector(menu_y[new_selection]);
}

static void draw_options_popup(int selected)
{
    char line[40];

    draw_box(36, 42, 248, 116);
    bios_centered_text(6, lang_get("OPT_TITLE"), 255);

    snprintf(line, sizeof(line), "%c %s: %s",
            selected == 0 ? '>' : ' ',
            lang_get("OPT_SOUNDS"),
            lang_get(sound_enabled ? "OPT_ON" : "OPT_OFF"));
    bios_centered_text(8, line, selected == 0 ? 255 : 253);

    snprintf(line, sizeof(line), "%c %s: %s",
            selected == 1 ? '>' : ' ',
            lang_get("OPT_LANGUAGE"),
            lang_get(ui_language ? "OPT_LANG_FIN" : "OPT_LANG_ENG"));
    bios_centered_text(10, line, selected == 1 ? 255 : 253);

    bios_centered_text(12, lang_get("OPT_EDITOR"), 254);
    bios_centered_text(15, lang_get("OPT_HELP"), 252);
}

static void run_options_popup(void)
{
    int selected;
    int key;
    int scan;

    selected = 0;
    draw_options_popup(selected);

    while (1)
    {
        key = dhero_getch();
        if (key == KEY_ESC) break;

        if (key == 0 || key == 0xE0)
        {
            scan = dhero_getch();
            if (scan == KEY_UP || scan == KEY_DOWN)
            {
                selected = 1 - selected;
                draw_options_popup(selected);
            }
            else if (scan == KEY_LEFT || scan == KEY_RIGHT)
            {
                if (selected == 0)
                {
                    sound_enabled = !sound_enabled;
                    if (!sound_enabled) speaker_stop();
                }
                else ui_language = !ui_language;
                draw_options_popup(selected);
            }
        }
        else if (key == KEY_ENTER)
        {
            if (selected == 0)
            {
                sound_enabled = !sound_enabled;
                if (!sound_enabled) speaker_stop();
            }
            else ui_language = !ui_language;
            draw_options_popup(selected);
        }
    }
}

static int run_menu(void)
{
    int selection;
    int old_selection;
    int key;

    selection = MENU_NEW_GAME;
    save_selector_background(menu_y[selection]);
    draw_selector(menu_y[selection]);

    while (1)
    {
        key = dhero_getch();
        if (key == 0 || key == 0xE0)
        {
            key = dhero_getch();
            if (key == KEY_UP)
            {
                old_selection = selection;
                selection--;
                if (selection < 0) selection = MENU_COUNT - 1;
                move_selector(old_selection, selection);
            }
            else if (key == KEY_DOWN)
            {
                old_selection = selection;
                selection++;
                if (selection >= MENU_COUNT) selection = 0;
                move_selector(old_selection, selection);
            }
        }
        else if (key == KEY_ENTER)
        {
            if (selection == MENU_OPTIONS)
            {
                restore_selector_background(menu_y[selection]);
                run_options_popup();
                load_pcx(MENU_IMAGE);
                save_selector_background(menu_y[selection]);
                draw_selector(menu_y[selection]);
            }
            else
            {
                restore_selector_background(menu_y[selection]);
                return selection;
            }
        }
        else if (key == KEY_ESC)
        {
            restore_selector_background(menu_y[selection]);
            return MENU_EXIT;
        }
    }
}

static int game_max_hp(const GameState *game)
{
    return game->base_max_hp + game->armor_max_hp;
}

static int game_attack(const GameState *game)
{
    return game->base_attack + game->weapon_attack;
}

static int game_defense(const GameState *game)
{
    return game->base_defense + game->armor_defense;
}

static int next_xp(const GameState *game)
{
    return game->level * 30;
}

static void level_up(GameState *game, char *message)
{
    int need;

    need = next_xp(game);
    while (game->experience >= need)
    {
        game->experience -= need;
        game->level++;
        game->base_max_hp += 2;
        game->base_attack++;
        game->hp = game_max_hp(game);
        need = next_xp(game);
        snprintf(message, 64, "%s", lang_get("MSG_LEVEL_UP"));
        sound_level_up();
    }
}

static void init_game(GameState *game)
{
    memset(game, 0, sizeof(GameState));
    game->level = 1;
    game->base_max_hp = 12;
    game->hp = 12;
    game->base_attack = 1;
    game->weapon_attack = 1;
    strcpy(game->weapon, "DAGGER");
    strcpy(game->armor, "NONE");
    game->start_time = time(NULL);
}

static void equip_or_consume_item(GameState *game, const ItemDefs *defs,
                                  int index, char *message)
{
    const ItemDef *item;
    int old_index;

    item = &defs->entries[index];

    if (strcmp(item->slot, "WEAPON") == 0)
    {
        old_index = data_find_item_def(defs, game->weapon);
        if (old_index >= 0) game->inventory[old_index]++;
        game->weapon_attack = item->attack;
        strcpy(game->weapon, item->name);
        snprintf(message, 64, lang_get("MSG_EQUIPPED"),
                item_display_name(item->name));
    }
    else if (strcmp(item->slot, "ARMOR") == 0)
    {
        old_index = data_find_item_def(defs, game->armor);
        if (old_index >= 0) game->inventory[old_index]++;
        game->armor_defense = item->defense;
        game->armor_max_hp = item->max_hp;
        strcpy(game->armor, item->name);
        if (game->hp > game_max_hp(game)) game->hp = game_max_hp(game);
        snprintf(message, 64, lang_get("MSG_EQUIPPED"),
                item_display_name(item->name));
    }
    else if (strcmp(item->slot, "CONSUMABLE") == 0)
    {
        game->hp += item->heal;
        if (game->hp > game_max_hp(game)) game->hp = game_max_hp(game);
        snprintf(message, 64, lang_get("MSG_USED"),
                item_display_name(item->name));
    }
}

static void receive_item(GameState *game, const ItemDefs *defs,
                         const char *item_name, char *message)
{
    int index;
    const ItemDef *item;

    if (strcmp(item_name, DATA_SLOT_NONE) == 0) return;
    index = data_find_item_def(defs, item_name);
    if (index < 0)
    {
        snprintf(message, 64, "%s", lang_get("MSG_UNKNOWN_ITEM"));
        return;
    }

    item = &defs->entries[index];
    if (strcmp(item->slot, "NONE") == 0 && item->gold != 0)
    {
        game->gold += item->gold;
        snprintf(message, 64, lang_get("MSG_GOLD_PLUS"), item->gold);
        return;
    }

    game->inventory[index]++;
    snprintf(message, 64, lang_get("MSG_STORED"), item_display_name(item_name));
}

static int inventory_type_count(const GameState *game, const ItemDefs *defs)
{
    int i;
    int count;

    count = 0;
    for (i = 0; i < defs->count; i++)
        if (game->inventory[i] > 0) count++;
    return count;
}

static int inventory_nth_index(const GameState *game, const ItemDefs *defs,
                               int nth)
{
    int i;
    int count;

    count = 0;
    for (i = 0; i < defs->count; i++)
    {
        if (game->inventory[i] <= 0) continue;
        if (count == nth) return i;
        count++;
    }
    return -1;
}

static void draw_inventory_popup(const GameState *game,
                                 const ItemDefs *defs,
                                 int selected)
{
    int total;
    int first;
    int row;
    int nth;
    int index;
    char line[40];
    const ItemDef *item;

    draw_box(20, 28, 280, 128);
    bios_text(4, 15, lang_get("INV_TITLE"), 255);

    total = inventory_type_count(game, defs);
    if (total == 0)
    {
        bios_text(8, 11, lang_get("INV_EMPTY"), 253);
        bios_text(17, 7, lang_get("INV_CLOSE"), 252);
        return;
    }

    first = selected - INVENTORY_ROWS / 2;
    if (first < 0) first = 0;
    if (first > total - INVENTORY_ROWS) first = total - INVENTORY_ROWS;
    if (first < 0) first = 0;

    for (row = 0; row < INVENTORY_ROWS; row++)
    {
        nth = first + row;
        if (nth >= total) break;
        index = inventory_nth_index(game, defs, nth);
        if (index < 0) continue;
        snprintf(line, sizeof(line), "%c %-18s x%d",
                nth == selected ? '>' : ' ',
                item_display_name(defs->entries[index].name),
                game->inventory[index]);
        bios_text(6 + row, 5, line, nth == selected ? 255 : 253);
    }

    index = inventory_nth_index(game, defs, selected);
    if (index >= 0)
    {
        item = &defs->entries[index];
        if (strcmp(item->slot, "WEAPON") == 0)
            snprintf(line, sizeof(line), lang_get("INV_WEAPON"), item->attack);
        else if (strcmp(item->slot, "ARMOR") == 0)
            snprintf(line, sizeof(line), lang_get("INV_ARMOR"),
                    item->defense, item->max_hp);
        else if (strcmp(item->slot, "CONSUMABLE") == 0)
            snprintf(line, sizeof(line), lang_get("INV_HEAL"), item->heal);
        else strcpy(line, "");
        bios_text(15, 5, line, 254);
    }

    bios_text(17, 4, lang_get("INV_HELP"), 252);
}

static int inventory_popup(GameState *game, const ItemDefs *defs,
                           char *message)
{
    int total;
    int selected;
    int key;
    int scan;
    int index;
    const ItemDef *item;

    total = inventory_type_count(game, defs);
    selected = 0;
    draw_inventory_popup(game, defs, selected);

    while (1)
    {
        key = dhero_getch();
        if (key == KEY_SPACE || key == KEY_ESC) return 0;

        if (key == 0 || key == 0xE0)
        {
            scan = dhero_getch();
            if (total > 0 && scan == KEY_UP)
            {
                selected--;
                if (selected < 0) selected = total - 1;
                draw_inventory_popup(game, defs, selected);
            }
            else if (total > 0 && scan == KEY_DOWN)
            {
                selected++;
                if (selected >= total) selected = 0;
                draw_inventory_popup(game, defs, selected);
            }
        }
        else if (key == KEY_ENTER && total > 0)
        {
            index = inventory_nth_index(game, defs, selected);
            if (index < 0) continue;
            item = &defs->entries[index];

            if (strcmp(item->slot, "CONSUMABLE") == 0 &&
                game->hp >= game_max_hp(game))
            {
                snprintf(message, 64, "%s", lang_get("MSG_HP_FULL"));
                draw_inventory_popup(game, defs, selected);
                continue;
            }

            if (strcmp(item->slot, "WEAPON") != 0 &&
                strcmp(item->slot, "ARMOR") != 0 &&
                strcmp(item->slot, "CONSUMABLE") != 0)
            {
                snprintf(message, 64, "%s", lang_get("MSG_CANNOT_USE"));
                draw_inventory_popup(game, defs, selected);
                continue;
            }

            game->inventory[index]--;
            equip_or_consume_item(game, defs, index, message);
            sound_interact();
            return 1;
        }
    }
}

static int direction_from_token(const char *text)
{
    int len;

    if (strcmp(text, "UP") == 0) return DIR_UP;
    if (strcmp(text, "DOWN") == 0) return DIR_DOWN;
    if (strcmp(text, "LEFT") == 0) return DIR_LEFT;
    if (strcmp(text, "RIGHT") == 0) return DIR_RIGHT;

    len = (int)strlen(text);
    if (len >= 3 && strcmp(text + len - 3, "_UP") == 0) return DIR_UP;
    if (len >= 5 && strcmp(text + len - 5, "_DOWN") == 0) return DIR_DOWN;
    if (len >= 5 && strcmp(text + len - 5, "_LEFT") == 0) return DIR_LEFT;
    return DIR_RIGHT;
}

static int load_sheet_pair(const char *folder, const char *name,
                           PCXImage *image, SheetSpec *spec)
{
    char pcx_path[DATA_PATH_LEN];
    char dat_path[DATA_PATH_LEN];

    snprintf(pcx_path, sizeof(pcx_path), "DATA/GFX/%s/%s.pcx", folder, name);
    snprintf(dat_path, sizeof(dat_path), "DATA/GFX/%s/%s.DAT", folder, name);
    if (!data_load_sheet(dat_path, spec)) return 0;
    if (strcmp(folder, "SPRITES") == 0) {
        snprintf(pcx_path, sizeof(pcx_path), "DATA/GFX/%s/%s.PCX", folder, name);
    }
    if (!pcx_load_image(pcx_path, image)) return 0;
    return 1;
}

static int draw_sheet_frame(const PCXImage *image, const SheetSpec *spec,
                            const char *name, int dest_x, int dest_y,
                            int transparent_index)
{
    int index;
    int sx;
    int sy;

    index = data_find_sheet_entry(spec, name);
    if (index < 0) return 0;
    sx = spec->entries[index].column * spec->cell_width;
    sy = spec->entries[index].row * spec->cell_height;
    pcx_blit_region(image, sx, sy,
                    spec->cell_width, spec->cell_height,
                    dest_x, dest_y, transparent_index);
    return 1;
}

static void draw_direction_marker(int x, int y, int direction,
                                  int cell_w, int cell_h)
{
    unsigned char color;
    int cx;
    int cy;

    color = 255;
    cx = x + cell_w / 2;
    cy = y + cell_h / 2;

    if (direction == DIR_UP)
    {
        put_pixel(cx, y, color);
        put_pixel(cx - 1, y + 1, color);
        put_pixel(cx + 1, y + 1, color);
    }
    else if (direction == DIR_DOWN)
    {
        put_pixel(cx, y + cell_h - 1, color);
        put_pixel(cx - 1, y + cell_h - 2, color);
        put_pixel(cx + 1, y + cell_h - 2, color);
    }
    else if (direction == DIR_LEFT)
    {
        put_pixel(x, cy, color);
        put_pixel(x + 1, cy - 1, color);
        put_pixel(x + 1, cy + 1, color);
    }
    else
    {
        put_pixel(x + cell_w - 1, cy, color);
        put_pixel(x + cell_w - 2, cy - 1, color);
        put_pixel(x + cell_w - 2, cy + 1, color);
    }
}

static int enemy_at(RuntimeEnemy *enemies, int count, int x, int y)
{
    int i;
    for (i = 0; i < count; i++)
        if (enemies[i].alive && enemies[i].x == x && enemies[i].y == y)
            return i;
    return -1;
}

static int living_enemies(RuntimeEnemy *enemies, int count)
{
    int i;
    int total;

    total = 0;
    for (i = 0; i < count; i++)
        if (enemies[i].alive) total++;
    return total;
}

static const char *tile_name_at(const LevelData *level, int x, int y)
{
    int index;

    if (x < 0 || y < 0 || x >= level->width || y >= level->height)
        return NULL;
    index = level->cells[y][x];
    if (index < 0 || index >= level->legend_count) return NULL;
    return level->legend[index].tile;
}

static int tile_blocks_sight(const LevelData *level, int x, int y)
{
    const char *name;

    name = tile_name_at(level, x, y);
    if (name == NULL) return 1;
    if (data_level_is_walkable(level, x, y)) return 0;
    if (strcmp(name, "CHEST") == 0) return 0;
    if (strcmp(name, "DOOR_OPEN") == 0) return 0;
    return 1;
}

static int sight_ray_clear(const LevelData *level,
                           int source_x, int source_y,
                           int target_x, int target_y,
                           int target_offset_x, int target_offset_y)
{
    int x0;
    int y0;
    int x1;
    int y1;
    int x;
    int y;
    int old_x;
    int old_y;
    int dx;
    int dy;
    int step_x;
    int step_y;
    int err;
    int e2;
    int cell_x;
    int cell_y;
    int old_cell_x;
    int old_cell_y;

    x0 = source_x * LOS_SCALE + LOS_CENTER;
    y0 = source_y * LOS_SCALE + LOS_CENTER;
    x1 = target_x * LOS_SCALE + target_offset_x;
    y1 = target_y * LOS_SCALE + target_offset_y;
    x = x0;
    y = y0;
    dx = abs(x1 - x0);
    dy = abs(y1 - y0);
    step_x = (x0 < x1) ? 1 : -1;
    step_y = (y0 < y1) ? 1 : -1;
    err = dx - dy;

    while (!(x == x1 && y == y1))
    {
        old_x = x;
        old_y = y;
        e2 = err * 2;
        if (e2 > -dy)
        {
            err -= dy;
            x += step_x;
        }
        if (e2 < dx)
        {
            err += dx;
            y += step_y;
        }

        old_cell_x = old_x / LOS_SCALE;
        old_cell_y = old_y / LOS_SCALE;
        cell_x = x / LOS_SCALE;
        cell_y = y / LOS_SCALE;

        if (cell_x != old_cell_x && cell_y != old_cell_y &&
            tile_blocks_sight(level, cell_x, old_cell_y) &&
            tile_blocks_sight(level, old_cell_x, cell_y))
        {
            if (cell_x == target_x && cell_y == target_y &&
                tile_blocks_sight(level, target_x, target_y))
                return 1;
            return 0;
        }

        if (cell_x == target_x && cell_y == target_y) return 1;
        if ((cell_x != source_x || cell_y != source_y) &&
            tile_blocks_sight(level, cell_x, cell_y)) return 0;
    }
    return 1;
}

static int line_of_sight(const LevelData *level,
                         int x0, int y0, int x1, int y1)
{
    static const int sample_x[5] =
    {
        LOS_CENTER, LOS_INSET, LOS_SCALE - LOS_INSET,
        LOS_INSET, LOS_SCALE - LOS_INSET
    };
    static const int sample_y[5] =
    {
        LOS_CENTER, LOS_INSET, LOS_INSET,
        LOS_SCALE - LOS_INSET, LOS_SCALE - LOS_INSET
    };
    int i;

    if (x0 == x1 && y0 == y1) return 1;
    for (i = 0; i < 5; i++)
        if (sight_ray_clear(level, x0, y0, x1, y1,
                            sample_x[i], sample_y[i])) return 1;
    return 0;
}

static void update_visibility(const LevelData *level,
                              int hero_x, int hero_y,
                              unsigned char visible[DATA_MAX_MAP_HEIGHT][DATA_MAX_MAP_WIDTH])
{
    int x;
    int y;
    int nx;
    int ny;
    int reveal;

    memset(visible, 0,
           DATA_MAX_MAP_HEIGHT * DATA_MAX_MAP_WIDTH * sizeof(unsigned char));

    for (y = 0; y < level->height; y++)
        for (x = 0; x < level->width; x++)
            if (line_of_sight(level, hero_x, hero_y, x, y))
                visible[y][x] = 1;

    for (y = 0; y < level->height; y++)
    {
        for (x = 0; x < level->width; x++)
        {
            if (visible[y][x] || !tile_blocks_sight(level, x, y)) continue;
            reveal = 0;
            for (ny = y - 1; ny <= y + 1 && !reveal; ny++)
            {
                for (nx = x - 1; nx <= x + 1; nx++)
                {
                    if (nx < 0 || ny < 0 ||
                        nx >= level->width || ny >= level->height) continue;
                    if (visible[ny][nx] &&
                        !tile_blocks_sight(level, nx, ny))
                    {
                        reveal = 1;
                        break;
                    }
                }
            }
            if (reveal) visible[y][x] = 1;
        }
    }
}

static void update_enemy_discovery(RuntimeEnemy *enemies,
                                   int enemy_count,
                                   unsigned char visible[DATA_MAX_MAP_HEIGHT][DATA_MAX_MAP_WIDTH],
                                   unsigned char *was_visible)
{
    int i;
    int now_visible;
    int discovered;

    discovered = 0;
    for (i = 0; i < enemy_count; i++)
    {
        now_visible = enemies[i].alive &&
                      visible[enemies[i].y][enemies[i].x];
        if (now_visible && !was_visible[i]) discovered = 1;
        was_visible[i] = (unsigned char)now_visible;
    }
    if (discovered) sound_enemy_discovery();
}

static void camera_for(const LevelData *level, int hero_x, int hero_y,
                       int *camera_x, int *camera_y)
{
    int max_x;
    int max_y;

    *camera_x = hero_x - VIEW_COLS / 2;
    *camera_y = hero_y - VIEW_ROWS / 2;
    max_x = level->width - VIEW_COLS;
    max_y = level->height - VIEW_ROWS;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (*camera_x < 0) *camera_x = 0;
    if (*camera_y < 0) *camera_y = 0;
    if (*camera_x > max_x) *camera_x = max_x;
    if (*camera_y > max_y) *camera_y = max_y;
}

static void draw_level(const LevelData *level,
                       const PCXImage *tiles,
                       const SheetSpec *tile_spec,
                       unsigned char visible[DATA_MAX_MAP_HEIGHT][DATA_MAX_MAP_WIDTH],
                       int camera_x, int camera_y)
{
    int vx;
    int vy;
    int mx;
    int my;
    int legend_index;
    const char *tile_name;

    fill_rect(0, 0, SCREEN_WIDTH, HUD_Y, 0);
    for (vy = 0; vy < VIEW_ROWS; vy++)
    {
        for (vx = 0; vx < VIEW_COLS; vx++)
        {
            mx = camera_x + vx;
            my = camera_y + vy;
            if (mx >= level->width || my >= level->height) continue;
            if (!visible[my][mx]) continue;
            legend_index = level->cells[my][mx];
            tile_name = level->legend[legend_index].tile;
            draw_sheet_frame(tiles, tile_spec, tile_name,
                             vx * tile_spec->cell_width,
                             vy * tile_spec->cell_height, -1);
        }
    }
}

static void draw_actors(const PCXImage *characters,
                        const SheetSpec *char_spec,
                        int transparent_index,
                        RuntimeEnemy *enemies,
                        int enemy_count,
                        const EnemyDefs *enemy_defs,
                        unsigned char visible[DATA_MAX_MAP_HEIGHT][DATA_MAX_MAP_WIDTH],
                        int hero_x, int hero_y, int hero_dir,
                        int camera_x, int camera_y)
{
    int i;
    int sx;
    int sy;
    int px;
    int py;
    const char *name;

    for (i = 0; i < enemy_count; i++)
    {
        if (!enemies[i].alive || !visible[enemies[i].y][enemies[i].x])
            continue;
        sx = enemies[i].x - camera_x;
        sy = enemies[i].y - camera_y;
        if (sx < 0 || sy < 0 || sx >= VIEW_COLS || sy >= VIEW_ROWS) continue;
        name = enemy_defs->entries[enemies[i].def_index].name;
        px = sx * char_spec->cell_width;
        py = sy * char_spec->cell_height;
        draw_sheet_frame(characters, char_spec, name,
                         px, py, transparent_index);
        draw_direction_marker(px, py, enemies[i].direction,
                              char_spec->cell_width,
                              char_spec->cell_height);
    }

    sx = hero_x - camera_x;
    sy = hero_y - camera_y;
    px = sx * char_spec->cell_width;
    py = sy * char_spec->cell_height;
    draw_sheet_frame(characters, char_spec, "HERO",
                     px, py, transparent_index);
    draw_direction_marker(px, py, hero_dir,
                          char_spec->cell_width,
                          char_spec->cell_height);
}

static void draw_hud(const GameState *game, const char *level_name,
                     int enemies_left, const char *message)
{
    char line[64];
    const char *display_level;

    fill_rect(0, HUD_Y, SCREEN_WIDTH, SCREEN_HEIGHT - HUD_Y, 0);
    display_level = level_name;
    if (ui_language && strncmp(level_name, "LEVEL", 5) == 0)
        display_level = level_name + 5;

    if (ui_language)
        snprintf(line, sizeof(line), "T%s HP:%d/%d %s:%d XP:%d/%d",
                display_level, game->hp, game_max_hp(game),
                lang_get("HUD_LEVEL"), game->level,
                game->experience, next_xp(game));
    else
        snprintf(line, sizeof(line), "%s HP:%d/%d %s:%d XP:%d/%d",
                display_level, game->hp, game_max_hp(game),
                lang_get("HUD_LEVEL"), game->level,
                game->experience, next_xp(game));
    bios_text(20, 0, line, 255);

    snprintf(line, sizeof(line), "%s:%d %s:%s",
            lang_get("HUD_GOLD"), game->gold,
            lang_get("HUD_WEAPON"), item_display_name(game->weapon));
    bios_text(21, 0, line, 254);

    snprintf(line, sizeof(line), "%s:%s %s:%d",
            lang_get("HUD_ARMOR"), item_display_name(game->armor),
            lang_get("HUD_FOES"), enemies_left);
    bios_text(22, 0, line, 254);
    bios_text(23, 0, message, 255);
    bios_text(24, 0, lang_get("HUD_CONTROLS"), 253);
}

static void redraw(const LevelData *level,
                   const PCXImage *tiles,
                   const SheetSpec *tile_spec,
                   const PCXImage *characters,
                   const SheetSpec *char_spec,
                   int transparent_index,
                   RuntimeEnemy *enemies,
                   int enemy_count,
                   const EnemyDefs *enemy_defs,
                   const GameState *game,
                   unsigned char visible[DATA_MAX_MAP_HEIGHT][DATA_MAX_MAP_WIDTH],
                   int hero_x, int hero_y, int hero_dir,
                   const char *message)
{
    int camera_x;
    int camera_y;

    camera_for(level, hero_x, hero_y, &camera_x, &camera_y);
    draw_level(level, tiles, tile_spec, visible, camera_x, camera_y);
    draw_actors(characters, char_spec, transparent_index,
                enemies, enemy_count, enemy_defs, visible,
                hero_x, hero_y, hero_dir, camera_x, camera_y);
    draw_hud(game, level->name,
             living_enemies(enemies, enemy_count), message);
}

static void direction_target(int x, int y, int direction, int *tx, int *ty)
{
    *tx = x;
    *ty = y;
    if (direction == DIR_UP) (*ty)--;
    else if (direction == DIR_DOWN) (*ty)++;
    else if (direction == DIR_LEFT) (*tx)--;
    else (*tx)++;
}

static void attack_enemy(GameState *game, RuntimeEnemy *enemy,
                         const EnemyDef *def, char *message)
{
    int damage;
    int xp;

    sound_battle();
    damage = combat_roll_dialog(lang_get("COMBAT_HERO"),
                                game_attack(game),
                                enemy_display_name(def->name),
                                def->defense);

    if (damage <= 0)
    {
        snprintf(message, 64, lang_get("MSG_MISS"),
                enemy_display_name(def->name));
        return;
    }

    enemy->hp -= damage;

    if (enemy->hp <= 0)
    {
        enemy->alive = 0;
        game->gold += def->gold;
        xp = def->hp + def->attack + def->defense + def->gold / 2;
        game->experience += xp;
        snprintf(message, 64, lang_get("MSG_ENEMY_DOWN"),
                enemy_display_name(def->name), def->gold, xp);
        sound_enemy_killed();
        level_up(game, message);
    }
    else
    {
        snprintf(message, 64, lang_get("MSG_HIT"),
                enemy_display_name(def->name), damage);
    }
}

static int enemy_can_move(const LevelData *level,
                          RuntimeEnemy *enemies, int enemy_count,
                          int hero_x, int hero_y, int self_index,
                          int x, int y)
{
    int occupant;

    if (!data_level_is_walkable(level, x, y)) return 0;
    if (x == hero_x && y == hero_y) return 0;
    occupant = enemy_at(enemies, enemy_count, x, y);
    if (occupant >= 0 && occupant != self_index) return 0;
    return 1;
}

static void set_enemy_direction(RuntimeEnemy *enemy, int dx, int dy)
{
    if (abs(dx) >= abs(dy) && dx != 0)
        enemy->direction = (dx > 0) ? DIR_RIGHT : DIR_LEFT;
    else if (dy != 0)
        enemy->direction = (dy > 0) ? DIR_DOWN : DIR_UP;
}

static void move_enemy_toward(LevelData *level,
                              RuntimeEnemy *enemies, int enemy_count,
                              int index, int hero_x, int hero_y,
                              int target_x, int target_y)
{
    int dx;
    int dy;
    int nx;
    int ny;

    dx = target_x - enemies[index].x;
    dy = target_y - enemies[index].y;
    nx = enemies[index].x;
    ny = enemies[index].y;

    if (abs(dx) >= abs(dy) && dx != 0)
    {
        nx += (dx > 0) ? 1 : -1;
        set_enemy_direction(&enemies[index], dx, 0);
        if (!enemy_can_move(level, enemies, enemy_count,
                            hero_x, hero_y, index, nx, ny))
        {
            nx = enemies[index].x;
            ny = enemies[index].y + ((dy > 0) ? 1 : -1);
            if (dy == 0 ||
                !enemy_can_move(level, enemies, enemy_count,
                                hero_x, hero_y, index, nx, ny))
            {
                nx = enemies[index].x;
                ny = enemies[index].y;
            }
            else set_enemy_direction(&enemies[index], 0, dy);
        }
    }
    else if (dy != 0)
    {
        ny += (dy > 0) ? 1 : -1;
        set_enemy_direction(&enemies[index], 0, dy);
        if (!enemy_can_move(level, enemies, enemy_count,
                            hero_x, hero_y, index, nx, ny))
        {
            nx = enemies[index].x + ((dx > 0) ? 1 : -1);
            ny = enemies[index].y;
            if (dx == 0 ||
                !enemy_can_move(level, enemies, enemy_count,
                                hero_x, hero_y, index, nx, ny))
            {
                nx = enemies[index].x;
                ny = enemies[index].y;
            }
            else set_enemy_direction(&enemies[index], dx, 0);
        }
    }

    enemies[index].x = nx;
    enemies[index].y = ny;
}

static void wander_enemy(LevelData *level,
                         RuntimeEnemy *enemies, int enemy_count,
                         int index, int hero_x, int hero_y)
{
    int choice;
    int nx;
    int ny;
    int direction;
    int tries;

    for (tries = 0; tries < 4; tries++)
    {
        choice = rand() % 5;
        if (choice == 4) return;
        nx = enemies[index].x;
        ny = enemies[index].y;
        direction = choice;
        if (direction == DIR_UP) ny--;
        else if (direction == DIR_DOWN) ny++;
        else if (direction == DIR_LEFT) nx--;
        else nx++;
        enemies[index].direction = direction;
        if (enemy_can_move(level, enemies, enemy_count,
                           hero_x, hero_y, index, nx, ny))
        {
            enemies[index].x = nx;
            enemies[index].y = ny;
            return;
        }
    }
}

static void enemy_turn(LevelData *level,
                       GameState *game,
                       RuntimeEnemy *enemies, int enemy_count,
                       const EnemyDefs *defs,
                       int hero_x, int hero_y,
                       char *message)
{
    int i;
    int dx;
    int dy;
    int damage;
    int sees_hero;
    int old_x;
    int old_y;
    int moved_any;
    const EnemyDef *def;

    moved_any = 0;
    for (i = 0; i < enemy_count && game->hp > 0; i++)
    {
        if (!enemies[i].alive) continue;
        old_x = enemies[i].x;
        old_y = enemies[i].y;
        def = &defs->entries[enemies[i].def_index];
        dx = hero_x - enemies[i].x;
        dy = hero_y - enemies[i].y;
        sees_hero = line_of_sight(level,
                                  enemies[i].x, enemies[i].y,
                                  hero_x, hero_y);

        if (sees_hero)
        {
            enemies[i].alerted = 1;
            enemies[i].last_seen_x = hero_x;
            enemies[i].last_seen_y = hero_y;
        }

        if ((abs(dx) + abs(dy)) == 1 && sees_hero)
        {
            set_enemy_direction(&enemies[i], dx, dy);
            sound_battle();
            damage = combat_roll_dialog(enemy_display_name(def->name),
                                        def->attack,
                                        lang_get("COMBAT_HERO"),
                                        game_defense(game));

            if (damage > 0)
            {
                game->hp -= damage;
                if (game->hp < 0) game->hp = 0;
                snprintf(message, 64, lang_get("MSG_ENEMY_HIT"),
                        enemy_display_name(def->name), damage);
                sound_damage();
            }
            else
            {
                snprintf(message, 64, lang_get("MSG_ENEMY_MISS"),
                        enemy_display_name(def->name));
            }
            continue;
        }

        if (sees_hero)
            move_enemy_toward(level, enemies, enemy_count, i,
                              hero_x, hero_y, hero_x, hero_y);
        else if (enemies[i].alerted)
        {
            if (enemies[i].x == enemies[i].last_seen_x &&
                enemies[i].y == enemies[i].last_seen_y)
            {
                enemies[i].alerted = 0;
                wander_enemy(level, enemies, enemy_count, i, hero_x, hero_y);
            }
            else
                move_enemy_toward(level, enemies, enemy_count, i,
                                  hero_x, hero_y,
                                  enemies[i].last_seen_x,
                                  enemies[i].last_seen_y);
        }
        else wander_enemy(level, enemies, enemy_count, i, hero_x, hero_y);

        if (enemies[i].x != old_x || enemies[i].y != old_y) moved_any = 1;
    }
    if (moved_any) sound_move_enemy();
}

static int interact_front(LevelData *level,
                          GameState *game,
                          const ItemDefs *items,
                          RuntimeEnemy *enemies, int enemy_count,
                          const EnemyDefs *enemy_defs,
                          int hero_x, int hero_y, int direction,
                          char *message)
{
    int tx;
    int ty;
    int i;
    int enemy_index;

    direction_target(hero_x, hero_y, direction, &tx, &ty);
    enemy_index = enemy_at(enemies, enemy_count, tx, ty);
    if (enemy_index >= 0)
    {
        attack_enemy(game, &enemies[enemy_index],
                     &enemy_defs->entries[enemies[enemy_index].def_index],
                     message);
        return 1;
    }

    for (i = 0; i < level->door_count; i++)
    {
        if (!level->doors[i].opened &&
            level->doors[i].x == tx && level->doors[i].y == ty)
        {
            if (data_level_set_tile(level, tx, ty, level->doors[i].open_tile))
            {
                level->doors[i].opened = 1;
                snprintf(message, 64, "%s", lang_get("MSG_DOOR_OPEN"));
                sound_interact();
                return 1;
            }
        }
    }

    for (i = 0; i < level->chest_count; i++)
    {
        if (!level->chests[i].opened &&
            level->chests[i].x == tx && level->chests[i].y == ty)
        {
            if (data_level_set_tile(level, tx, ty,
                                    level->chests[i].replacement_tile))
            {
                level->chests[i].opened = 1;
                game->gold += level->chests[i].gold;
                if (strcmp(level->chests[i].item, DATA_SLOT_NONE) != 0)
                    receive_item(game, items, level->chests[i].item, message);
                else
                    snprintf(message, 64, lang_get("MSG_CHEST_GOLD"),
                            level->chests[i].gold);
                sound_interact();
                return 1;
            }
        }
    }

    snprintf(message, 64, "%s", lang_get("MSG_NOTHING"));
    return 0;
}

static void collect_items(LevelData *level,
                          GameState *game,
                          const ItemDefs *items,
                          int hero_x, int hero_y,
                          char *message)
{
    int i;

    for (i = 0; i < level->item_count; i++)
    {
        if (!level->items[i].taken &&
            level->items[i].x == hero_x && level->items[i].y == hero_y)
        {
            if (data_level_set_tile(level, hero_x, hero_y,
                                    level->items[i].replacement_tile))
            {
                level->items[i].taken = 1;
                receive_item(game, items, level->items[i].item, message);
            }
        }
    }
}

static int exit_at(const LevelData *level,
                   int hero_x, int hero_y,
                   char *next_level)
{
    int i;
    for (i = 0; i < level->exit_count; i++)
    {
        if (level->exits[i].x == hero_x && level->exits[i].y == hero_y)
        {
            strcpy(next_level, level->exits[i].next_level);
            return 1;
        }
    }
    return 0;
}

static int init_runtime_enemies(const LevelData *level,
                                const EnemyDefs *defs,
                                RuntimeEnemy *runtime)
{
    int i;
    int index;

    for (i = 0; i < level->enemy_count; i++)
    {
        index = data_find_enemy_def(defs, level->enemies[i].type);
        if (index < 0) return 0;
        runtime[i].x = level->enemies[i].x;
        runtime[i].y = level->enemies[i].y;
        runtime[i].hp = defs->entries[index].hp;
        runtime[i].alive = 1;
        runtime[i].direction = direction_from_token(level->enemies[i].direction);
        runtime[i].def_index = index;
        runtime[i].alerted = 0;
        runtime[i].last_seen_x = runtime[i].x;
        runtime[i].last_seen_y = runtime[i].y;
    }
    return 1;
}

static int validate_level(const LevelData *level,
                          const SheetSpec *tile_spec,
                          const SheetSpec *char_spec,
                          const EnemyDefs *enemy_defs,
                          const ItemDefs *item_defs)
{
    int i;
    int enemy_index;

    if (tile_spec->cell_width != 16 || tile_spec->cell_height != 16 ||
        char_spec->cell_width != 16 || char_spec->cell_height != 16) return 0;

    for (i = 0; i < level->legend_count; i++)
        if (data_find_sheet_entry(tile_spec, level->legend[i].tile) < 0)
            return 0;
    if (data_find_sheet_entry(char_spec, "HERO") < 0) return 0;

    for (i = 0; i < level->enemy_count; i++)
    {
        enemy_index = data_find_enemy_def(enemy_defs, level->enemies[i].type);
        if (enemy_index < 0) return 0;
        if (data_find_sheet_entry(char_spec,
                                  enemy_defs->entries[enemy_index].name) < 0)
            return 0;
    }

    for (i = 0; i < level->chest_count; i++)
        if (strcmp(level->chests[i].item, DATA_SLOT_NONE) != 0 &&
            data_find_item_def(item_defs, level->chests[i].item) < 0) return 0;
    for (i = 0; i < level->item_count; i++)
        if (strcmp(level->items[i].item, DATA_SLOT_NONE) != 0 &&
            data_find_item_def(item_defs, level->items[i].item) < 0) return 0;
    return 1;
}

static int run_level(const char *level_name,
                     GameState *game,
                     const EnemyDefs *enemy_defs,
                     const ItemDefs *item_defs,
                     PCXImage *characters,
                     const SheetSpec *char_spec,
                     char *next_level)
{
    char level_path[DATA_PATH_LEN];
    LevelData level;
    PCXImage tiles;
    SheetSpec tile_spec;
    RuntimeEnemy enemies[MAX_RUNTIME_ENEMIES];
    unsigned char visible[DATA_MAX_MAP_HEIGHT][DATA_MAX_MAP_WIDTH];
    unsigned char enemy_was_visible[MAX_RUNTIME_ENEMIES];
    int hero_x;
    int hero_y;
    int hero_dir;
    int transparent_index;
    int key;
    int scan;
    int nx;
    int ny;
    int enemy_index;
    int action;
    char message[64];

    snprintf(level_path, sizeof(level_path), "DATA/GFX/MAP/%s.DAT", level_name);
    if (!data_load_level(level_path, &level)) return -1;
    if (!load_sheet_pair("TILES", level.tile_sheet, &tiles, &tile_spec)) return -1;

    pcx_remap_to_palette(characters, tiles.palette);
    transparent_index = char_spec->transparent_top_left ?
                        characters->pixels[0] : -1;

    if (!validate_level(&level, &tile_spec, char_spec,
                        enemy_defs, item_defs) ||
        !init_runtime_enemies(&level, enemy_defs, enemies))
    {
        pcx_free_image(&tiles);
        return -1;
    }

    memset(enemy_was_visible, 0, sizeof(enemy_was_visible));
    hero_x = level.start_x;
    hero_y = level.start_y;
    hero_dir = direction_from_token(level.start_frame);
    update_visibility(&level, hero_x, hero_y, visible);

    snprintf(message, 64, "%s", lang_get("MSG_START"));
    set_video_mode(0x13);
    pcx_apply_palette(&tiles);
    redraw(&level, &tiles, &tile_spec,
           characters, char_spec, transparent_index,
           enemies, level.enemy_count, enemy_defs, game,
           visible, hero_x, hero_y, hero_dir, message);
    update_enemy_discovery(enemies, level.enemy_count,
                           visible, enemy_was_visible);

    while (1)
    {
        key = dhero_getch();
        action = 0;

        if (key == KEY_ESC)
        {
            speaker_stop();
            pcx_free_image(&tiles);
            return -3;
        }

        if (key == KEY_SPACE)
        {
            action = inventory_popup(game, item_defs, message);
            if (!action)
            {
                redraw(&level, &tiles, &tile_spec,
                       characters, char_spec, transparent_index,
                       enemies, level.enemy_count, enemy_defs, game,
                       visible, hero_x, hero_y, hero_dir, message);
                continue;
            }
        }
        else if (key == KEY_ENTER)
        {
            action = interact_front(&level, game, item_defs,
                                    enemies, level.enemy_count, enemy_defs,
                                    hero_x, hero_y, hero_dir, message);
        }
        else if (key == 0 || key == 0xE0)
        {
            scan = dhero_getch();
            nx = hero_x;
            ny = hero_y;
            if (scan == KEY_UP) { hero_dir = DIR_UP; ny--; }
            else if (scan == KEY_DOWN) { hero_dir = DIR_DOWN; ny++; }
            else if (scan == KEY_LEFT) { hero_dir = DIR_LEFT; nx--; }
            else if (scan == KEY_RIGHT) { hero_dir = DIR_RIGHT; nx++; }
            else continue;

            enemy_index = enemy_at(enemies, level.enemy_count, nx, ny);
            if (enemy_index >= 0)
            {
                attack_enemy(game, &enemies[enemy_index],
                             &enemy_defs->entries[enemies[enemy_index].def_index],
                             message);
                action = 1;
            }
            else if (data_level_is_walkable(&level, nx, ny))
            {
                hero_x = nx;
                hero_y = ny;
                sound_move_player();
                snprintf(message, 64, "%s", lang_get("MSG_EXPLORING"));
                collect_items(&level, game, item_defs,
                              hero_x, hero_y, message);
                action = 1;
            }
            else snprintf(message, 64, "%s", lang_get("MSG_BLOCKED"));
        }

        if (action && game->hp > 0)
            enemy_turn(&level, game, enemies, level.enemy_count,
                       enemy_defs, hero_x, hero_y, message);

        if (game->hp <= 0) break;

        update_visibility(&level, hero_x, hero_y, visible);
        update_enemy_discovery(enemies, level.enemy_count,
                               visible, enemy_was_visible);

        if (exit_at(&level, hero_x, hero_y, next_level))
        {
            sound_finish();
            pcx_free_image(&tiles);
            return 1;
        }

        redraw(&level, &tiles, &tile_spec,
               characters, char_spec, transparent_index,
               enemies, level.enemy_count, enemy_defs, game,
               visible, hero_x, hero_y, hero_dir, message);
    }

    speaker_stop();
    pcx_free_image(&tiles);
    return -2;
}

static long final_score(const GameState *game, long seconds)
{
    long time_bonus;

    time_bonus = 7200L - seconds;
    if (time_bonus < 0) time_bonus = 0;
    return (long)game->gold * 100L + time_bonus * 5L;
}

static void finish_scored_run(const GameState *game, int victory)
{
    long seconds;
    long score;

    seconds = (long)(time(NULL) - game->start_time);
    if (seconds < 0) seconds = 0;
    score = final_score(game, seconds);

    scoreboard_end_run(score, seconds, game->gold, game->level,
                       item_display_name(game->weapon),
                       item_display_name(game->armor), victory);
}

static void run_campaign(void)
{
    EnemyDefs enemy_defs;
    ItemDefs item_defs;
    SheetSpec char_spec;
    PCXImage characters;
    GameState game;
    char current_level[DATA_NAME_LEN];
    char next_level[DATA_NAME_LEN];
    int result;
    int victory;

    victory = 0;
    if (!data_load_enemy_defs(ENEMY_DEFS_FILE, &enemy_defs) ||
        !data_load_item_defs(ITEM_DEFS_FILE, &item_defs) ||
        !data_load_sheet(CHAR_SPEC, &char_spec) ||
        !pcx_load_image(CHAR_IMAGE, &characters))
    {
        set_video_mode(0x03);
        dhero_printf("%s\n", lang_get("ERR_CAMPAIGN"));
        dhero_printf(lang_get("ERR_REQUIRED"), CHAR_IMAGE); dhero_printf("\n");
        dhero_printf(lang_get("ERR_DRAW"), CHAR_SPEC); dhero_printf("\n");
        dhero_printf("%s\n", lang_get("END_PRESS"));
        dhero_getch();
        return;
    }

    srand((unsigned int)time(NULL));
    init_game(&game);
    strcpy(current_level, "LEVEL1");

    while (1)
    {
        strcpy(next_level, "END");
        result = run_level(current_level, &game,
                           &enemy_defs, &item_defs,
                           &characters, &char_spec,
                           next_level);

        if (result == 1)
        {
            if (strcmp(next_level, "END") == 0)
            {
                victory = 1;
                break;
            }
            strcpy(current_level, next_level);
            continue;
        }

        if (result == -1)
        {
            set_video_mode(0x03);
            dhero_printf(lang_get("ERR_LEVEL"), current_level); dhero_printf("\n");
            dhero_printf("%s\n", lang_get("ERR_LEVEL_HINT"));
            dhero_printf("%s\n", lang_get("END_PRESS"));
            dhero_getch();
            pcx_free_image(&characters);
            return;
        }

        if (result == -3)
        {
            pcx_free_image(&characters);
            return;
        }

        break;
    }

    speaker_stop();
    pcx_free_image(&characters);
    finish_scored_run(&game, victory);
}

int dhero_game_run(void)
{
    int selection;
    int running;

    lang_init();
    running = 1;

    if (!check_file(MENU_IMAGE))
    {
        dhero_printf(lang_get("ERR_MENU_MISSING"), MENU_IMAGE);
        dhero_printf("\n");
        return 1;
    }

    score_show_presents_once();

    while (running)
    {
        if (!load_pcx(MENU_IMAGE))
        {
            set_video_mode(0x03);
            dhero_printf(lang_get("ERR_MENU_LOAD"), MENU_IMAGE);
            dhero_printf("\n");
            return 1;
        }

        sound_intro();
        selection = run_menu();
        if (selection == MENU_NEW_GAME) run_campaign();
        else if (selection == MENU_EXIT) running = 0;
    }

    speaker_stop();
    set_video_mode(0x03);
    return 0;
}
