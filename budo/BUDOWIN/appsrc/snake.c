#define GAME_ID "snake"
#define GAME_NAME "Snake"
#define GRID_W 20
#define GRID_H 20
#define WINDOW_W 430
#define WINDOW_H 450
#define MIN_W 360
#define MIN_H 330
#define START_PAUSED 1
#define GAME_HINT "Arrows/WASD: steer. Space: start."
#define GAME_HELP "Eat apples. Avoid walls and your own tail."
#include "game_ui.h"

#define MAX_LENGTH 100
typedef struct Point { int x, y; } Point;
static Point snake[MAX_LENGTH], fruit;
static int length, direction, pending_direction, turn_queued;
static unsigned int interval;
static const int dx[] = {0, 0, -1, 1}, dy[] = {-1, 1, 0, 0};

static unsigned int game_interval(void) { return interval ? interval : 100; }
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
    length = 3;
    direction = pending_direction = 3;
    turn_queued = 0;
    interval = 100;
    for (int i = 0; i < length; ++i) snake[i] = (Point){10 - i, 10};
    place_fruit();
    snprintf(status, sizeof(status), "Score: 0. Reach 97 apples to win.");
}
static void game_tick(void)
{
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
        snprintf(status, sizeof(status), "Game over. Score: %d. R: new game.", length - 3);
        effect(3);
        return;
    }
    for (int i = length - !growing; i > 0; --i) snake[i] = snake[i - 1];
    snake[0] = head;
    if (growing) {
        ++length;
        if ((length - 3) % 5 == 0 && interval > 30) interval -= 10;
        if (length == MAX_LENGTH) finished = 1;
        else place_fruit();
        effect(finished ? 4 : 2);
    }
    snprintf(status, sizeof(status), "%sScore: %d%s", finished ? "You win! " : "", length - 3,
             finished ? ". R: new game." : "");
}
static int game_key(int key)
{
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
