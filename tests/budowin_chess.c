#include "../budo/BUDOWIN/appsrc/chess.c"
#include <assert.h>
#include <time.h>

static int client_w = 564, client_h = 388, confirmations, close_count;
static unsigned long long clock_ms = 123;
static BwaTone heard[8];
static unsigned int heard_count;
static int sound_calls, sound_stops, audio_available = 1;
static int play_tones(const BwaTone *notes, unsigned int count)
{
    assert(count <= 8);
    if (!count) { ++sound_stops; return 1; }
    ++sound_calls;
    heard_count = count;
    memcpy(heard, notes, count * sizeof(*notes));
    return audio_available;
}
static void assert_effect(ChessSound effect)
{
    if (heard_count != sound_effects[effect].count) fprintf(stderr, "effect %d: heard %u, expected %u\n", effect, heard_count, sound_effects[effect].count);
    assert(heard_count == sound_effects[effect].count);
    for (unsigned int i = 0; i < heard_count; ++i) {
        assert(heard[i].frequency_hz == sound_effects[effect].notes[i].frequency_hz);
        assert(heard[i].duration_ms == sound_effects[effect].notes[i].duration_ms);
    }
}
static unsigned char test_color(int role) { return (unsigned char)role; }
static int metric(int role) { return role == BUDO_SYS_METRIC_MENU_HEIGHT ? 18 : 16; }
static int inside(int x, int y, int bx, int by, int w, int h)
{
    return x >= bx && y >= by && x < bx + w && y < by + h;
}
static int client(int *x, int *y, int *w, int *h)
{
    *x = 0; *y = 0; *w = client_w; *h = client_h;
    return 1;
}
static void bounds(int x, int y, int w, int h)
{
    assert(w > 0 && h > 0 && x >= 0 && y >= 0 && x + w <= client_w && y + h <= client_h);
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
    (void)s; (void)c; (void)n;
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
static void region(int x, int y, int w, int h, int c, const char *s)
{
    (void)c; (void)s;
    bounds(x, y, w, h);
}
static unsigned long long now(void) { unsigned long long value = clock_ms; clock_ms += 16; return value; }
static int create(int x, int y, int w, int h, const char *s, unsigned int flags)
{
    (void)x; (void)y; (void)w; (void)h; (void)s; (void)flags;
    return 1;
}
static int minimum(int w, int h) { assert(w == 500 && h == 350); return 1; }
static int close_window(void) { ++close_count; return 1; }
static int confirm(int k, const char *title, const char *message)
{
    assert(k == BUDO_CONFIRM_OVERWRITE && title && message);
    ++confirmations;
    return 1;
}
static BwaHostApi test_host = {
    .abi_major = BWA_ABI_MAJOR, .abi_minor = BWA_ABI_MINOR,
    .fill_rect = fill, .draw_rect = fill, .fill_rect_rgb = rgb, .draw_text = label,
    .point_in_rect = inside, .get_system_color = test_color, .get_system_metric = metric,
    .draw_standard_button = button, .draw_button_state = button_state, .draw_sunken_panel = fill,
    .window_create = create, .window_set_min_size = minimum, .window_get_client_rect = client,
    .play_tones = play_tones,
    .window_close = close_window, .pointer_region = region, .get_time_ms = now, .confirm_dialog = confirm
};

static unsigned long perft(const GameState *s, int depth)
{
    if (!depth) return 1;
    MoveList legal;
    generate_legal_moves(s, &legal);
    unsigned long total = 0;
    for (int i = 0; i < legal.count; ++i) {
        GameState next = *s;
        make_move(&next, legal.items[i]);
        total += perft(&next, depth - 1);
    }
    return total;
}

static GameState fen(const char *board, int white)
{
    GameState s = {0};
    memset(s.board, ' ', sizeof(s.board));
    int r = 0, c = 0;
    for (; *board; ++board) {
        if (*board == '/') { ++r; c = 0; }
        else if (*board >= '1' && *board <= '8') c += *board - '0';
        else s.board[r][c++] = *board;
    }
    s.white_to_move = white;
    s.en_passant_row = s.en_passant_col = -1;
    s.fullmove_number = 1;
    return s;
}

static Move find(const GameState *s, const char *notation)
{
    MoveList legal;
    generate_legal_moves(s, &legal);
    for (int i = 0; i < legal.count; ++i) {
        Move m = legal.items[i];
        if (m.from_col == notation[0] - 'a' && m.from_row == '8' - notation[1] &&
            m.to_col == notation[2] - 'a' && m.to_row == '8' - notation[3]) return m;
    }
    fprintf(stderr, "No legal move: %s\n", notation);
    abort();
}

static void move(const char *notation) { play_move(find(&game, notation)); }

static void rules_checks(void)
{
    GameState s;
    init_game(&s);
    assert(perft(&s, 1) == 20 && perft(&s, 2) == 400 && perft(&s, 3) == 8902 && perft(&s, 4) == 197281);
    s = fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R", 1);
    s.white_castle_king = s.white_castle_queen = s.black_castle_king = s.black_castle_queen = 1;
    assert(perft(&s, 1) == 48 && perft(&s, 2) == 2039 && perft(&s, 3) == 97862);
    Move castle = find(&s, "e1g1");
    assert(castle.castle);
    make_move(&s, castle);
    assert(s.board[7][6] == 'K' && s.board[7][5] == 'R' && !s.white_castle_king && !s.white_castle_queen);
    s = fen("k7/8/8/3pP3/8/8/8/7K", 1);
    s.en_passant_row = 2; s.en_passant_col = 3;
    Move ep = find(&s, "e5d6");
    assert(ep.en_passant);
    make_move(&s, ep);
    assert(s.board[3][3] == ' ' && s.board[2][3] == 'P');
    s = fen("k3r3/8/8/3pP3/8/8/8/4K3", 1);
    s.en_passant_row = 2; s.en_passant_col = 3;
    assert(effective_ep(&s) == -1); /* Capturing would expose the king. */
    s = fen("7k/P7/8/8/8/8/8/7K", 1);
    MoveList legal;
    generate_legal_moves(&s, &legal);
    int promotions = 0;
    for (int i = 0; i < legal.count; ++i) promotions += legal.items[i].promotion != 0;
    assert(promotions == 4);
}

static void game_checks(void)
{
    new_game();
    move("f2f3"); move("e7e5"); move("g2g4"); move("d8h4");
    assert(finished && strstr(status, "Checkmate. Black"));
    undo(); assert(!finished && !game.white_to_move);
    new_game();
    for (int i = 0; i < 2; ++i) {
        move("g1f3"); move("g8f6"); move("f3g1"); move("f6g8");
    }
    assert(finished && strstr(status, "threefold"));
    new_game();
    game = fen("7k/5K2/6Q1/8/8/8/8/8", 0);
    update_status(); assert(finished && strstr(status, "stalemate"));
    game = fen("7k/8/8/8/8/8/8/K7", 1);
    update_status(); assert(finished && strstr(status, "material"));
    game = fen("7k/8/8/8/8/8/8/KR6", 1);
    game.halfmove_clock = 100;
    update_status(); assert(finished && strstr(status, "fifty"));
    new_game(); game = fen("7k/P7/8/8/8/8/8/7K", 1);
    select_square(8); select_square(0);
    assert(promotion && game.board[1][0] == 'P');
    chess_key('N'); assert(!promotion && game.board[0][0] == 'N');
    new_game(); move("e2e4");
    request_new_game(MODE_PVC);
    assert(confirmations == 1 && history_count == 1 && confirming);
    confirm_result(BUDO_CONFIRM_OVERWRITE, BUDO_RESPONSE_CANCEL);
    assert(history_count == 1 && mode == MODE_PVP && !confirming);
    request_new_game(MODE_PVC);
    confirm_result(BUDO_CONFIRM_OVERWRITE, BUDO_RESPONSE_SAVE);
    assert(!history_count && mode == MODE_PVC);
}

static void computer_checks(void)
{
    for (int level = DIFF_EASY; level <= DIFF_HARD; ++level) {
        new_game(); mode = MODE_PVC; difficulty = level;
        move("e2e4");
        int steps = 0;
        while (!game.white_to_move && ++steps < 256) ai_step();
        assert(game.white_to_move && history_count == 2 && !king_in_check(&game, 0));
        undo(); assert(history_count == 0 && game.board[6][4] == 'P');
        move("e2e4"); ai_step(); undo();
        assert(!ai_active && history_count == 0 && game.white_to_move);
    }
    new_game(); mode = MODE_PVC; difficulty = DIFF_HARD;
    game = fen("6k1/8/8/8/8/6q1/5p2/7K", 0);
    update_status();
    for (int i = 0; i < 256 && !finished; ++i) ai_step();
    assert(finished && strstr(status, "Checkmate"));
}

static void ui_checks(void)
{
    mode = MODE_PVP;
    new_game();
    Layout l;
    assert(layout(&l));
    chess_mouse_down(l.bx + 4 * l.cell + 2, l.by + 6 * l.cell + 2, 1);
    assert(selected == 52);
    chess_mouse_down(l.bx + 4 * l.cell + 2, l.by + 4 * l.cell + 2, 1);
    assert(game.board[4][4] == 'P' && !game.white_to_move);
    chess_key(26); assert(history_count == 0);
    flipped = 1;
    cursor = 0; chess_key(0x100 | 75); assert(cursor == 1);
    flipped = 0;
    for (int size = 0; size < 3; ++size) {
        client_w = size == 0 ? 494 : size == 1 ? 564 : 634;
        client_h = size == 0 ? 324 : size == 1 ? 388 : 450;
        for (menu = -1; menu < 3; ++menu) chess_draw();
        menu = -1;
        help_page = 1; chess_draw(); help_page = 0;
        promotion = 1; chess_draw(); promotion = 0;
    }
}

static void sound_checks(void)
{
    mode = MODE_PVP;
    new_game(); assert_effect(SOUND_NEW);
    select_square(52); assert_effect(SOUND_SELECT);
    select_square(28); assert_effect(SOUND_ERROR); /* e2-e5 is illegal. */
    select_square(36); assert_effect(SOUND_MOVE);
    move("d7d5"); move("e4d5"); assert_effect(SOUND_CAPTURE);
    undo(); assert_effect(SOUND_UNDO);
    new_game();
    game = fen("7k/8/8/3pP3/8/8/8/K7", 1);
    game.en_passant_row = 2; game.en_passant_col = 3;
    move("e5d6"); assert_effect(SOUND_CAPTURE);
    new_game();
    game = fen("4k3/8/8/8/8/8/8/R3K2R", 1);
    game.white_castle_king = 1;
    move("e1g1"); assert_effect(SOUND_CASTLE);
    new_game();
    game = fen("7k/8/8/8/8/8/8/KR6", 1);
    move("b1b8"); assert_effect(SOUND_CHECK);
    new_game();
    game = fen("7k/P7/8/8/8/8/8/r6K", 1);
    select_square(8); select_square(0); promote(2); assert_effect(SOUND_PROMOTE);
    new_game();
    move("f2f3"); move("e7e5"); move("g2g4"); move("d8h4"); assert_effect(SOUND_MATE);
    new_game();
    for (int i = 0; i < 2; ++i) {
        move("g1f3"); move("g8f6"); move("f3g1"); move("f6g8");
    }
    assert_effect(SOUND_DRAW);
    new_game();
    int before = sound_calls;
    chess_draw(); chess_draw(); assert(sound_calls == before);
    chess_key('s'); assert(!sound_enabled && sound_stops > 0);
    move("e2e4"); assert(sound_calls == before);
    chess_key('S'); assert(sound_enabled); assert_effect(SOUND_SELECT);
    audio_available = 0;
    chess_sound(SOUND_MOVE); assert(sound_failed);
    before = sound_calls;
    chess_sound(SOUND_MOVE); assert(sound_calls == before);
    audio_available = 1; sound_failed = 0;
    unsigned short minor = test_host.abi_minor;
    test_host.abi_minor = 14;
    before = sound_calls;
    chess_sound(SOUND_MOVE); assert(sound_calls == before);
    assert(chess_open()); /* Backward-compatible ABI 1.14 game. */
    test_host.abi_minor = minor;
    chess_stop_sound();
}

int main(void)
{
    BwaAppDefinition app;
    assert(bwa_entry(&test_host, &app) && !strcmp(app.app_id, "chess"));
    assert(app.callbacks.open());
    rules_checks();
    game_checks();
    computer_checks();
    ui_checks();
    sound_checks();
    puts("PASS: chess perft, special moves, endings, promotion, AI levels, undo, confirmation, resized UI, event sounds, mute and legacy hosts");
    return 0;
}
