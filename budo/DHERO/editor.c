#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "PCX.H"
#include "DATA.H"

#define VGA_MEMORY 0xA0000
#define SCREEN_W 320
#define SCREEN_H 200
#define VIEW_COLS 20
#define VIEW_ROWS 10
#define HUD_Y 160

#define KEY_ESC 27
#define KEY_ENTER 13
#define KEY_TAB 9
#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_LEFT 75
#define KEY_RIGHT 77
#define KEY_PGUP 73
#define KEY_PGDN 81
#define KEY_DEL 83
#define KEY_F1 59
#define KEY_F2 60

#define TOOL_TILE 0
#define TOOL_HERO 1
#define TOOL_ENEMY 2
#define TOOL_DOOR 3
#define TOOL_CHEST 4
#define TOOL_ITEM 5
#define TOOL_EXIT 6
#define TOOL_COUNT 7

#define SCORE_FILE "DATA/GAME/SCORES.DAT"
#define ENEMY_FILE "DATA/GAME/ENEMIES.DAT"
#define ITEM_FILE "DATA/GAME/ITEMS.DAT"
#define CHAR_SPEC_FILE "DATA/GFX/SPRITES/CHAR000.DAT"
#define CHAR_PCX_FILE "DATA/GFX/SPRITES/CHAR000.PCX"

static const char *tool_names[TOOL_COUNT] =
{
    "TILE", "HERO", "ENEMY", "DOOR", "CHEST", "ITEM", "EXIT"
};

static void set_mode(int mode)
{
    dhero_mode(mode);
}

static void put_pixel(int x, int y, unsigned char color)
{
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    dhero_pixels_write(&color, 1, VGA_MEMORY + y * SCREEN_W + x);
}

static void fill_rect(int x, int y, int w, int h, unsigned char color)
{
    unsigned char row[SCREEN_W];
    int i;
    int yy;

    if (w <= 0 || h <= 0 || x < 0 || y < 0 ||
        x + w > SCREEN_W || y + h > SCREEN_H) return;
    for (i = 0; i < w; i++) row[i] = color;
    for (yy = 0; yy < h; yy++)
        dhero_pixels_write(row, w, VGA_MEMORY + (y + yy) * SCREEN_W + x);
}

static void text_at(int row, int col, const char *text, unsigned char color)
{
    dhero_text(row, col, text, color);
}

static void frame(int x, int y, int w, int h, unsigned char color)
{
    int i;
    for (i = x; i < x + w; i++)
    {
        put_pixel(i, y, color);
        put_pixel(i, y + h - 1, color);
    }
    for (i = y; i < y + h; i++)
    {
        put_pixel(x, i, color);
        put_pixel(x + w - 1, i, color);
    }
}

static void draw_sheet(const PCXImage *image, const SheetSpec *sheet,
                       const char *name, int x, int y, int transparent)
{
    int i;
    int sx;
    int sy;

    i = data_find_sheet_entry(sheet, name);
    if (i < 0) return;
    sx = sheet->entries[i].column * sheet->cell_width;
    sy = sheet->entries[i].row * sheet->cell_height;
    pcx_blit_region(image, sx, sy, sheet->cell_width, sheet->cell_height,
                    x, y, transparent);
}

static int load_tiles(const LevelData *level, PCXImage *tiles,
                      SheetSpec *spec)
{
    char pcx[DATA_PATH_LEN];
    char dat[DATA_PATH_LEN];
    snprintf(pcx, sizeof(pcx), "DATA/GFX/TILES/%s.pcx", level->tile_sheet);
    snprintf(dat, sizeof(dat), "DATA/GFX/TILES/%s.DAT", level->tile_sheet);
    return data_load_sheet(dat, spec) && pcx_load_image(pcx, tiles);
}

static int enemy_at(const LevelData *level, int x, int y)
{
    int i;
    for (i = 0; i < level->enemy_count; i++)
        if (level->enemies[i].x == x && level->enemies[i].y == y) return i;
    return -1;
}

static int door_at(const LevelData *level, int x, int y)
{
    int i;
    for (i = 0; i < level->door_count; i++)
        if (level->doors[i].x == x && level->doors[i].y == y) return i;
    return -1;
}

static int chest_at(const LevelData *level, int x, int y)
{
    int i;
    for (i = 0; i < level->chest_count; i++)
        if (level->chests[i].x == x && level->chests[i].y == y) return i;
    return -1;
}

static int item_at(const LevelData *level, int x, int y)
{
    int i;
    for (i = 0; i < level->item_count; i++)
        if (level->items[i].x == x && level->items[i].y == y) return i;
    return -1;
}

static int exit_at(const LevelData *level, int x, int y)
{
    int i;
    for (i = 0; i < level->exit_count; i++)
        if (level->exits[i].x == x && level->exits[i].y == y) return i;
    return -1;
}

static void remove_enemy(LevelData *level, int index)
{
    int i;
    if (index < 0) return;
    for (i = index; i < level->enemy_count - 1; i++)
        level->enemies[i] = level->enemies[i + 1];
    level->enemy_count--;
}

static void remove_door(LevelData *level, int index)
{
    int i;
    if (index < 0) return;
    for (i = index; i < level->door_count - 1; i++)
        level->doors[i] = level->doors[i + 1];
    level->door_count--;
}

static void remove_chest(LevelData *level, int index)
{
    int i;
    if (index < 0) return;
    for (i = index; i < level->chest_count - 1; i++)
        level->chests[i] = level->chests[i + 1];
    level->chest_count--;
}

static void remove_item(LevelData *level, int index)
{
    int i;
    if (index < 0) return;
    for (i = index; i < level->item_count - 1; i++)
        level->items[i] = level->items[i + 1];
    level->item_count--;
}

static void remove_exit(LevelData *level, int index)
{
    int i;
    if (index < 0) return;
    for (i = index; i < level->exit_count - 1; i++)
        level->exits[i] = level->exits[i + 1];
    level->exit_count--;
}

static void set_floor_if_possible(LevelData *level, int x, int y)
{
    if (data_level_find_legend_by_tile(level, "FLOOR") >= 0)
        data_level_set_tile(level, x, y, "FLOOR");
}

static void next_level_name(int number, char *name)
{
    if (number >= 10) strcpy(name, "END");
    else snprintf(name, DATA_NAME_LEN, "LEVEL%d", number + 1);
}

static int ensure_door(LevelData *level, int x, int y)
{
    int i;

    if (data_level_find_legend_by_tile(level, "DOOR_CLOSED") < 0 ||
        data_level_find_legend_by_tile(level, "DOOR_OPEN") < 0) return 0;

    i = door_at(level, x, y);
    if (i < 0)
    {
        if (level->door_count >= DATA_MAX_DOORS) return 0;
        i = level->door_count++;
        level->doors[i].x = x;
        level->doors[i].y = y;
    }
    strcpy(level->doors[i].open_tile, "DOOR_OPEN");
    return data_level_set_tile(level, x, y, "DOOR_CLOSED");
}

static int ensure_chest(LevelData *level, int x, int y)
{
    int i;

    if (data_level_find_legend_by_tile(level, "CHEST") < 0 ||
        data_level_find_legend_by_tile(level, "FLOOR") < 0) return 0;

    i = chest_at(level, x, y);
    if (i < 0)
    {
        if (level->chest_count >= DATA_MAX_CHESTS) return 0;
        i = level->chest_count++;
        level->chests[i].x = x;
        level->chests[i].y = y;
        strcpy(level->chests[i].replacement_tile, "FLOOR");
        level->chests[i].gold = 0;
        strcpy(level->chests[i].item, "NONE");
    }
    return data_level_set_tile(level, x, y, "CHEST");
}

static int ensure_exit(LevelData *level, int level_number, int x, int y)
{
    if (data_level_find_legend_by_tile(level, "STAIRS") < 0) return 0;
    level->exit_count = 1;
    level->exits[0].x = x;
    level->exits[0].y = y;
    next_level_name(level_number, level->exits[0].next_level);
    return data_level_set_tile(level, x, y, "STAIRS");
}

static void clear_special_metadata(LevelData *level, int x, int y,
                                   const char *new_tile)
{
    int i;

    if (strcmp(new_tile, "DOOR_CLOSED") != 0)
    {
        i = door_at(level, x, y);
        if (i >= 0) remove_door(level, i);
    }
    if (strcmp(new_tile, "CHEST") != 0)
    {
        i = chest_at(level, x, y);
        if (i >= 0) remove_chest(level, i);
    }
    if (strcmp(new_tile, "STAIRS") != 0)
    {
        i = exit_at(level, x, y);
        if (i >= 0) remove_exit(level, i);
    }
}

static void rotate_direction(char *direction)
{
    if (strcmp(direction, "UP") == 0) strcpy(direction, "RIGHT");
    else if (strcmp(direction, "RIGHT") == 0) strcpy(direction, "DOWN");
    else if (strcmp(direction, "DOWN") == 0) strcpy(direction, "LEFT");
    else strcpy(direction, "UP");
}

static void camera_for(const LevelData *level, int cx, int cy,
                       int *camera_x, int *camera_y)
{
    int max_x;
    int max_y;

    *camera_x = cx - VIEW_COLS / 2;
    *camera_y = cy - VIEW_ROWS / 2;
    max_x = level->width - VIEW_COLS;
    max_y = level->height - VIEW_ROWS;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (*camera_x < 0) *camera_x = 0;
    if (*camera_y < 0) *camera_y = 0;
    if (*camera_x > max_x) *camera_x = max_x;
    if (*camera_y > max_y) *camera_y = max_y;
}

static void draw_editor(const LevelData *level,
                        const PCXImage *tiles, const SheetSpec *tile_spec,
                        const PCXImage *chars, const SheetSpec *char_spec,
                        const EnemyDefs *enemy_defs, const ItemDefs *item_defs,
                        int cx, int cy, int tool, int tile_sel,
                        int enemy_sel, int item_sel, int dirty,
                        const char *status)
{
    int cam_x;
    int cam_y;
    int vx;
    int vy;
    int mx;
    int my;
    int li;
    int i;
    int px;
    int py;
    int transparent;
    char line[80];
    const char *selection;

    camera_for(level, cx, cy, &cam_x, &cam_y);
    fill_rect(0, 0, SCREEN_W, SCREEN_H, 0);

    for (vy = 0; vy < VIEW_ROWS; vy++)
    {
        for (vx = 0; vx < VIEW_COLS; vx++)
        {
            mx = cam_x + vx;
            my = cam_y + vy;
            if (mx >= level->width || my >= level->height) continue;
            li = level->cells[my][mx];
            if (li >= 0 && li < level->legend_count)
                draw_sheet(tiles, tile_spec, level->legend[li].tile,
                           vx * 16, vy * 16, -1);
        }
    }

    transparent = char_spec->transparent_top_left ? chars->pixels[0] : -1;
    for (i = 0; i < level->enemy_count; i++)
    {
        vx = level->enemies[i].x - cam_x;
        vy = level->enemies[i].y - cam_y;
        if (vx < 0 || vy < 0 || vx >= VIEW_COLS || vy >= VIEW_ROWS) continue;
        draw_sheet(chars, char_spec, level->enemies[i].type,
                   vx * 16, vy * 16, transparent);
    }

    vx = level->start_x - cam_x;
    vy = level->start_y - cam_y;
    if (vx >= 0 && vy >= 0 && vx < VIEW_COLS && vy < VIEW_ROWS)
        draw_sheet(chars, char_spec, "HERO", vx * 16, vy * 16, transparent);

    px = (cx - cam_x) * 16;
    py = (cy - cam_y) * 16;
    frame(px, py, 16, 16, 255);
    frame(px + 1, py + 1, 14, 14, 252);

    fill_rect(0, HUD_Y, SCREEN_W, 40, 0);
    snprintf(line, sizeof(line), "%s %dx%d CUR:%d,%d %s",
            level->name, level->width, level->height, cx, cy,
            dirty ? "*CHANGED*" : "SAVED");
    text_at(20, 0, line, 255);

    selection = "";
    if (tool == TOOL_TILE && tile_sel < level->legend_count)
        selection = level->legend[tile_sel].tile;
    else if (tool == TOOL_ENEMY && enemy_sel < enemy_defs->count)
        selection = enemy_defs->entries[enemy_sel].name;
    else if (tool == TOOL_ITEM && item_sel < item_defs->count)
        selection = item_defs->entries[item_sel].name;
    snprintf(line, sizeof(line), "TOOL:%s SELECT:%s", tool_names[tool], selection);
    text_at(21, 0, line, 254);
    text_at(22, 0, status, 255);
    text_at(23, 0, "TAB TOOL +/- SELECT ENTER APPLY DEL REMOVE", 253);
    text_at(24, 0, "F1 HELP F2 SAVE PGUP/DN LEVEL ESC EXIT", 252);
}

static void show_help(void)
{
    fill_rect(18, 16, 284, 168, 0);
    frame(18, 16, 284, 168, 255);
    text_at(3, 12, "DUNGEON HERO LEVEL EDITOR", 255);
    text_at(5, 3, "ARROWS  MOVE CURSOR", 254);
    text_at(6, 3, "TAB     NEXT TOOL", 254);
    text_at(7, 3, "+ / -   SELECT TILE/ENEMY/ITEM", 254);
    text_at(8, 3, "ENTER   APPLY CURRENT TOOL", 254);
    text_at(9, 3, "DEL     REMOVE OBJECT", 254);
    text_at(10, 3, "R       ROTATE HERO/ENEMY", 254);
    text_at(11, 3, "G       CYCLE CHEST GOLD", 254);
    text_at(12, 3, "I       CYCLE CHEST ITEM", 254);
    text_at(13, 3, "F2      SAVE + RESET TOP10", 254);
    text_at(14, 3, "PGUP/DN CHANGE LEVEL", 254);
    text_at(15, 3, "SPECIAL TILES AUTO-CREATE OBJECT DATA", 253);
    text_at(17, 7, "PRESS ANY KEY", 252);
    dhero_getch();
}

static void level_path(int number, char *path)
{
    snprintf(path, DATA_PATH_LEN, "DATA/GFX/MAP/LEVEL%d.DAT", number);
}

static int save_level(const LevelData *level, int number)
{
    FILE *f;
    LevelData check;
    char path[DATA_PATH_LEN];
    char tmp[DATA_PATH_LEN];
    char bak[DATA_PATH_LEN];
    int x;
    int y;
    int i;
    int li;

    level_path(number, path);
    snprintf(tmp, sizeof(tmp), "DATA/GFX/MAP/LEVEL%d.TMP", number);
    snprintf(bak, sizeof(bak), "DATA/GFX/MAP/LEVEL%d.BAK", number);

    f = fopen(tmp, "wt");
    if (f == NULL) return 0;
    fprintf(f, "; DUNGEON HERO - LEVEL %d - SAVED BY EDITOR\n", number);
    fprintf(f, "NAME %s\n", level->name);
    fprintf(f, "SIZE %d %d\n", level->width, level->height);
    fprintf(f, "TILES %s\n", level->tile_sheet);
    fprintf(f, "HERO %s\n", level->hero_sheet);
    fprintf(f, "LEGEND\n");
    for (i = 0; i < level->legend_count; i++)
        fprintf(f, "%c %s %s\n", level->legend[i].symbol,
                level->legend[i].tile,
                level->legend[i].walkable ? "WALK" : "BLOCK");
    fprintf(f, "END\nMAP\n");
    for (y = 0; y < level->height; y++)
    {
        for (x = 0; x < level->width; x++)
        {
            li = level->cells[y][x];
            if (li < 0 || li >= level->legend_count)
            {
                fclose(f);
                remove(tmp);
                return 0;
            }
            fputc(level->legend[li].symbol, f);
        }
        fputc('\n', f);
    }
    fprintf(f, "END\n");
    fprintf(f, "START %d %d %s\n", level->start_x, level->start_y,
            level->start_frame);
    for (i = 0; i < level->door_count; i++)
        fprintf(f, "DOOR %d %d %s\n", level->doors[i].x,
                level->doors[i].y, level->doors[i].open_tile);
    for (i = 0; i < level->chest_count; i++)
        fprintf(f, "CHEST %d %d %s %d %s\n", level->chests[i].x,
                level->chests[i].y, level->chests[i].replacement_tile,
                level->chests[i].gold, level->chests[i].item);
    for (i = 0; i < level->item_count; i++)
        fprintf(f, "ITEM %d %d %s %s\n", level->items[i].x,
                level->items[i].y, level->items[i].replacement_tile,
                level->items[i].item);
    for (i = 0; i < level->enemy_count; i++)
        fprintf(f, "ENEMY %s %d %d %s\n", level->enemies[i].type,
                level->enemies[i].x, level->enemies[i].y,
                level->enemies[i].direction);
    for (i = 0; i < level->exit_count; i++)
        fprintf(f, "EXIT %d %d %s\n", level->exits[i].x,
                level->exits[i].y, level->exits[i].next_level);
    fclose(f);

    if (!data_load_level(tmp, &check))
    {
        remove(tmp);
        return 0;
    }

    remove(bak);
    rename(path, bak);
    if (rename(tmp, path) != 0)
    {
        rename(bak, path);
        remove(tmp);
        return 0;
    }
    remove(SCORE_FILE);
    return 1;
}

static void cycle_chest_gold(LevelData *level, int x, int y)
{
    int i;
    i = chest_at(level, x, y);
    if (i < 0) return;
    if (level->chests[i].gold == 0) level->chests[i].gold = 10;
    else if (level->chests[i].gold == 10) level->chests[i].gold = 25;
    else if (level->chests[i].gold == 25) level->chests[i].gold = 50;
    else level->chests[i].gold = 0;
}

static void cycle_chest_item(LevelData *level, const ItemDefs *defs,
                             int x, int y)
{
    int c;
    int i;
    int current;

    c = chest_at(level, x, y);
    if (c < 0) return;
    current = -1;
    for (i = 0; i < defs->count; i++)
        if (strcmp(defs->entries[i].name, level->chests[c].item) == 0)
            current = i;
    current++;
    if (current >= defs->count) strcpy(level->chests[c].item, "NONE");
    else strcpy(level->chests[c].item, defs->entries[current].name);
}

static int paint_tile(LevelData *level, int level_number,
                      int x, int y, int tile_sel, char *status)
{
    const char *name;

    if (tile_sel < 0 || tile_sel >= level->legend_count) return 0;
    name = level->legend[tile_sel].tile;

    if (strcmp(name, "DOOR_CLOSED") == 0)
    {
        clear_special_metadata(level, x, y, name);
        if (!ensure_door(level, x, y))
        {
            strcpy(status, "DOOR NEEDS DOOR_OPEN IN LEGEND.");
            return 0;
        }
        strcpy(status, "DOOR PAINTED + INTERACTION CREATED.");
        return 1;
    }
    if (strcmp(name, "CHEST") == 0)
    {
        clear_special_metadata(level, x, y, name);
        if (!ensure_chest(level, x, y))
        {
            strcpy(status, "CHEST NEEDS FLOOR IN LEGEND.");
            return 0;
        }
        strcpy(status, "CHEST PAINTED + REWARD DATA CREATED.");
        return 1;
    }
    if (strcmp(name, "STAIRS") == 0)
    {
        clear_special_metadata(level, x, y, name);
        if (!ensure_exit(level, level_number, x, y)) return 0;
        strcpy(status, "STAIRS PAINTED + EXIT CREATED.");
        return 1;
    }

    clear_special_metadata(level, x, y, name);
    level->cells[y][x] = (unsigned char)tile_sel;
    strcpy(status, "TILE PAINTED.");
    return 1;
}

static int apply_tool(LevelData *level, int level_number,
                      const EnemyDefs *enemy_defs, const ItemDefs *item_defs,
                      int x, int y, int tool, int tile_sel,
                      int enemy_sel, int item_sel, char *status)
{
    int i;

    if (tool == TOOL_TILE)
        return paint_tile(level, level_number, x, y, tile_sel, status);

    if (tool == TOOL_HERO)
    {
        if (!data_level_is_walkable(level, x, y))
        {
            strcpy(status, "HERO NEEDS WALKABLE TILE.");
            return 0;
        }
        level->start_x = x;
        level->start_y = y;
        strcpy(status, "HERO START MOVED. R ROTATES.");
        return 1;
    }

    if (tool == TOOL_ENEMY)
    {
        if (!data_level_is_walkable(level, x, y))
        {
            strcpy(status, "ENEMY NEEDS WALKABLE TILE.");
            return 0;
        }
        i = enemy_at(level, x, y);
        if (i < 0)
        {
            if (level->enemy_count >= DATA_MAX_ENEMIES) return 0;
            i = level->enemy_count++;
            level->enemies[i].x = x;
            level->enemies[i].y = y;
            strcpy(level->enemies[i].direction, "DOWN");
        }
        strcpy(level->enemies[i].type, enemy_defs->entries[enemy_sel].name);
        strcpy(status, "ENEMY PLACED. R ROTATES, DEL REMOVES.");
        return 1;
    }

    if (tool == TOOL_DOOR)
    {
        clear_special_metadata(level, x, y, "DOOR_CLOSED");
        if (!ensure_door(level, x, y))
        {
            strcpy(status, "DOOR NEEDS CLOSED/OPEN TILES IN LEGEND.");
            return 0;
        }
        strcpy(status, "DOOR PLACED + INTERACTION READY.");
        return 1;
    }

    if (tool == TOOL_CHEST)
    {
        clear_special_metadata(level, x, y, "CHEST");
        if (!ensure_chest(level, x, y))
        {
            strcpy(status, "CHEST NEEDS CHEST/FLOOR TILES IN LEGEND.");
            return 0;
        }
        strcpy(status, "CHEST: G GOLD, I ITEM, DEL REMOVE.");
        return 1;
    }

    if (tool == TOOL_ITEM)
    {
        if (!data_level_is_walkable(level, x, y))
        {
            strcpy(status, "ITEM NEEDS WALKABLE TILE.");
            return 0;
        }
        i = item_at(level, x, y);
        if (i < 0)
        {
            if (level->item_count >= DATA_MAX_ITEMS) return 0;
            i = level->item_count++;
            level->items[i].x = x;
            level->items[i].y = y;
            strcpy(level->items[i].replacement_tile, "FLOOR");
        }
        strcpy(level->items[i].item, item_defs->entries[item_sel].name);
        strcpy(status, "ITEM PLACED. DEL REMOVES.");
        return 1;
    }

    if (tool == TOOL_EXIT)
    {
        clear_special_metadata(level, x, y, "STAIRS");
        if (!ensure_exit(level, level_number, x, y))
        {
            strcpy(status, "EXIT NEEDS STAIRS TILE IN LEGEND.");
            return 0;
        }
        strcpy(status, "EXIT MOVED; DESTINATION SET AUTOMATICALLY.");
        return 1;
    }

    return 0;
}

static int remove_tool(LevelData *level, int x, int y, int tool,
                       char *status)
{
    int i;

    if (tool == TOOL_ENEMY)
    {
        i = enemy_at(level, x, y);
        if (i < 0) return 0;
        remove_enemy(level, i);
        strcpy(status, "ENEMY REMOVED.");
        return 1;
    }
    if (tool == TOOL_DOOR)
    {
        i = door_at(level, x, y);
        if (i < 0) return 0;
        remove_door(level, i);
        set_floor_if_possible(level, x, y);
        strcpy(status, "DOOR REMOVED.");
        return 1;
    }
    if (tool == TOOL_CHEST)
    {
        i = chest_at(level, x, y);
        if (i < 0) return 0;
        remove_chest(level, i);
        set_floor_if_possible(level, x, y);
        strcpy(status, "CHEST REMOVED.");
        return 1;
    }
    if (tool == TOOL_ITEM)
    {
        i = item_at(level, x, y);
        if (i < 0) return 0;
        remove_item(level, i);
        strcpy(status, "ITEM REMOVED.");
        return 1;
    }
    if (tool == TOOL_EXIT)
    {
        i = exit_at(level, x, y);
        if (i < 0) return 0;
        remove_exit(level, i);
        set_floor_if_possible(level, x, y);
        strcpy(status, "EXIT REMOVED.");
        return 1;
    }
    return 0;
}

int dhero_editor_run(void)
{
    LevelData level;
    EnemyDefs enemy_defs;
    ItemDefs item_defs;
    SheetSpec tile_spec;
    SheetSpec char_spec;
    PCXImage tiles;
    PCXImage chars;
    char path[DATA_PATH_LEN];
    char status[80];
    int level_number;
    int cx;
    int cy;
    int tool;
    int tile_sel;
    int enemy_sel;
    int item_sel;
    int dirty;
    int key;
    int scan;
    int old_level;
    int i;

    if (!data_load_enemy_defs(ENEMY_FILE, &enemy_defs) || enemy_defs.count <= 0 ||
        !data_load_item_defs(ITEM_FILE, &item_defs) || item_defs.count <= 0 ||
        !data_load_sheet(CHAR_SPEC_FILE, &char_spec) ||
        !pcx_load_image(CHAR_PCX_FILE, &chars))
    {
        dhero_printf("LEVEL EDITOR: missing game data or CHAR000 assets.\n");
        return 1;
    }

    level_number = 1;
    tool = TOOL_TILE;
    tile_sel = 0;
    enemy_sel = 0;
    item_sel = 0;
    dirty = 0;
    strcpy(status, "F1 HELP. TAB CHANGES TOOL.");

reload_level:
    level_path(level_number, path);
    if (!data_load_level(path, &level))
    {
        set_mode(3);
        dhero_printf("LEVEL EDITOR: could not load %s\n", path);
        pcx_free_image(&chars);
        return 1;
    }
    if (!load_tiles(&level, &tiles, &tile_spec))
    {
        set_mode(3);
        dhero_printf("LEVEL EDITOR: could not load tile sheet for %s\n", path);
        pcx_free_image(&chars);
        return 1;
    }

    pcx_remap_to_palette(&chars, tiles.palette);
    cx = level.start_x;
    cy = level.start_y;
    if (tile_sel >= level.legend_count) tile_sel = 0;
    dirty = 0;
    set_mode(0x13);
    pcx_apply_palette(&tiles);

    while (1)
    {
        draw_editor(&level, &tiles, &tile_spec, &chars, &char_spec,
                    &enemy_defs, &item_defs, cx, cy, tool, tile_sel,
                    enemy_sel, item_sel, dirty, status);
        key = dhero_getch();

        if (key == KEY_ESC)
        {
            if (dirty)
            {
                strcpy(status, "UNSAVED: F2 SAVE OR ESC AGAIN TO DISCARD.");
                draw_editor(&level, &tiles, &tile_spec, &chars, &char_spec,
                            &enemy_defs, &item_defs, cx, cy, tool, tile_sel,
                            enemy_sel, item_sel, dirty, status);
                key = dhero_getch();
                if (key != KEY_ESC) continue;
            }
            break;
        }

        if (key == KEY_TAB)
        {
            tool++;
            if (tool >= TOOL_COUNT) tool = 0;
            snprintf(status, sizeof(status), "TOOL: %s", tool_names[tool]);
            continue;
        }

        if (key == '+' || key == '=')
        {
            if (tool == TOOL_TILE)
            {
                tile_sel++;
                if (tile_sel >= level.legend_count) tile_sel = 0;
            }
            else if (tool == TOOL_ENEMY)
            {
                enemy_sel++;
                if (enemy_sel >= enemy_defs.count) enemy_sel = 0;
            }
            else if (tool == TOOL_ITEM)
            {
                item_sel++;
                if (item_sel >= item_defs.count) item_sel = 0;
            }
            continue;
        }

        if (key == '-')
        {
            if (tool == TOOL_TILE)
            {
                tile_sel--;
                if (tile_sel < 0) tile_sel = level.legend_count - 1;
            }
            else if (tool == TOOL_ENEMY)
            {
                enemy_sel--;
                if (enemy_sel < 0) enemy_sel = enemy_defs.count - 1;
            }
            else if (tool == TOOL_ITEM)
            {
                item_sel--;
                if (item_sel < 0) item_sel = item_defs.count - 1;
            }
            continue;
        }

        if (key == 'r' || key == 'R')
        {
            if (tool == TOOL_HERO && cx == level.start_x && cy == level.start_y)
            {
                rotate_direction(level.start_frame);
                dirty = 1;
                strcpy(status, "HERO ROTATED.");
            }
            else if (tool == TOOL_ENEMY)
            {
                i = enemy_at(&level, cx, cy);
                if (i >= 0)
                {
                    rotate_direction(level.enemies[i].direction);
                    dirty = 1;
                    strcpy(status, "ENEMY ROTATED.");
                }
            }
            continue;
        }

        if ((key == 'g' || key == 'G') && tool == TOOL_CHEST)
        {
            i = chest_at(&level, cx, cy);
            if (i >= 0)
            {
                cycle_chest_gold(&level, cx, cy);
                dirty = 1;
                snprintf(status, sizeof(status), "CHEST GOLD: %d", level.chests[i].gold);
            }
            continue;
        }

        if ((key == 'i' || key == 'I') && tool == TOOL_CHEST)
        {
            i = chest_at(&level, cx, cy);
            if (i >= 0)
            {
                cycle_chest_item(&level, &item_defs, cx, cy);
                dirty = 1;
                snprintf(status, sizeof(status), "CHEST ITEM: %s", level.chests[i].item);
            }
            continue;
        }

        if (key == KEY_ENTER)
        {
            if (apply_tool(&level, level_number, &enemy_defs, &item_defs,
                           cx, cy, tool, tile_sel, enemy_sel, item_sel, status))
                dirty = 1;
            continue;
        }

        if (key == 0 || key == 0xE0)
        {
            scan = dhero_getch();
            if (scan == KEY_UP && cy > 0) cy--;
            else if (scan == KEY_DOWN && cy < level.height - 1) cy++;
            else if (scan == KEY_LEFT && cx > 0) cx--;
            else if (scan == KEY_RIGHT && cx < level.width - 1) cx++;
            else if (scan == KEY_DEL)
            {
                if (remove_tool(&level, cx, cy, tool, status)) dirty = 1;
            }
            else if (scan == KEY_F1) show_help();
            else if (scan == KEY_F2)
            {
                if (!dirty) strcpy(status, "NO CHANGES TO SAVE.");
                else if (save_level(&level, level_number))
                {
                    dirty = 0;
                    strcpy(status, "SAVED. TOP10 SCOREBOARD RESET.");
                }
                else strcpy(status, "SAVE FAILED; ORIGINAL PRESERVED.");
            }
            else if (scan == KEY_PGUP || scan == KEY_PGDN)
            {
                if (dirty)
                {
                    strcpy(status, "SAVE/DISCARD BEFORE CHANGING LEVEL.");
                    continue;
                }
                old_level = level_number;
                if (scan == KEY_PGUP) level_number--;
                else level_number++;
                if (level_number < 1) level_number = 10;
                if (level_number > 10) level_number = 1;
                if (level_number != old_level)
                {
                    pcx_free_image(&tiles);
                    goto reload_level;
                }
            }
        }
    }

    pcx_free_image(&tiles);
    pcx_free_image(&chars);
    set_mode(3);
    return 0;
}
