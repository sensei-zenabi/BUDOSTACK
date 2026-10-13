#define GAME_ID "invaders"
#define GAME_NAME "Invaders"
#define GRID_W 38
#define GRID_H 20
#define WINDOW_W 580
#define WINDOW_H 410
#define MIN_W 400
#define MIN_H 310
#define START_PAUSED 1
#define GAME_HINT "Left/Right or A/D: move. Space/click: fire."
#define GAME_HELP "Shoot all 48 aliens before they reach you."
#include "game_ui.h"

static int player_x, invaders[4][12], offset_x, offset_y, direction, score;
static unsigned int frame;
static struct { int active, x, y; } bullet;
static unsigned int game_interval(void) { return 100000; }
static void game_reset(void)
{
    player_x = GRID_W / 2;
    bullet.active = 0;
    score = 0;
    frame = 0;
    offset_x = 3;
    offset_y = 1;
    direction = 1;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 12; ++c) invaders[r][c] = 1;
    }
    snprintf(status, sizeof(status), "Score: 0. Clear the formation!");
}
static int hit_invader(void)
{
    if (!bullet.active) return 0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 12; ++c) {
            if (!invaders[r][c] || bullet.x != offset_x + c * 3 || bullet.y != offset_y + r * 2) continue;
            invaders[r][c] = 0;
            bullet.active = 0;
            score += 10;
            finished = score == 480;
            effect(finished ? 4 : 2);
            return 1;
        }
    }
    return 0;
}
static void game_tick(void)
{
    if (bullet.active && !hit_invader()) {
        if (--bullet.y < 0) bullet.active = 0;
        else (void)hit_invader();
    }
    if (!finished && frame++ % 5 == 0) {
        int left = GRID_W, right = -1;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 12; ++c) {
                if (!invaders[r][c]) continue;
                int x = offset_x + c * 3;
                if (x < left) left = x;
                if (x > right) right = x;
            }
        }
        if (right < 0) finished = 1;
        else if ((direction > 0 && right + 1 >= GRID_W) || (direction < 0 && left == 0)) {
            ++offset_y;
            direction = -direction;
        } else offset_x += direction;
        (void)hit_invader();
        if (!finished) {
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 12; ++c) {
                    if (invaders[r][c] && offset_y + r * 2 >= GRID_H - 1) finished = 1;
                }
            }
            if (finished) effect(3);
        }
    }
    snprintf(status, sizeof(status), "Score: %d%s", score,
             finished ? (score == 480 ? " - You win! R: new game." : " - Game over. R: new game.") : "");
}
static void fire(void)
{
    if (bullet.active || finished || paused) return;
    bullet.active = 1;
    bullet.x = player_x;
    bullet.y = GRID_H - 2;
    effect(1);
}
static int game_key(int key)
{
    if (finished || paused) return 0;
    if (key == (0x100 | 75) || key == 'a' || key == 'A') {
        if (player_x > 0) --player_x;
    } else if (key == (0x100 | 77) || key == 'd' || key == 'D') {
        if (player_x < GRID_W - 1) ++player_x;
    } else if (key == ' ') fire();
    else return 0;
    return 1;
}
static void game_click(const Layout *l, int x, int y)
{
    (void)y;
    player_x = (x - l->bx) / l->cell;
    fire();
}
static void alien(int x, int y, int size, unsigned int rgb)
{
    static const unsigned char rows[] = {0x24,0x18,0x7e,0xdb,0xff,0xa5,0x24,0x42};
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            if (rows[r] & (1U << (7 - c))) {
                int w = (c + 1) * size / 8 - c * size / 8;
                int h = (r + 1) * size / 8 - r * size / 8;
                if (w && h) host_api->fill_rect_rgb(x + c * size / 8, y + r * size / 8, w, h, rgb);
            }
        }
    }
}
static void game_draw(const Layout *l)
{
    host_api->fill_rect_rgb(l->bx, l->by, GRID_W * l->cell, GRID_H * l->cell, 0x101830);
    for (int i = 0; i < 40; ++i)
        host_api->fill_rect_rgb(l->bx + (i * 97 % (GRID_W * l->cell)),
                                l->by + (i * 53 % (GRID_H * l->cell)), 1, 1, 0x607090);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 12; ++c) {
            if (invaders[r][c] && offset_y + r * 2 < GRID_H)
                alien(l->bx + (offset_x + c * 3) * l->cell, l->by + (offset_y + r * 2) * l->cell,
                      l->cell, r & 1 ? 0x60d8c0 : 0xe8b858);
        }
    }
    int px = l->bx + player_x * l->cell, py = l->by + (GRID_H - 1) * l->cell;
    host_api->fill_rect_rgb(px + l->cell / 3, py, l->cell / 3, l->cell, 0x90d8ff);
    host_api->fill_rect_rgb(px, py + l->cell / 2, l->cell, l->cell / 2, 0x90d8ff);
    if (bullet.active) host_api->fill_rect_rgb(l->bx + bullet.x * l->cell + l->cell / 2,
                                              l->by + bullet.y * l->cell, 2, l->cell, 0xffffa0);
}
static void game_icon(int x, int y)
{
    static const unsigned int palette[] = {0,0x101830,0x60d8c0,0xe8b858,0x90d8ff,0xffffff};
    static const char pixels[] =
        "0001111111111000" "0011111111111100" "0111511111111110" "0111111111115110"
        "0111121111211110" "0111112222111110" "0111122222211110" "0111221221221110"
        "0111222222221110" "0111212112121110" "0111121111211110" "0111111131111110"
        "0111111441111110" "0111114444111110" "0011144444411100" "0001111111111000";
    icon_sprite(x, y, pixels, palette);
}
