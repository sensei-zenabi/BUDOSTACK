#if defined(TEST_SNAKE)
#include "../budo/BUDOWIN/appsrc/snake.c"
#elif defined(TEST_INVADERS)
#include "../budo/BUDOWIN/appsrc/invaders.c"
#else
#include "../budo/BUDOWIN/appsrc/tictactoe.c"
#endif
#include <assert.h>

static int client_w = WINDOW_W - 8, client_h = WINDOW_H - 26;
static unsigned long long clock_ms = 123;
static int tone_calls, stops, audio_ok = 1, close_calls, confirm_calls;
static unsigned long long now(void) { return clock_ms; }
static int client(int *x, int *y, int *w, int *h)
{
    *x = 0; *y = 0; *w = client_w; *h = client_h;
    return 1;
}
static void bounds(int x, int y, int w, int h)
{
    assert(x >= 0 && y >= 0 && w > 0 && h > 0);
    assert(x + w <= client_w && y + h <= client_h);
}
static void fill(int x, int y, int w, int h, unsigned char c)
{
    (void)c;
    bounds(x, y, w, h);
}
static void rgb(int x, int y, int w, int h, unsigned int c)
{
    (void)c;
    bounds(x, y, w, h);
}
static void label(int x, int y, const char *s, unsigned char c, int n)
{
    (void)c;
    assert(s && n > 0);
    bounds(x, y, 1, 8);
}
static void button(int x, int y, int w, int h, const char *s, int state)
{
    (void)s; (void)state;
    bounds(x, y, w, h);
}
static void button_state(int x, int y, int w, int h, const char *s, unsigned int state)
{
    button(x, y, w, h, s, (int)state);
}
static unsigned char test_color(int role) { return (unsigned char)role; }
static int metric(int role) { return role == BUDO_SYS_METRIC_STATUS_HEIGHT ? 16 : 18; }
static int inside(int x, int y, int bx, int by, int w, int h)
{
    return x >= bx && y >= by && x < bx + w && y < by + h;
}
static int create(int x, int y, int w, int h, const char *s, unsigned int flags)
{
    (void)x; (void)y; (void)flags;
    assert(w == WINDOW_W && h == WINDOW_H && !strcmp(s, GAME_NAME));
    return 1;
}
static int minimum(int w, int h) { assert(w == MIN_W && h == MIN_H); return 1; }
static int close_window(void) { ++close_calls; return 1; }
static int confirm(int k, const char *title, const char *message)
{
    assert(k == BUDO_CONFIRM_OVERWRITE && title && message);
    ++confirm_calls;
    return 1;
}
static int tones(const BwaTone *notes, unsigned int count)
{
    if (!count) { ++stops; return 1; }
    assert(notes && count <= 8);
    unsigned int total = 0;
    for (unsigned int i = 0; i < count; ++i) {
        assert(notes[i].frequency_hz <= 4000 && notes[i].duration_ms > 0 && notes[i].duration_ms <= 1000);
        total += notes[i].duration_ms;
    }
    assert(total <= 2000);
    ++tone_calls;
    return audio_ok;
}
static BwaHostApi test_host = {
    .abi_major = BWA_ABI_MAJOR, .abi_minor = BWA_ABI_MINOR,
    .fill_rect = fill, .draw_rect = fill, .fill_rect_rgb = rgb, .draw_text = label,
    .point_in_rect = inside, .get_system_color = test_color, .get_system_metric = metric,
    .draw_standard_button = button, .draw_button_state = button_state, .draw_sunken_panel = fill,
    .window_create = create, .window_set_min_size = minimum, .window_get_client_rect = client,
    .play_tones = tones, .window_close = close_window, .get_time_ms = now, .confirm_dialog = confirm
};
static void common_checks(void)
{
    BwaAppDefinition app;
    assert(bwa_entry(&test_host, &app) && !strcmp(app.app_id, GAME_ID));
    assert(app.callbacks.open() && paused && tone_calls == 1);
    int before = tone_calls;
    draw(); clock_ms += 10000; draw();
    assert(tone_calls == before); /* No simulation or sound while paused. */
    key(' '); assert(!paused);
    request_new(); assert(confirming && paused && confirm_calls == 1);
    confirm_result(BUDO_CONFIRM_OVERWRITE, BUDO_RESPONSE_CANCEL);
    assert(!confirming && !paused);
    request_new(); confirm_result(BUDO_CONFIRM_OVERWRITE, BUDO_RESPONSE_SAVE);
    assert(!confirming && paused);
    for (int size = 0; size < 3; ++size) {
        client_w = size == 0 ? MIN_W - 8 : size == 1 ? WINDOW_W - 8 : 800;
        client_h = size == 0 ? MIN_H - 26 : size == 1 ? WINDOW_H - 26 : 600;
        draw(); help_page = 1; draw(); help_page = 0;
        game_icon(0, 0);
    }
    key('m'); assert(!sound_enabled && stops > 0);
    before = tone_calls; effect(2); assert(tone_calls == before);
    key('M'); assert(sound_enabled);
    audio_ok = 0; effect(1); assert(sound_failed);
    before = tone_calls; effect(1); assert(tone_calls == before);
    audio_ok = 1; sound_failed = 0;
    test_host.abi_minor = 14;
    before = tone_calls; new_game(); assert(tone_calls == before);
    assert(bwa_entry(&test_host, &app));
    test_host.abi_minor = BWA_ABI_MINOR;
    test_host.get_time_ms = NULL; assert(!bwa_entry(&test_host, &app)); test_host.get_time_ms = now;
    key('q'); assert(close_calls == 1);
    app.callbacks.close();
}
static void rules_checks(void)
{
    new_game(); paused = 0;
#if defined(TEST_SNAKE)
    fruit = (Point){11, 10}; game_tick(); assert(length == 4 && snake[0].x == 11);
    for (int i = 0; i < length; ++i) assert(fruit.x != snake[i].x || fruit.y != snake[i].y);
    key('a'); assert(pending_direction == 3); /* Reject reverse. */
    key('w'); key('a'); assert(turn_count == 2 && pending_direction == 2);
    game_tick(); assert(snake[0].y == 9 && direction == 0);
    game_tick(); assert(snake[0].x == 10 && direction == 2 && !turn_count);
    snake[0] = (Point){19, 10}; direction = pending_direction = 3;
    game_tick(); assert(finished && strstr(status, "Game over"));
    new_game(); paused = 0;
    length = 4;
    snake[0] = (Point){2,2}; snake[1] = (Point){2,3}; snake[2] = (Point){3,3}; snake[3] = (Point){3,2};
    fruit = (Point){0,0}; game_tick(); assert(!finished && snake[0].x == 3); /* Vacating tail is legal. */
    direction = pending_direction = 2; game_tick(); assert(finished); /* Body collision. */
    new_game(); paused = 0; length = 99;
    for (int i = 0; i < 99; ++i) snake[i] = (Point){i % 20, i / 20};
    snake[0] = (Point){10,10}; fruit = (Point){11,10}; game_tick(); assert(length == 100 && finished);
#elif defined(TEST_INVADERS)
    key(' '); assert(bullet.active && bullet.x == player_x);
    int bx = bullet.x; key('d'); key(' '); assert(bullet.x == bx); /* Single bullet. */
    bullet.x = offset_x; bullet.y = offset_y; game_tick(); assert(score == 1 && !invaders[0][0]);
    new_game(); paused = 0;
    bullet.active = 1; bullet.x = offset_x; bullet.y = offset_y + 1;
    game_tick(); assert(score == 1); /* Collision after bullet motion. */
    new_game(); paused = 0;
    bullet.active = 1; bullet.x = offset_x; bullet.y = offset_y + 2;
    game_tick(); assert(score == 1 && !bullet.active); /* Second crossed cell. */
    for (int y = 0; y < 2; ++y) {
        new_game(); paused = 0;
        bullet.active = 1; bullet.x = 0; bullet.y = y;
        game_tick(); assert(!bullet.active && bullet.y == -1 && score == 0);
    }
    new_game(); paused = 0;
    offset_x = 4; formation_elapsed = formation_interval() - 100; game_tick(); assert(offset_y == 2 && direction == -1);
    new_game(); paused = 0;
    memset(invaders, 0, sizeof(invaders)); invaders[0][0] = 1;
    score = 47; remaining = 1; bullet.active = 1; bullet.x = offset_x; bullet.y = offset_y;
    game_tick(); assert(finished && score == 48 && strstr(status, "You win"));
    new_game(); paused = 0;
    offset_y = 13; offset_x = 4; formation_elapsed = formation_interval() - 100; game_tick(); assert(finished && strstr(status, "Game over"));
    new_game(); paused = 0;
    Layout l; assert(layout(&l)); game_click(&l, l.bx, l.by);
    assert(player_x == 0 && bullet.active && bullet.x == 0);
#else
    mode = 2;
    for (int d = 0; d < 4; ++d) {
        new_game(); paused = 0;
        static const int dr[] = {0,1,1,1}, dc[] = {1,0,1,-1};
        for (int i = 0; i < 4; ++i) board[4 + i * dr[d]][8 + i * dc[d]] = 'X';
        place((4 + 4 * dr[d]) * 17 + 8 + 4 * dc[d]);
        assert(finished && strstr(status, "X wins"));
    }
    new_game(); paused = 0; place(144); int count = move_count; place(144);
    assert(move_count == count && board[8][8] == 'X');
    new_game(); mode = 1; human_marker = 'O'; paused = 0;
    game_tick(); assert(board[8][8] == 'X' && current_player == 'O');
    new_game(); mode = 1; human_marker = 'O'; paused = 0;
    for (int i = 0; i < 4; ++i) board[0][i] = 'O';
    move_count = 4; game_tick(); assert(board[0][4] == 'X'); /* Block immediate threat. */
    new_game(); mode = 3; paused = 0;
    for (int i = 0; i < 4; ++i) board[0][i] = 'X';
    move_count = 4; game_tick(); assert(finished && board[0][4] == 'X');
    new_game(); mode = 3; paused = 0;
    game_tick(); assert(move_count == 1);
    for (int i = 0; i < 100 && move_count == 1; ++i) game_tick();
    assert(move_count == 2 && !ai_active);
    new_game(); mode = 1; key('b'); assert(mode == 2); key('h'); assert(human_marker == 'X');
    paused = 0;
    Layout l; assert(layout(&l)); game_click(&l, l.bx + 2, l.by + 2); assert(board[0][0] == 'X');
    key('b'); assert(mode == 2); /* In-progress mode is stable. */
#endif
    new_game(); paused = 0;
    int before = tone_calls;
    draw(); assert(tone_calls == before); /* Drawing before the deadline is inert. */
    key('p'); assert(paused); key(0x100 | 59); assert(help_page);
    clock_ms += 100000; draw(); key(27); assert(!help_page && paused);
}
static void timing_checks(void)
{
    BwaAppDefinition app;
    assert(bwa_entry(&test_host, &app));
    assert((app.flags & BWA_FLAG_IDLE_TICK) && app.tick);
    new_game();
    key(' ');
#if defined(TEST_SNAKE)
    fruit = (Point){0, 0};
    int head_x = snake[0].x;
    clock_ms += 199;
    assert(!app.tick(1));
    for (int i = 0; i < 200; ++i) { key(0x100 | 77); draw(); }
    assert(snake[0].x == head_x); /* Redraws/auto-repeat cannot advance time. */
    ++clock_ms;
    assert(app.tick(1) && snake[0].x == head_x + 1);
    for (int i = 0; i < 200; ++i) { key(0x100 | 77); assert(!app.tick(1)); }
    clock_ms += 600;
    assert(app.tick(1) && snake[0].x == head_x + 4); /* Elapsed time catches up. */
    clock_ms += 10000;
    assert(!app.tick(0));
    assert(!app.tick(1) && snake[0].x == head_x + 4); /* Minimize cannot burst on restore. */
    for (int level = 0; level < 3; ++level) {
        difficulty = level;
        new_game();
        paused = 0;
        unsigned int previous = interval;
        for (int apples = 0; apples < 97; ++apples) {
            length = 3 + apples;
            for (int i = 0; i < length; ++i) snake[i] = (Point){i % 20, i / 20};
            snake[0] = (Point){10, 10};
            direction = pending_direction = 3;
            fruit = (Point){11, 10};
            game_tick();
            assert(interval < previous && snake_score() == (apples + 1) * (level + 1));
            previous = interval;
        }
        assert(finished);
    }
    difficulty = 0;
    new_game();
    Layout l;
    assert(layout(&l));
    assert(game_toolbar_click(&l, l.x + 145, l.y + 35));
    assert(difficulty == 2 && paused && !snake_score());
    key(' ');
    fruit = (Point){0, 0};
    game_tick();
    set_difficulty(1);
    assert(confirming && difficulty == 2 && pending_difficulty == 1);
    confirm_result(BUDO_CONFIRM_OVERWRITE, BUDO_RESPONSE_CANCEL);
    assert(difficulty == 2 && pending_difficulty == -1);
    set_difficulty(1);
    confirm_result(BUDO_CONFIRM_OVERWRITE, BUDO_RESPONSE_SAVE);
    assert(difficulty == 1 && paused && !snake_score() && interval == 133333);
#elif defined(TEST_INVADERS)
    int original = offset_y;
    clock_ms += 600;
    assert(app.tick(1) && offset_x == 3 && offset_y == original);
    clock_ms += 600;
    assert(app.tick(1));
    assert(offset_x != 3 || offset_y != original);
    int frame_before = (int)frame;
    for (int i = 0; i < 100; ++i) { draw(); assert(!app.tick(1)); }
    assert((int)frame == frame_before);
#else
    mode = 3;
    new_game();
    paused = 0;
    clock_ms += 250;
    assert(app.tick(1) && move_count == 1);
    for (int i = 0; i < 100 && move_count < 2; ++i) {
        clock_ms += 16;
        (void)app.tick(1);
    }
    assert(move_count >= 2); /* Computer demo advances without any input. */
#endif
    test_host.abi_minor = 15;
    assert(bwa_entry(&test_host, &app) && !(app.flags & BWA_FLAG_IDLE_TICK));
    union { max_align_t alignment; unsigned char bytes[sizeof(BwaAppDefinition) + 16]; } legacy;
    memset(legacy.bytes, 0xa5, sizeof(legacy.bytes));
    assert(bwa_entry(&test_host, (BwaAppDefinition *)legacy.bytes));
    for (size_t i = offsetof(BwaAppDefinition, tick); i < sizeof(legacy.bytes); ++i) assert(legacy.bytes[i] == 0xa5);
    test_host.abi_minor = BWA_ABI_MINOR;
}
#if defined(TEST_INVADERS)
static void invaders_difficulty_checks(void)
{
    unsigned int previous_start = 0, previous_floor = 0, previous_lifetime = 0;
    for (int level = 0; level < 3; ++level) {
        difficulty = level;
        new_game();
        assert(paused && score == 0 && remaining == 48);
        assert(strstr(status, difficulty_names[level]));
        unsigned int start = formation_interval();
        offset_y = GRID_H;
        unsigned int floor = formation_interval();
        assert(start > floor && floor >= 100);
        if (level) assert(start < previous_start && floor < previous_floor);
        previous_start = start;
        previous_floor = floor;
        new_game();
        paused = 0;
        while (!finished && frame < 2000) game_tick();
        assert(finished && remaining == 48 && strstr(status, "Game over"));
        if (level) assert(frame < previous_lifetime);
        previous_lifetime = frame;
        new_game();
        paused = 0;
        /* Clear the actual formation: score and victory must work at every level. */
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 12; ++c) {
                bullet.active = 1;
                bullet.x = offset_x + c * 3;
                bullet.y = offset_y + r * 2;
                assert(hit_invader());
                assert(score == (48 - remaining) * (level + 1));
                assert(finished == (remaining == 0));
            }
        }
        invaders_status();
        assert(score == 48 * (level + 1) && strstr(status, "You win"));
        new_game();
        assert(difficulty == level && !score && remaining == 48 && !finished);
    }
    difficulty = 0;
    new_game();
    Layout l;
    assert(layout(&l));
    mouse_down(l.x + 145, l.y + 35, 1);
    assert(difficulty == 2 && paused && !confirming);
    key('2');
    assert(difficulty == 1 && paused);
    key(' ');
    key(' '); /* A shot before the first tick also counts as progress. */
    assert(bullet.active && !frame);
    key('1');
    assert(confirming && paused && difficulty == 1 && pending_difficulty == 0);
    key('3');
    assert(pending_difficulty == 0);
    confirm_result(BUDO_CONFIRM_OVERWRITE, BUDO_RESPONSE_CANCEL);
    assert(!confirming && !paused && difficulty == 1 && pending_difficulty == -1 && bullet.active);
    game_tick();
    unsigned int saved_frame = frame;
    key('p');
    key('3');
    confirm_result(BUDO_CONFIRM_OVERWRITE, BUDO_RESPONSE_CANCEL);
    assert(paused && difficulty == 1 && frame == saved_frame);
    key('3');
    confirm_result(BUDO_CONFIRM_OVERWRITE, BUDO_RESPONSE_SAVE);
    assert(paused && difficulty == 2 && pending_difficulty == -1 && !frame && !bullet.active && !score);
    finished = 1;
    key('1');
    assert(difficulty == 0 && paused && !finished && !confirming);
}
static void invaders_playability_checks(void)
{
    /* Ordinary keyboard input at 200/300 ms intervals, with no trajectory
       prediction, teleporting, or changes to the formation or score. */
    for (int cadence = 2; cadence <= 3; ++cadence) {
        new_game();
        paused = 0;
        for (int steps = 0; !finished && steps < 2000; ++steps) {
            if (steps % cadence == 0) {
                int target = -1, distance = GRID_W;
                for (int r = 3; r >= 0 && target < 0; --r) {
                    for (int c = 0; c < 12; ++c) {
                        int x = offset_x + c * 3;
                        int delta = x > player_x ? x - player_x : player_x - x;
                        if (invaders[r][c] && delta < distance) {
                            target = x;
                            distance = delta;
                        }
                    }
                }
                if (target < player_x) key('a');
                else if (target > player_x) key('d');
                key(' ');
            }
            clock_ms += 100;
            assert(tick(1));
        }
        assert(finished && score == 48 && strstr(status, "You win"));
        assert(!bullet.active);
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 12; ++c) assert(!invaders[r][c]);
        }
    }
    /* Slower descent still ends an unattended game. */
    new_game();
    paused = 0;
    while (!finished && frame < 2000) game_tick();
    assert(finished && score == 0 && strstr(status, "Game over"));
    assert(frame >= 450 && frame < 600);
}
#endif
static void behavior_checks(void)
{
#if defined(TEST_SNAKE)
    for (int level = 0; level < 3; ++level) {
        difficulty = level;
        new_game();
        assert(interval == 10000000U / (unsigned int)starting_speed[level]);
    }
    new_game(); paused = 0;
    key('w'); key('w'); key('a'); key('s');
    assert(turn_count == 2 && turns[0] == 0 && turns[1] == 2);
    fruit = (Point){0, 0};
    game_tick(); assert(direction == 0);
    game_tick(); assert(direction == 2);
    key('d'); assert(!turn_count); /* No reversal into neck. */
#elif defined(TEST_INVADERS)
    new_game(); paused = 0;
    for (int i = 0; i < 11; ++i) game_tick();
    assert(offset_x == 3 && offset_y == 1 && formation_interval() == 1200);
    game_tick(); assert(offset_x == 4);
    formation_elapsed = formation_interval() - 100;
    game_tick(); assert(offset_y == 2 && formation_interval() == 1140);
    unsigned int before = formation_interval();
    for (offset_y = 3; offset_y < GRID_H; ++offset_y) {
        assert(formation_interval() <= before && formation_interval() >= 600);
        before = formation_interval();
    }
    assert(formation_interval() == 600);
    invaders_playability_checks();
    invaders_difficulty_checks();
#else
    for (int orientation = 0; orientation < 4; ++orientation) {
        new_game(); paused = 0; mode = 1; human_marker = 'X'; current_player = 'O';
        static const int dr[] = {1,0,1,1}, dc[] = {0,1,1,-1};
        for (int i = 0; i < 3; ++i) board[6 + i * dr[orientation]][8 + i * dc[orientation]] = 'X';
        board[0][0] = board[0][1] = 'O';
        move_count = 5;
        for (int steps = 0; steps < 32 && current_player == 'O'; ++steps) game_tick();
        assert(current_player == 'X');
        int r0 = 6 - dr[orientation], c0 = 8 - dc[orientation];
        int r1 = 6 + 3 * dr[orientation], c1 = 8 + 3 * dc[orientation];
        assert(board[r0][c0] == 'O' || board[r1][c1] == 'O'); /* Block the open three before an unstoppable four. */
    }
    new_game(); paused = 0; mode = 1; human_marker = 'X';
    for (int row = 5; row < 12 && !finished; ++row) {
        if (board[row][8] != ' ') continue;
        place(row * 17 + 8);
        for (int steps = 0; steps < 32 && current_player == 'O' && !finished; ++steps) game_tick();
        assert(check_winner(board) != 'X'); /* Reported straight-down opening. */
    }
#endif
}
int main(void)
{
    common_checks();
    rules_checks();
    timing_checks();
    behavior_checks();
    printf("PASS: %s rules, controls, timing, resized UI, icons, sound and legacy hosts\n", GAME_NAME);
    return 0;
}
