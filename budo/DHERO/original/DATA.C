#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "DATA.H"

static void trim_eol(char *text)
{
    int len;

    len = (int)strlen(text);
    while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n'))
    {
        text[len - 1] = '\0';
        len--;
    }
}

static char *skip_space(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text))
    {
        text++;
    }
    return text;
}

static void copy_name(char *dest, const char *source)
{
    strncpy(dest, source, DATA_NAME_LEN - 1);
    dest[DATA_NAME_LEN - 1] = '\0';
}

int data_load_sheet(const char *filename, SheetSpec *sheet)
{
    FILE *file;
    char line[256];
    char *text;
    char name[DATA_NAME_LEN];
    int column;
    int row;

    memset(sheet, 0, sizeof(SheetSpec));

    file = fopen(filename, "rt");
    if (file == NULL)
    {
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        trim_eol(line);
        text = skip_space(line);

        if (*text == '\0' || *text == '#' || *text == ';')
        {
            continue;
        }

        if (sscanf(text, "CELL %d %d", &sheet->cell_width, &sheet->cell_height) == 2)
        {
            continue;
        }

        if (strcmp(text, "TRANSPARENT TOP_LEFT") == 0)
        {
            sheet->transparent_top_left = 1;
            continue;
        }

        if (sscanf(text, "%31s %d %d", name, &column, &row) == 3)
        {
            if (sheet->entry_count >= DATA_MAX_SHEET_ENTRIES || column < 0 || row < 0)
            {
                fclose(file);
                return 0;
            }

            copy_name(sheet->entries[sheet->entry_count].name, name);
            sheet->entries[sheet->entry_count].column = column;
            sheet->entries[sheet->entry_count].row = row;
            sheet->entry_count++;
        }
    }

    fclose(file);

    return sheet->cell_width > 0 &&
           sheet->cell_height > 0 &&
           sheet->entry_count > 0;
}

int data_find_sheet_entry(const SheetSpec *sheet, const char *name)
{
    int i;

    for (i = 0; i < sheet->entry_count; i++)
    {
        if (strcmp(sheet->entries[i].name, name) == 0)
        {
            return i;
        }
    }

    return -1;
}

static int find_legend_symbol(const LevelData *level, char symbol)
{
    int i;

    for (i = 0; i < level->legend_count; i++)
    {
        if (level->legend[i].symbol == symbol)
        {
            return i;
        }
    }

    return -1;
}

int data_level_find_legend_by_tile(const LevelData *level, const char *tile)
{
    int i;

    for (i = 0; i < level->legend_count; i++)
    {
        if (strcmp(level->legend[i].tile, tile) == 0)
        {
            return i;
        }
    }

    return -1;
}

int data_level_set_tile(LevelData *level, int x, int y, const char *tile)
{
    int legend_index;

    if (x < 0 || y < 0 || x >= level->width || y >= level->height)
    {
        return 0;
    }

    legend_index = data_level_find_legend_by_tile(level, tile);
    if (legend_index < 0)
    {
        return 0;
    }

    level->cells[y][x] = (unsigned char)legend_index;
    return 1;
}

int data_level_is_walkable(const LevelData *level, int x, int y)
{
    int legend_index;

    if (x < 0 || y < 0 || x >= level->width || y >= level->height)
    {
        return 0;
    }

    legend_index = level->cells[y][x];
    if (legend_index < 0 || legend_index >= level->legend_count)
    {
        return 0;
    }

    return level->legend[legend_index].walkable;
}

int data_load_level(const char *filename, LevelData *level)
{
    FILE *file;
    char line[256];
    char *text;
    char word1[DATA_NAME_LEN];
    char word2[DATA_NAME_LEN];
    char word3[DATA_NAME_LEN];
    char movement[16];
    char symbol;
    int mode;
    int map_row;
    int x;
    int index;
    int a;
    int b;
    int c;

    memset(level, 0, sizeof(LevelData));
    level->start_x = -1;
    level->start_y = -1;

    file = fopen(filename, "rt");
    if (file == NULL)
    {
        return 0;
    }

    mode = 0;
    map_row = 0;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        trim_eol(line);
        text = skip_space(line);

        if (*text == '\0' || *text == ';')
        {
            continue;
        }

        if (mode == 1)
        {
            if (strcmp(text, "END") == 0)
            {
                mode = 0;
                continue;
            }

            if (sscanf(text, "%c %31s %15s", &symbol, word1, movement) != 3)
            {
                fclose(file);
                return 0;
            }

            if (level->legend_count >= DATA_MAX_LEGEND_ENTRIES)
            {
                fclose(file);
                return 0;
            }

            level->legend[level->legend_count].symbol = symbol;
            copy_name(level->legend[level->legend_count].tile, word1);

            if (strcmp(movement, "WALK") == 0)
            {
                level->legend[level->legend_count].walkable = 1;
            }
            else if (strcmp(movement, "BLOCK") == 0)
            {
                level->legend[level->legend_count].walkable = 0;
            }
            else
            {
                fclose(file);
                return 0;
            }

            level->legend_count++;
            continue;
        }

        if (mode == 2)
        {
            if (strcmp(text, "END") == 0)
            {
                mode = 0;
                continue;
            }

            if (map_row >= level->height || (int)strlen(text) != level->width)
            {
                fclose(file);
                return 0;
            }

            for (x = 0; x < level->width; x++)
            {
                index = find_legend_symbol(level, text[x]);
                if (index < 0)
                {
                    fclose(file);
                    return 0;
                }
                level->cells[map_row][x] = (unsigned char)index;
            }

            map_row++;
            continue;
        }

        if (strcmp(text, "LEGEND") == 0)
        {
            mode = 1;
            continue;
        }

        if (strcmp(text, "MAP") == 0)
        {
            if (level->width <= 0 || level->height <= 0 || level->legend_count <= 0)
            {
                fclose(file);
                return 0;
            }
            mode = 2;
            map_row = 0;
            continue;
        }

        if (sscanf(text, "NAME %31s", word1) == 1)
        {
            copy_name(level->name, word1);
            continue;
        }

        if (sscanf(text, "SIZE %d %d", &a, &b) == 2)
        {
            if (a <= 0 || b <= 0 || a > DATA_MAX_MAP_WIDTH || b > DATA_MAX_MAP_HEIGHT)
            {
                fclose(file);
                return 0;
            }
            level->width = a;
            level->height = b;
            continue;
        }

        if (sscanf(text, "TILES %31s", word1) == 1)
        {
            copy_name(level->tile_sheet, word1);
            continue;
        }

        if (sscanf(text, "HERO %31s", word1) == 1)
        {
            copy_name(level->hero_sheet, word1);
            continue;
        }

        if (sscanf(text, "START %d %d %31s", &a, &b, word1) == 3)
        {
            level->start_x = a;
            level->start_y = b;
            copy_name(level->start_frame, word1);
            continue;
        }

        if (sscanf(text, "DOOR %d %d %31s", &a, &b, word1) == 3)
        {
            if (level->door_count >= DATA_MAX_DOORS)
            {
                fclose(file);
                return 0;
            }
            level->doors[level->door_count].x = a;
            level->doors[level->door_count].y = b;
            copy_name(level->doors[level->door_count].open_tile, word1);
            level->door_count++;
            continue;
        }

        if (sscanf(text, "CHEST %d %d %31s %d %31s", &a, &b, word1, &c, word2) == 5)
        {
            if (level->chest_count >= DATA_MAX_CHESTS)
            {
                fclose(file);
                return 0;
            }
            level->chests[level->chest_count].x = a;
            level->chests[level->chest_count].y = b;
            copy_name(level->chests[level->chest_count].replacement_tile, word1);
            level->chests[level->chest_count].gold = c;
            copy_name(level->chests[level->chest_count].item, word2);
            level->chest_count++;
            continue;
        }

        if (sscanf(text, "ITEM %d %d %31s %31s", &a, &b, word1, word2) == 4)
        {
            if (level->item_count >= DATA_MAX_ITEMS)
            {
                fclose(file);
                return 0;
            }
            level->items[level->item_count].x = a;
            level->items[level->item_count].y = b;
            copy_name(level->items[level->item_count].replacement_tile, word1);
            copy_name(level->items[level->item_count].item, word2);
            level->item_count++;
            continue;
        }

        if (sscanf(text, "EXIT %d %d %31s", &a, &b, word1) == 3)
        {
            if (level->exit_count >= DATA_MAX_EXITS)
            {
                fclose(file);
                return 0;
            }
            level->exits[level->exit_count].x = a;
            level->exits[level->exit_count].y = b;
            copy_name(level->exits[level->exit_count].next_level, word1);
            level->exit_count++;
            continue;
        }

        if (sscanf(text, "ENEMY %31s %d %d %31s", word1, &a, &b, word2) == 4)
        {
            if (level->enemy_count >= DATA_MAX_ENEMIES)
            {
                fclose(file);
                return 0;
            }
            copy_name(level->enemies[level->enemy_count].type, word1);
            level->enemies[level->enemy_count].x = a;
            level->enemies[level->enemy_count].y = b;
            copy_name(level->enemies[level->enemy_count].direction, word2);
            level->enemy_count++;
            continue;
        }

        fclose(file);
        return 0;
    }

    fclose(file);

    if (mode != 0 ||
        level->name[0] == '\0' ||
        level->tile_sheet[0] == '\0' ||
        level->hero_sheet[0] == '\0' ||
        level->width <= 0 ||
        level->height <= 0 ||
        level->legend_count <= 0 ||
        map_row != level->height ||
        level->start_x < 0 ||
        level->start_y < 0 ||
        level->start_x >= level->width ||
        level->start_y >= level->height ||
        level->start_frame[0] == '\0')
    {
        return 0;
    }

    return 1;
}

int data_load_enemy_defs(const char *filename, EnemyDefs *defs)
{
    FILE *file;
    char line[256];
    char *text;
    EnemyDef entry;

    memset(defs, 0, sizeof(EnemyDefs));

    file = fopen(filename, "rt");
    if (file == NULL)
    {
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        trim_eol(line);
        text = skip_space(line);

        if (*text == '\0' || *text == '#' || *text == ';')
        {
            continue;
        }

        if (sscanf(text, "%31s %31s %d %d %d %d %d",
                   entry.name,
                   entry.sheet,
                   &entry.hp,
                   &entry.attack,
                   &entry.defense,
                   &entry.move,
                   &entry.gold) != 7)
        {
            fclose(file);
            return 0;
        }

        if (defs->count >= DATA_MAX_ENEMY_TYPES)
        {
            fclose(file);
            return 0;
        }

        defs->entries[defs->count] = entry;
        defs->count++;
    }

    fclose(file);
    return 1;
}

int data_find_enemy_def(const EnemyDefs *defs, const char *name)
{
    int i;

    for (i = 0; i < defs->count; i++)
    {
        if (strcmp(defs->entries[i].name, name) == 0)
        {
            return i;
        }
    }

    return -1;
}

int data_load_item_defs(const char *filename, ItemDefs *defs)
{
    FILE *file;
    char line[256];
    char *text;
    ItemDef entry;

    memset(defs, 0, sizeof(ItemDefs));

    file = fopen(filename, "rt");
    if (file == NULL)
    {
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        trim_eol(line);
        text = skip_space(line);

        if (*text == '\0' || *text == '#' || *text == ';')
        {
            continue;
        }

        if (sscanf(text, "%31s %31s %d %d %d %d %d",
                   entry.name,
                   entry.slot,
                   &entry.attack,
                   &entry.defense,
                   &entry.max_hp,
                   &entry.heal,
                   &entry.gold) != 7)
        {
            fclose(file);
            return 0;
        }

        if (defs->count >= DATA_MAX_ITEM_TYPES)
        {
            fclose(file);
            return 0;
        }

        defs->entries[defs->count] = entry;
        defs->count++;
    }

    fclose(file);
    return 1;
}

int data_find_item_def(const ItemDefs *defs, const char *name)
{
    int i;

    for (i = 0; i < defs->count; i++)
    {
        if (strcmp(defs->entries[i].name, name) == 0)
        {
            return i;
        }
    }

    return -1;
}
