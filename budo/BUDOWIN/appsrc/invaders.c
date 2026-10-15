#define GAME_ID "invaders"
#define GAME_NAME "Invaders"
#define GRID_W 38
#define GRID_H 20
#define WINDOW_W 580
#define WINDOW_H 410
#define MIN_W 400
#define MIN_H 310
#define START_PAUSED 1
#define GAME_EXTRA_UI 1
#define GAME_HINT "Left/Right or A/D: move. Space/click: fire."
#define GAME_HELP "Clear 48 aliens. 1/2/3: level, 1/2/3 pts per kill."
#include "game_ui.h"

static int player_x, invaders[4][12], offset_x, offset_y, direction, score, remaining;
static int difficulty, pending_difficulty = -1;
static const char *difficulty_names[] = {"EASY", "MEDIUM", "HARD"};
/* Milliseconds per formation step, floor, and descent speed multiplier. */
static const unsigned int starting_period[] = {1200, 700, 400};
static const unsigned int minimum_period[] = {600, 300, 100};
static const unsigned int descent_multiplier[] = {19, 18, 17};
static unsigned int frame, formation_elapsed;
static unsigned int formation_interval(void)
{
    unsigned int period = starting_period[difficulty];
    for (int row = 1; row < offset_y; ++row) {
        period = period * descent_multiplier[difficulty] / 20;
    }
    return period < minimum_period[difficulty] ? minimum_period[difficulty] : period;
}
static struct { int active, x, y; } bullet;
static unsigned int game_interval(void) { return 100000; }
static void invaders_status(void)
{
    snprintf(status, sizeof(status), "%s Score: %d%s", difficulty_names[difficulty], score,
             finished ? (remaining == 0 ? " - You win! R: new game." : " - Game over. R: new game.") : "");
}
static void game_cancel_new(void) { pending_difficulty = -1; }
static void set_difficulty(int level)
{
    if (level == difficulty || confirming) return;
    pending_difficulty = level;
    if ((!frame && !bullet.active) || finished) new_game();
    else {
        request_new();
        if (!confirming) pending_difficulty = -1;
    }
}
static void game_toolbar_draw(const Layout *l)
{
    for (int i = 0; i < 3; ++i) {
        budo_button_draw(host_api, l->x + 6 + i * 68, l->y + 32, 64, 20, difficulty_names[i],
                         difficulty == i ? BUDO_BUTTON_PRESSED : 0);
    }
    text(l->x + 214, l->y + 38, "1/2/3: level", (l->w - 222) / 6);
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
    host_api->draw_text(l->x + 4, l->y + l->h - l->status_h + 3, line,
                        color(BUDO_SYS_COLOR_STATUS_TEXT), (l->w - 8) / 6);
}
static void game_reset(void)
{
    if (pending_difficulty >= 0) difficulty = pending_difficulty;
    pending_difficulty = -1;
    remaining = 48;
    player_x = GRID_W / 2;
    bullet.active = 0;
    score = 0;
    frame = 0;
    formation_elapsed = 0;
    offset_x = 3;
    offset_y = 1;
    direction = 1;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 12; ++c) invaders[r][c] = 1;
    }
    invaders_status();
}
static int hit_invader(void)
{
    if (!bullet.active) return 0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 12; ++c) {
            if (!invaders[r][c] || bullet.x != offset_x + c * 3 || bullet.y != offset_y + r * 2) continue;
            invaders[r][c] = 0;
            bullet.active = 0;
            score += difficulty + 1;
            finished = --remaining == 0;
            effect(finished ? 4 : 2);
            return 1;
        }
    }
    return 0;
}
static void game_tick(void)
{
    /* Check every crossed cell so faster shots cannot skip an alien. */
    for (int step = 0; step < 2 && bullet.active && !hit_invader(); ++step) {
        if (--bullet.y < 0) bullet.active = 0;
        else (void)hit_invader();
    }
    ++frame;
    formation_elapsed += 100;
    if (!finished && formation_elapsed >= formation_interval()) {
        formation_elapsed -= formation_interval();
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
    invaders_status();
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
    if (key >= '1' && key <= '3') { set_difficulty(key - '1'); return 1; }
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
