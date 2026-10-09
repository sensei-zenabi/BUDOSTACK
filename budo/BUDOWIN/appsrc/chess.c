#include "../sdk/ui.h"
#include "../../../lib/chess_engine.h"

#include <stdio.h>
#include <string.h>

#define HISTORY_SIZE 512

static const BwaHostApi *host_api;
static GameState game, positions[HISTORY_SIZE];
static Move moves_played[HISTORY_SIZE];
static int history_count, selected = -1, cursor = 52, menu = -1;
static int mode = MODE_PVP, difficulty = DIFF_MEDIUM, flipped, finished;
static int promotion, help_page, pending_mode, confirming;
static Move promotion_move;
static char status[96];
static BudoScrollbar history_scroll;
/* One root candidate per draw keeps the desktop processing input during search. */
static MoveList ai_moves;
static Move ai_best;
static int ai_index, ai_score, ai_active;
static unsigned int random_state = 1;

static unsigned char color(int role)
{
    return host_api->get_system_color(role);
}

static unsigned int random_number(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

typedef struct Layout {
    int x, y, w, h, menu_h, status_h;
    int bx, by, cell, sx, sw, list_y, list_h;
} Layout;

static int layout(Layout *l)
{
    if (!host_api->window_get_client_rect(&l->x, &l->y, &l->w, &l->h)) return 0;
    l->menu_h = host_api->get_system_metric(BUDO_SYS_METRIC_MENU_HEIGHT);
    l->status_h = host_api->get_system_metric(BUDO_SYS_METRIC_STATUS_HEIGHT);
    l->cell = (l->w - 200) / 8;
    int vertical = (l->h - l->menu_h - l->status_h - 34) / 8;
    if (l->cell > vertical) l->cell = vertical;
    l->bx = l->x + 20;
    l->by = l->y + l->menu_h + 18;
    l->sx = l->bx + l->cell * 8 + 22;
    l->sw = l->x + l->w - l->sx - 8;
    l->list_y = l->by + 168;
    l->list_h = l->y + l->h - l->status_h - l->list_y - 6;
    return l->cell >= 24;
}

static int insufficient_material(const GameState *s)
{
    int minors = 0, knights = 0, bishop_color = -1;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            char p = lower_piece(s->board[r][c]);
            if (p == ' ' || p == 'k') continue;
            if (p != 'b' && p != 'n') return 0;
            ++minors;
            if (p == 'n') ++knights;
            else if (bishop_color == -1) bishop_color = (r + c) & 1;
            else if (bishop_color != ((r + c) & 1)) return 0;
        }
    }
    return minors <= 1 || !knights;
}

/* An en-passant target counts only when a legal en-passant capture exists. */
static int effective_ep(const GameState *s)
{
    if (s->en_passant_row < 0) return -1;
    MoveList legal;
    generate_legal_moves(s, &legal);
    for (int i = 0; i < legal.count; ++i) {
        if (legal.items[i].en_passant) return s->en_passant_col;
    }
    return -1;
}

static int same_position(const GameState *a, const GameState *b)
{
    return memcmp(a->board, b->board, sizeof(a->board)) == 0 &&
        a->white_to_move == b->white_to_move &&
        a->white_castle_king == b->white_castle_king &&
        a->white_castle_queen == b->white_castle_queen &&
        a->black_castle_king == b->black_castle_king &&
        a->black_castle_queen == b->black_castle_queen && effective_ep(a) == effective_ep(b);
}

static void update_status(void)
{
    MoveList legal;
    generate_legal_moves(&game, &legal);
    finished = 1;
    if (!legal.count) {
        snprintf(status, sizeof(status), "%s", king_in_check(&game, game.white_to_move) ?
                 (game.white_to_move ? "Checkmate. Black wins." : "Checkmate. White wins.") : "Draw by stalemate.");
        return;
    }
    if (game.halfmove_clock >= 100 || insufficient_material(&game)) {
        snprintf(status, sizeof(status), "%s", game.halfmove_clock >= 100 ?
                 "Draw by fifty-move rule." : "Draw by insufficient material.");
        return;
    }
    int repeats = 1;
    for (int i = history_count - 1; i >= 0 && i >= history_count - game.halfmove_clock; --i) {
        if (same_position(&game, &positions[i])) ++repeats;
    }
    if (repeats >= 3) {
        snprintf(status, sizeof(status), "Draw by threefold repetition.");
        return;
    }
    finished = 0;
    snprintf(status, sizeof(status), "%s to move%s", game.white_to_move ? "White" : "Black",
             king_in_check(&game, game.white_to_move) ? " - Check!" : ".");
}

static void new_game(void)
{
    init_game(&game);
    history_count = promotion = help_page = ai_active = finished = confirming = 0;
    selected = -1;
    cursor = 52;
    menu = -1;
    history_scroll = (BudoScrollbar){0};
    update_status();
}

static void play_move(Move move)
{
    if (history_count == HISTORY_SIZE) {
        memmove(positions, positions + 1, (HISTORY_SIZE - 1) * sizeof(*positions));
        memmove(moves_played, moves_played + 1, (HISTORY_SIZE - 1) * sizeof(*moves_played));
        --history_count;
    }
    positions[history_count] = game;
    moves_played[history_count++] = move;
    (void)make_move(&game, move);
    selected = -1;
    promotion = ai_active = 0;
    history_scroll.position = history_count;
    update_status();
}

static void undo(void)
{
    if (!history_count) return;
    ai_active = promotion = 0;
    game = positions[--history_count];
    if (mode == MODE_PVC && !game.white_to_move && history_count)
        game = positions[--history_count];
    selected = -1;
    update_status();
}

static void ai_step(void)
{
    if (mode != MODE_PVC || game.white_to_move || finished || promotion || help_page || confirming) return;
    if (!ai_active) {
        generate_legal_moves(&game, &ai_moves);
        if (!ai_moves.count) return;
        if (difficulty == DIFF_EASY) {
            play_move(ai_moves.items[random_number() % (unsigned int)ai_moves.count]);
            return;
        }
        sort_moves(&game, &ai_moves);
        ai_index = 0;
        ai_score = INF_SCORE;
        ai_best = ai_moves.items[0];
        ai_active = 1;
        snprintf(status, sizeof(status), "Computer is thinking...");
        return;
    }
    GameState copy = game;
    Move candidate = ai_moves.items[ai_index++];
    (void)make_move(&copy, candidate);
    int score = minimax(&copy, difficulty == DIFF_HARD ? 2 : 0, -INF_SCORE, INF_SCORE);
    if (score < ai_score || (score == ai_score && (random_number() & 1))) {
        ai_score = score;
        ai_best = candidate;
    }
    if (ai_index == ai_moves.count) play_move(ai_best);
}

static void select_square(int square)
{
    if (finished || promotion || (mode == MODE_PVC && !game.white_to_move)) return;
    int row = square / 8, col = square % 8;
    MoveList legal;
    generate_legal_moves(&game, &legal);
    for (int i = 0; i < legal.count && selected >= 0; ++i) {
        Move move = legal.items[i];
        if (move.from_row * 8 + move.from_col == selected && move.to_row == row && move.to_col == col) {
            if (move.promotion) {
                promotion_move = move;
                promotion = 1;
                snprintf(status, sizeof(status), "Promote pawn: choose Q, R, B or N.");
            } else play_move(move);
            return;
        }
    }
    if (selected == square) selected = -1;
    else if (piece_color(game.board[row][col]) == (game.white_to_move ? 1 : -1)) selected = square;
    else {
        snprintf(status, sizeof(status), "Illegal move. Select a piece and destination.");
    }
}

/* Hand-drawn 16x20 monochrome sprites: silhouette plus inset white fill. */
static const unsigned short sprites[6][20] = {
    {0,0,0x03c0,0x07e0,0x07e0,0x07e0,0x03c0,0x0180,0x03c0,0x07e0,0x03c0,0x03c0,0x03c0,0x07e0,0x0ff0,0x0ff0,0x1ff8,0x1ff8,0,0},
    {0,0x0180,0x07c0,0x0fe0,0x1ff0,0x3df0,0x3ff0,0x3ff0,0x01f0,0x03f0,0x07e0,0x0fe0,0x0fc0,0x0fc0,0x0ff0,0x0ff0,0x1ff8,0x1ff8,0,0},
    {0,0x0180,0x03c0,0x07e0,0x0e70,0x0cf0,0x0ff0,0x07e0,0x03c0,0x07e0,0x03c0,0x03c0,0x07e0,0x07e0,0x0ff0,0x0ff0,0x1ff8,0x1ff8,0,0},
    {0,0,0x1db8,0x1db8,0x1ff8,0x1ff8,0x0ff0,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0x0ff0,0x0ff0,0x1ff8,0x1ff8,0,0},
    {0,0,0x2184,0x73ce,0x73ce,0x3ffc,0x1ff8,0x1ff8,0x0ff0,0x07e0,0x0ff0,0x07e0,0x07e0,0x07e0,0x0ff0,0x0ff0,0x1ff8,0x1ff8,0,0},
    {0,0x0180,0x0180,0x07e0,0x0180,0x0180,0x07e0,0x0ff0,0x0ff0,0x07e0,0x0ff0,0x07e0,0x07e0,0x07e0,0x0ff0,0x0ff0,0x1ff8,0x1ff8,0,0}
};

static void draw_piece(int x, int y, int cell, char piece)
{
    const char *kinds = "pnbrqk";
    const char *kind = strchr(kinds, lower_piece(piece));
    if (!kind || piece == ' ') return;
    int index = (int)(kind - kinds);
    int image_h = cell - 8;
    int image_w = image_h * 4 / 5;
    x += (cell - image_w) / 2;
    y += (cell - image_h) / 2;
    for (int r = 0; r < 20; ++r) {
        for (int c = 0; c < 16; ++c) {
            unsigned int bit = 1U << (15 - c);
            if (!(sprites[index][r] & bit)) continue;
            int inset = r > 0 && r < 19 && c > 0 && c < 15 &&
                (sprites[index][r - 1] & bit) && (sprites[index][r + 1] & bit) &&
                (sprites[index][r] & (bit << 1)) && (sprites[index][r] & (bit >> 1));
            unsigned int rgb = is_white_piece(piece) ? (inset ? 0xffffffU : 0x181818U) :
                (inset ? 0x303030U : 0xe0e0e0U);
            int px = c * image_w / 16, py = r * image_h / 20;
            int pw = (c + 1) * image_w / 16 - px;
            int ph = (r + 1) * image_h / 20 - py;
            if (pw && ph) host_api->fill_rect_rgb(x + px, y + py, pw, ph, rgb);
        }
    }
}

static void text(int x, int y, const char *label, int chars)
{
    host_api->draw_text(x, y, label, color(BUDO_SYS_COLOR_TEXT), chars);
}

static void history_configure(const Layout *l)
{
    history_scroll.x = l->sx + l->sw - BUDO_SCROLL_WIDTH;
    history_scroll.y = l->list_y;
    history_scroll.length = l->list_h;
    history_scroll.total = history_count;
    history_scroll.page = (l->list_h - 4) / 12;
    budo_scroll_clamp(&history_scroll);
}

static void draw_board(const Layout *l)
{
    MoveList legal;
    generate_legal_moves(&game, &legal);
    host_api->draw_sunken_panel(l->bx - 3, l->by - 3, l->cell * 8 + 6, l->cell * 8 + 6,
                                color(BUDO_SYS_COLOR_FACE));
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            int square = flipped ? 63 - (r * 8 + c) : r * 8 + c;
            int x = l->bx + c * l->cell, y = l->by + r * l->cell;
            host_api->fill_rect_rgb(x, y, l->cell, l->cell, ((r + c) & 1) ? 0x608080U : 0xe0dcc8U);
            if (square == selected) host_api->fill_rect(x + 1, y + 1, l->cell - 2, l->cell - 2,
                                                       color(BUDO_SYS_COLOR_SELECTION_BG));
            draw_piece(x, y, l->cell, game.board[square / 8][square % 8]);
            if (square == cursor) host_api->draw_rect(x + 2, y + 2, l->cell - 4, l->cell - 4,
                                                     color(BUDO_SYS_COLOR_ACCENT));
            if (history_count) {
                Move last = moves_played[history_count - 1];
                if (square == last.from_row * 8 + last.from_col || square == last.to_row * 8 + last.to_col)
                    host_api->draw_rect(x, y, l->cell, l->cell, color(BUDO_SYS_COLOR_ACCENT));
            }
            for (int i = 0; i < legal.count && selected >= 0; ++i) {
                Move m = legal.items[i];
                if (selected == m.from_row * 8 + m.from_col && square == m.to_row * 8 + m.to_col)
                    host_api->draw_rect(x + 4, y + 4, l->cell - 8, l->cell - 8, color(BUDO_SYS_COLOR_ACCENT));
            }
        }
        char label[2] = {(char)((flipped ? '1' : '8') + (flipped ? r : -r)), '\0'};
        text(l->bx - 13, l->by + r * l->cell + (l->cell - 8) / 2, label, 1);
        label[0] = (char)((flipped ? 'h' : 'a') + (flipped ? -r : r));
        text(l->bx + r * l->cell + (l->cell - 6) / 2, l->by + l->cell * 8 + 6, label, 1);
    }
    host_api->pointer_region(l->bx, l->by, l->cell * 8, l->cell * 8,
                             ai_active ? BUDO_CURSOR_BUSY : BUDO_CURSOR_ARROW,
                             "Select a piece, then a highlighted destination");
}

static void draw_sidebar(const Layout *l)
{
    text(l->sx, l->by, "Game mode", l->sw / 6);
    budo_button_draw(host_api, l->sx, l->by + 14, l->sw, 20, "Player vs. Player",
                     mode == MODE_PVP ? BUDO_BUTTON_PRESSED : 0);
    budo_button_draw(host_api, l->sx, l->by + 38, l->sw, 20, "Player vs Computer",
                     mode == MODE_PVC ? BUDO_BUTTON_PRESSED : 0);
    text(l->sx, l->by + 68, "Computer difficulty", l->sw / 6);
    const char *labels[] = {"Easy", "Medium", "Hard"};
    int bw = (l->sw - 4) / 3;
    for (int i = 0; i < 3; ++i) {
        budo_button_draw(host_api, l->sx + i * (bw + 2), l->by + 82, bw, 20, labels[i],
                         (mode == MODE_PVP ? BUDO_BUTTON_DISABLED : 0) |
                         (difficulty == i ? BUDO_BUTTON_PRESSED : 0));
    }
    budo_button_draw(host_api, l->sx, l->by + 112, l->sw / 2 - 2, 20, "New game", 0);
    budo_button_draw(host_api, l->sx + l->sw / 2 + 2, l->by + 112, l->sw / 2 - 2, 20, "Undo",
                     history_count ? 0 : BUDO_BUTTON_DISABLED);
    char turn[32];
    snprintf(turn, sizeof(turn), "%s / Move %d", game.white_to_move ? "White" : "Black", game.fullmove_number);
    text(l->sx, l->by + 140, turn, l->sw / 6);
    text(l->sx, l->by + 156, "Move history", l->sw / 6);
    history_configure(l);
    host_api->draw_sunken_panel(l->sx, l->list_y, l->sw - BUDO_SCROLL_WIDTH, l->list_h,
                                color(BUDO_SYS_COLOR_SURFACE));
    for (int row = 0; row < history_scroll.page; ++row) {
        int i = history_scroll.position + row;
        if (i >= history_count) break;
        Move m = moves_played[i];
        char label[40];
        snprintf(label, sizeof(label), "%d%c %c%c-%c%c%s%c", positions[i].fullmove_number,
                 positions[i].white_to_move ? '.' : ':', 'a' + m.from_col, '8' - m.from_row,
                 'a' + m.to_col, '8' - m.to_row, m.promotion ? "=" : "", m.promotion ? m.promotion : ' ');
        text(l->sx + 4, l->list_y + 3 + row * 12, label, (l->sw - BUDO_SCROLL_WIDTH - 8) / 6);
    }
    budo_scroll_draw(host_api, &history_scroll);
}

static void draw_overlay(const Layout *l)
{
    int x = l->x + (l->w - 330) / 2, y = l->by + 50;
    host_api->draw_sunken_panel(x, y, 330, help_page ? 184 : 80, color(BUDO_SYS_COLOR_FACE));
    if (promotion) {
        text(x + 12, y + 10, "Pawn promotion", 40);
        const char *labels[] = {"Queen", "Rook", "Bishop", "Knight"};
        for (int i = 0; i < 4; ++i)
            budo_button_draw(host_api, x + 9 + i * 78, y + 38, 76, 24, labels[i], i == 0 ? BUDO_BUTTON_DEFAULT : 0);
    } else {
        const char *lines[] = {"CHESS - BUDOWIN", "Classic chess for two players or computer.",
            "Computer plays Black. You play White.", "Click a piece, then its destination.",
            "Arrows: move cursor. Enter/Space: select.", "Ctrl+N: new game. Ctrl+Z: undo turn.",
            "F: flip board. F1: help. Esc: dismiss.", "Promotion: Q, R, B or N. Enter: queen.",
            "Draws: repetition, 50 moves, material."};
        for (int i = 0; i < 9; ++i) text(x + 12, y + 10 + i * 16, lines[i], 51);
        budo_button_draw(host_api, x + 125, y + 156, 80, 20, "OK", BUDO_BUTTON_DEFAULT);
    }
}

static void chess_draw(void)
{
    Layout l;
    if (!layout(&l)) return;
    ai_step();
    budo_menu_bar_item(host_api, l.x, l.y, 42, "Game", menu == 0);
    budo_menu_bar_item(host_api, l.x + 44, l.y, 60, "Options", menu == 1);
    budo_menu_bar_item(host_api, l.x + 106, l.y, 42, "Help", menu == 2);
    draw_board(&l);
    draw_sidebar(&l);
    host_api->fill_rect(l.x, l.y + l.h - l.status_h, l.w, l.status_h, color(BUDO_SYS_COLOR_STATUS_BG));
    host_api->draw_text(l.x + 4, l.y + l.h - l.status_h + 3, status,
                        color(BUDO_SYS_COLOR_STATUS_TEXT), (l.w - 8) / 6);
    if (promotion || help_page) draw_overlay(&l);
    if (menu == 0) {
        BudoMenuItem items[] = {{"New game  Ctrl+N", 1, 0}, {"Undo turn Ctrl+Z", history_count > 0, 0}, {"Close", 1, 0}};
        budo_menu_items_draw(host_api, l.x, l.y + l.menu_h, 170, items, 3);
    } else if (menu == 1) {
        BudoMenuItem items[] = {{"Player vs. Player", 1, mode == MODE_PVP}, {"Player vs Computer", 1, mode == MODE_PVC},
            {"Easy", mode == MODE_PVC, difficulty == DIFF_EASY}, {"Medium", mode == MODE_PVC, difficulty == DIFF_MEDIUM},
            {"Hard", mode == MODE_PVC, difficulty == DIFF_HARD}, {"Flip board", 1, flipped}};
        budo_menu_items_draw(host_api, l.x + 44, l.y + l.menu_h, 170, items, 6);
    } else if (menu == 2) {
        const char *labels[] = {"How to play / About"};
        budo_menu_draw(host_api, l.x + 106, l.y + l.menu_h, 170, labels, 1);
    }
}

static void request_new_game(int next_mode)
{
    pending_mode = next_mode;
    if (!history_count || finished) {
        mode = next_mode;
        new_game();
    } else if (!host_api->confirm_dialog(BUDO_CONFIRM_OVERWRITE, "New chess game?", "Replace the current game and start again?")) {
        snprintf(status, sizeof(status), "Unable to open new-game confirmation.");
    } else confirming = 1;
}

static void confirm_result(int kind, int response)
{
    confirming = 0;
    if (kind == BUDO_CONFIRM_OVERWRITE && response == BUDO_RESPONSE_SAVE) {
        mode = pending_mode;
        new_game();
    }
}

static void set_difficulty(int level)
{
    difficulty = level;
    ai_active = 0;
    update_status();
}

static void promote(int index)
{
    const char *pieces = game.white_to_move ? "QRBN" : "qrbn";
    promotion_move.promotion = pieces[index];
    play_move(promotion_move);
}

static int chess_mouse_down(int x, int y, int buttons)
{
    Layout l;
    if (!(buttons & 1) || !layout(&l)) return 0;
    if (promotion || help_page) {
        int ox = l.x + (l.w - 330) / 2, oy = l.by + 50;
        if (promotion && host_api->point_in_rect(x, y, ox + 9, oy + 38, 312, 24))
            promote((x - ox - 9) / 78);
        else if (help_page && host_api->point_in_rect(x, y, ox + 125, oy + 156, 80, 20)) help_page = 0;
        return 1;
    }
    if (host_api->point_in_rect(x, y, l.x, l.y, 148, l.menu_h)) {
        int next = x < l.x + 44 ? 0 : x < l.x + 106 ? 1 : 2;
        menu = menu == next ? -1 : next;
        return 1;
    }
    if (menu >= 0) {
        int old = menu;
        int item = budo_menu_hit(x, y, l.x + (old == 0 ? 0 : old == 1 ? 44 : 106), l.y + l.menu_h, 170,
                                 old == 0 ? 3 : old == 1 ? 6 : 1);
        menu = -1;
        if (old == 0 && item == 0) request_new_game(mode);
        else if (old == 0 && item == 1) undo();
        else if (old == 0 && item == 2) (void)host_api->window_close();
        else if (old == 1 && (item == 0 || item == 1)) request_new_game(item);
        else if (old == 1 && item >= 2 && item <= 4 && mode == MODE_PVC) set_difficulty(item - 2);
        else if (old == 1 && item == 5) flipped = !flipped;
        else if (old == 2 && item == 0) help_page = 1;
        return 1;
    }
    if (host_api->point_in_rect(x, y, l.sx, l.by + 14, l.sw, 20)) request_new_game(MODE_PVP);
    else if (host_api->point_in_rect(x, y, l.sx, l.by + 38, l.sw, 20)) request_new_game(MODE_PVC);
    else if (host_api->point_in_rect(x, y, l.sx, l.by + 82, l.sw, 20) && mode == MODE_PVC) {
        int i = (x - l.sx) / ((l.sw - 4) / 3 + 2);
        if (i < 3) set_difficulty(i);
    } else if (host_api->point_in_rect(x, y, l.sx, l.by + 112, l.sw, 20)) {
        if (x < l.sx + l.sw / 2) request_new_game(mode);
        else undo();
    } else if (host_api->point_in_rect(x, y, l.bx, l.by, l.cell * 8, l.cell * 8)) {
        int square = (y - l.by) / l.cell * 8 + (x - l.bx) / l.cell;
        cursor = flipped ? 63 - square : square;
        select_square(cursor);
    } else {
        history_configure(&l);
        return budo_scroll_pointer_host(host_api, &history_scroll, x, y, BUDO_POINTER_DOWN);
    }
    return 1;
}

static int chess_mouse_move(int x, int y, int buttons)
{
    Layout l;
    if (!(buttons & 1) || menu >= 0 || promotion || help_page || !layout(&l)) return 0;
    history_configure(&l);
    return budo_scroll_pointer_host(host_api, &history_scroll, x, y, BUDO_POINTER_MOVE);
}

static int chess_mouse_up(int x, int y, int buttons)
{
    (void)buttons;
    return budo_scroll_pointer_host(host_api, &history_scroll, x, y, BUDO_POINTER_UP);
}

static int chess_key(int key)
{
    if (promotion) {
        const char *choices = "qrbn";
        const char *p = strchr(choices, key >= 'A' && key <= 'Z' ? key + 'a' - 'A' : key);
        if (key == 13) promote(0);
        else if (key > 0 && key < 128 && p && *p) promote((int)(p - choices));
        else if (key == 27) { promotion = 0; update_status(); }
        return 1;
    }
    if (help_page) {
        if (key == 27 || key == 13 || key == ' ') help_page = 0;
        return 1;
    }
    if (key == 27) {
        menu = -1;
        selected = -1;
        update_status();
        return 1;
    }
    if (key == 14) { request_new_game(mode); return 1; }
    if (key == 26) { undo(); return 1; }
    if (key == 'f' || key == 'F') { flipped = !flipped; return 1; }
    if (key == (0x100 | 59)) { help_page = 1; menu = -1; return 1; }
    if (key == 13 || key == ' ') { select_square(cursor); return 1; }
    if (key >= 0x100) {
        int scan = key & 255;
        int display = flipped ? 63 - cursor : cursor;
        if (scan == 72 && display >= 8) display -= 8;
        else if (scan == 80 && display < 56) display += 8;
        else if (scan == 75 && display % 8) --display;
        else if (scan == 77 && display % 8 < 7) ++display;
        else if (scan == 201 || scan == 202 || scan == 73 || scan == 81) {
            history_scroll.position += scan == 201 ? -3 : scan == 202 ? 3 :
                scan == 73 ? -history_scroll.page : history_scroll.page;
            budo_scroll_clamp(&history_scroll);
            return 1;
        } else return 0;
        cursor = flipped ? 63 - display : display;
        return 1;
    }
    return 0;
}

static int chess_open(void)
{
    mode = MODE_PVP;
    difficulty = DIFF_MEDIUM;
    flipped = 0;
    random_state = (unsigned int)host_api->get_time_ms() | 1U;
    new_game();
    if (!host_api->window_create(34, 36, 570, 414, "Chess", BUDO_WINDOW_DEFAULT_BUTTONS)) return 0;
    return host_api->window_set_min_size(500, 350);
}

static void chess_icon(int x, int y)
{
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c)
            host_api->fill_rect_rgb(x + c * 8, y + r * 8, 8, 8, ((r + c) & 1) ? 0x608080U : 0xe0dcc8U);
    }
    draw_piece(x, y, 32, 'N');
}

int bwa_entry(const BwaHostApi *host, BwaAppDefinition *app)
{
    if (!host || !app || host->abi_major != BWA_ABI_MAJOR || host->abi_minor < 14 ||
        !host->window_create || !host->window_get_client_rect || !host->window_set_min_size ||
        !host->window_close || !host->get_system_color || !host->get_system_metric ||
        !host->fill_rect || !host->fill_rect_rgb || !host->draw_rect || !host->draw_text ||
        !host->draw_standard_button || !host->draw_button_state || !host->draw_sunken_panel ||
        !host->point_in_rect || !host->confirm_dialog || !host->pointer_region || !host->get_time_ms) return 0;
    host_api = host;
    memset(app, 0, sizeof(*app));
    app->app_id = "chess";
    app->name = "Chess";
    app->callbacks.open = chess_open;
    app->callbacks.draw = chess_draw;
    app->callbacks.draw_icon = chess_icon;
    app->callbacks.mouse_down = chess_mouse_down;
    app->callbacks.mouse_move = chess_mouse_move;
    app->callbacks.mouse_up = chess_mouse_up;
    app->callbacks.key = chess_key;
    app->confirm_result = confirm_result;
    return 1;
}
