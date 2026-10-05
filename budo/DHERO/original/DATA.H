#ifndef DATA_H
#define DATA_H

#define DATA_NAME_LEN            32
#define DATA_PATH_LEN            80
#define DATA_MAX_SHEET_ENTRIES   256
#define DATA_MAX_LEGEND_ENTRIES  64
#define DATA_MAX_MAP_WIDTH       64
#define DATA_MAX_MAP_HEIGHT      64
#define DATA_MAX_DOORS           64
#define DATA_MAX_CHESTS          64
#define DATA_MAX_ITEMS           64
#define DATA_MAX_EXITS           16
#define DATA_MAX_ENEMIES         64
#define DATA_MAX_ENEMY_TYPES     32
#define DATA_MAX_ITEM_TYPES      64

#define DATA_SLOT_NONE           "NONE"

typedef struct
{
    char name[DATA_NAME_LEN];
    int column;
    int row;
} SheetEntry;

typedef struct
{
    int cell_width;
    int cell_height;
    int transparent_top_left;
    int entry_count;
    SheetEntry entries[DATA_MAX_SHEET_ENTRIES];
} SheetSpec;

typedef struct
{
    char symbol;
    char tile[DATA_NAME_LEN];
    int walkable;
} LevelLegendEntry;

typedef struct
{
    int x;
    int y;
    char open_tile[DATA_NAME_LEN];
    int opened;
} LevelDoor;

typedef struct
{
    int x;
    int y;
    char replacement_tile[DATA_NAME_LEN];
    int gold;
    char item[DATA_NAME_LEN];
    int opened;
} LevelChest;

typedef struct
{
    int x;
    int y;
    char replacement_tile[DATA_NAME_LEN];
    char item[DATA_NAME_LEN];
    int taken;
} LevelItem;

typedef struct
{
    int x;
    int y;
    char next_level[DATA_NAME_LEN];
} LevelExit;

typedef struct
{
    char type[DATA_NAME_LEN];
    int x;
    int y;
    char direction[DATA_NAME_LEN];
} LevelEnemy;

typedef struct
{
    char name[DATA_NAME_LEN];
    char tile_sheet[DATA_NAME_LEN];
    char hero_sheet[DATA_NAME_LEN];
    int width;
    int height;

    int legend_count;
    LevelLegendEntry legend[DATA_MAX_LEGEND_ENTRIES];
    unsigned char cells[DATA_MAX_MAP_HEIGHT][DATA_MAX_MAP_WIDTH];

    int start_x;
    int start_y;
    char start_frame[DATA_NAME_LEN];

    int door_count;
    LevelDoor doors[DATA_MAX_DOORS];

    int chest_count;
    LevelChest chests[DATA_MAX_CHESTS];

    int item_count;
    LevelItem items[DATA_MAX_ITEMS];

    int exit_count;
    LevelExit exits[DATA_MAX_EXITS];

    int enemy_count;
    LevelEnemy enemies[DATA_MAX_ENEMIES];
} LevelData;

typedef struct
{
    char name[DATA_NAME_LEN];
    char sheet[DATA_NAME_LEN];
    int hp;
    int attack;
    int defense;
    int move;
    int gold;
} EnemyDef;

typedef struct
{
    int count;
    EnemyDef entries[DATA_MAX_ENEMY_TYPES];
} EnemyDefs;

typedef struct
{
    char name[DATA_NAME_LEN];
    char slot[DATA_NAME_LEN];
    int attack;
    int defense;
    int max_hp;
    int heal;
    int gold;
} ItemDef;

typedef struct
{
    int count;
    ItemDef entries[DATA_MAX_ITEM_TYPES];
} ItemDefs;

int data_load_sheet(const char *filename, SheetSpec *sheet);
int data_find_sheet_entry(const SheetSpec *sheet, const char *name);

int data_load_level(const char *filename, LevelData *level);
int data_level_find_legend_by_tile(const LevelData *level, const char *tile);
int data_level_set_tile(LevelData *level, int x, int y, const char *tile);
int data_level_is_walkable(const LevelData *level, int x, int y);

int data_load_enemy_defs(const char *filename, EnemyDefs *defs);
int data_find_enemy_def(const EnemyDefs *defs, const char *name);

int data_load_item_defs(const char *filename, ItemDefs *defs);
int data_find_item_def(const ItemDefs *defs, const char *name);

#endif
