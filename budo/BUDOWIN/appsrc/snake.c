#define GAME_ID "snake"
#define GAME_NAME "Snake"
#define GRID_W 20
#define GRID_H 20
#define WINDOW_W 430
#define WINDOW_H 450
#define MIN_W 360
#define MIN_H 330
#define START_PAUSED 1
#define GAME_EXTRA_UI 1
#define GAME_HINT "Arrows/WASD: steer. Space: start."
#define GAME_HELP "Each apple adds 0.1/0.2/0.3 cells/s. 1/2/3: level."
#include "game_ui.h"

#define MAX_LENGTH 100
typedef struct Point { int x, y; } Point;
static Point snake[MAX_LENGTH], fruit;
static int length, direction, pending_direction, turn_queued;
static unsigned int interval;
static int difficulty, pending_difficulty = -1, steps_played;
static const char *difficulty_names[] = {"Easy", "Medium", "Hard"};
static const int dx[] = {0, 0, -1, 1}, dy[] = {-1, 1, 0, 0};

static unsigned int game_interval(void) { return interval ? interval : 100000; }
static int snake_score(void) { return (length - 3) * (difficulty + 1); }
static void snake_status(void)
{
    snprintf(status, sizeof(status), "%s%s Score: %d%s", finished ? (length == MAX_LENGTH ? "You win! " : "Game over. ") : "",
             difficulty_names[difficulty], snake_score(), finished ? " R: new game." : "");
}
static void game_cancel_new(void) { pending_difficulty = -1; }
static void set_difficulty(int level)
{
    if (level == difficulty || confirming) return;
    pending_difficulty = level;
    if (!steps_played || finished) new_game();
    else {
        request_new();
        if (!confirming) pending_difficulty = -1;
    }
}
static void game_toolbar_draw(const Layout *l)
{
    for (int i = 0; i < 3; ++i)
        budo_button_draw(host_api, l->x + 6 + i * 68, l->y + 32, 64, 20, difficulty_names[i],
                         difficulty == i ? BUDO_BUTTON_PRESSED : 0);
    text(l->x + 214, l->y + 38, "Space: start", (l->w - 222) / 6);
}
static int game_toolbar_click(const Layout *l, int x, int y)
{
    for (int i = 0; i < 3; ++i) {
        if (host_api->point_in_rect(x, y, l->x + 6 + i * 68, l->y + 32, 64, 20)) {
            set_difficulty(i);
            return 1;
        }
    }
    return 0;
}
static void game_status_draw(const Layout *l, const char *line)
{
    char speed[32];
    snprintf(speed, sizeof(speed), "Speed: %.1f", 10.0 + (length - 3) * (difficulty + 1) / 10.0);
    int width = (int)strlen(speed) * 6;
    host_api->draw_text(l->x + 4, l->y + l->h - l->status_h + 3, line,
                        color(BUDO_SYS_COLOR_STATUS_TEXT), (l->w - width - 16) / 6);
    host_api->draw_text(l->x + l->w - width - 4, l->y + l->h - l->status_h + 3, speed,
                        color(BUDO_SYS_COLOR_STATUS_TEXT), (int)strlen(speed));
}
static void place_fruit(void)
{
    Point empty[GRID_W * GRID_H];
    int count = 0;
    for (int y = 0; y < GRID_H; ++y) {
        for (int x = 0; x < GRID_W; ++x) {
            int occupied = 0;
            for (int i = 0; i < length; ++i) {
                if (snake[i].x == x && snake[i].y == y) occupied = 1;
            }
            if (!occupied) empty[count++] = (Point){x, y};
        }
    }
    if (count) fruit = empty[random_number() % (unsigned int)count];
}
static void game_reset(void)
{
    if (pending_difficulty >= 0) difficulty = pending_difficulty;
    pending_difficulty = -1;
    steps_played = 0;
    length = 3;
    direction = pending_direction = 3;
    turn_queued = 0;
    interval = 100000;
    for (int i = 0; i < length; ++i) snake[i] = (Point){10 - i, 10};
    place_fruit();
    snake_status();
}
static void game_tick(void)
{
    ++steps_played;
    direction = pending_direction;
    turn_queued = 0;
    Point head = {snake[0].x + dx[direction], snake[0].y + dy[direction]};
    int growing = head.x == fruit.x && head.y == fruit.y;
    int collision = head.x < 0 || head.x >= GRID_W || head.y < 0 || head.y >= GRID_H;
    for (int i = 0; i < length - !growing; ++i) {
        if (snake[i].x == head.x && snake[i].y == head.y) collision = 1;
    }
    if (collision) {
        finished = 1;
        snake_status();
        effect(3);
        return;
    }
    for (int i = length - !growing; i > 0; --i) snake[i] = snake[i - 1];
    snake[0] = head;
    if (growing) {
        ++length;
        /* Linear speed growth: +0.1 cells/s per apple, multiplied by level.
         * Microsecond deadlines retain the increase even below one millisecond. */
        interval = 10000000U / (unsigned int)(100 + (length - 3) * (difficulty + 1));
        if (length == MAX_LENGTH) finished = 1;
        else place_fruit();
        effect(finished ? 4 : 2);
    }
    snake_status();
}
static int game_key(int key)
{
    if (key >= '1' && key <= '3') { set_difficulty(key - '1'); return 1; }
    if (finished || paused) return 0;
    int next = -1;
    if (key == (0x100 | 72) || key == 'w' || key == 'W') next = 0;
    else if (key == (0x100 | 80) || key == 's' || key == 'S') next = 1;
    else if (key == (0x100 | 75) || key == 'a' || key == 'A') next = 2;
    else if (key == (0x100 | 77) || key == 'd' || key == 'D') next = 3;
    if (next < 0) return 0;
    /* One turn per simulation step prevents rapid keys reversing into the neck. */
    if (!turn_queued && next != (direction ^ 1) && next != direction) {
        pending_direction = next;
        turn_queued = 1;
    }
    return 1;
}
static void game_click(const Layout *l, int x, int y) { (void)l; (void)x; (void)y; }
static void game_draw(const Layout *l)
{
    host_api->fill_rect_rgb(l->bx, l->by, GRID_W * l->cell, GRID_H * l->cell, 0x102820);
    for (int r = 0; r < GRID_H; ++r) {
        for (int c = 0; c < GRID_W; ++c) {
            if ((r + c) & 1) host_api->fill_rect_rgb(l->bx + c * l->cell, l->by + r * l->cell,
                                                    l->cell, l->cell, 0x183428);
        }
    }
    int fx = l->bx + fruit.x * l->cell, fy = l->by + fruit.y * l->cell;
    if (!finished || length < MAX_LENGTH) {
        host_api->fill_rect_rgb(fx + 2, fy + 2, l->cell - 4, l->cell - 4, 0xef5548);
        host_api->fill_rect_rgb(fx + l->cell / 2, fy + 1, 2, 2, 0xa0d050);
    }
    for (int i = length - 1; i >= 0; --i) {
        int x = l->bx + snake[i].x * l->cell, y = l->by + snake[i].y * l->cell;
        host_api->fill_rect_rgb(x + 1, y + 1, l->cell - 2, l->cell - 2, i ? 0x60b848 : 0xb0e878);
        if (!i) {
            host_api->fill_rect_rgb(x + l->cell / 3, y + l->cell / 3, 2, 2, 0x102820);
            host_api->fill_rect_rgb(x + 2 * l->cell / 3, y + l->cell / 3, 2, 2, 0x102820);
        }
    }
}
static void game_icon(int x, int y)
{
    static const unsigned int palette[] = {0,0x102820,0x60b848,0xb0e878,0xef5548,0xffffff};
    static const char pixels[] =
        "0001111111111000" "0011111111111100" "0111111111111110" "0111111111144110"
        "0112222221144110" "0112333321111110" "0112355321111110" "0112333321111110"
        "0112222221111110" "0111111221111110" "0112222221111110" "0112211111111110"
        "0112222222222110" "0111111111111110" "0011111111111100" "0001111111111000";
    icon_sprite(x, y, pixels, palette);
}
