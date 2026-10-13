#define GAME_ID "tictactoe"
#define GAME_NAME "Tic-Tac-Toe"
#define GRID_W 17
#define GRID_H 17
#define WINDOW_W 520
#define WINDOW_H 450
#define MIN_W 400
#define MIN_H 350
#define START_PAUSED 1
#define GAME_HINT "Arrows: move. Enter: place. B: mode. H: first."
#define GAME_HELP "17x17 board: connect five in any direction."
#include "game_ui.h"
#include <limits.h>

#define BOARD_SIZE 17
#define WIN_CONDITION 5
static char board[BOARD_SIZE][BOARD_SIZE], current_player, human_marker = 'X';
static int mode = 1, move_count, cursor = 144, last_move = -1;
static int ai_active, ai_index, ai_best, ai_score;
static const char *mode_names[] = {"", "Player vs Computer", "Player vs Player", "Computer demo"};

static char check_winner(char board[BOARD_SIZE][BOARD_SIZE]) {
    const int directions[4][2] = {
        {1, 0},
        {0, 1},
        {1, 1},
        {1, -1},
    };

    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            char marker = board[r][c];
            if (marker == ' ') {
                continue;
            }
            for (int d = 0; d < 4; ++d) {
                int dr = directions[d][0];
                int dc = directions[d][1];
                int match = 1;
                int rr = r;
                int cc = c;
                for (int step = 1; step < WIN_CONDITION; ++step) {
                    rr += dr;
                    cc += dc;
                    if (rr < 0 || rr >= BOARD_SIZE || cc < 0 || cc >= BOARD_SIZE) {
                        match = 0;
                        break;
                    }
                    if (board[rr][cc] != marker) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    return marker;
                }
            }
        }
    }
    return '\0';
}


static int board_full(char board[BOARD_SIZE][BOARD_SIZE]) {
    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            if (board[r][c] == ' ') {
                return 0;
            }
        }
    }
    return 1;
}


static int evaluate_board(char board[BOARD_SIZE][BOARD_SIZE], char ai_marker,
                          char opponent_marker) {
    char winner = check_winner(board);
    if (winner == ai_marker) {
        return 1000000;
    }
    if (winner == opponent_marker) {
        return -1000000;
    }

    static const int pattern_score[WIN_CONDITION + 1] = {0, 4, 32, 256, 10000};
    const int directions[4][2] = {
        {1, 0},
        {0, 1},
        {1, 1},
        {1, -1},
    };

    int score = 0;
    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            for (int d = 0; d < 4; ++d) {
                int dr = directions[d][0];
                int dc = directions[d][1];
                int end_r = r + (WIN_CONDITION - 1) * dr;
                int end_c = c + (WIN_CONDITION - 1) * dc;
                if (end_r < 0 || end_r >= BOARD_SIZE || end_c < 0 || end_c >= BOARD_SIZE) {
                    continue;
                }
                int ai_count = 0;
                int opp_count = 0;
                int empty_count = 0;
                for (int step = 0; step < WIN_CONDITION; ++step) {
                    char marker = board[r + step * dr][c + step * dc];
                    if (marker == ai_marker) {
                        ++ai_count;
                    } else if (marker == opponent_marker) {
                        ++opp_count;
                    } else {
                        ++empty_count;
                    }
                }
                if (ai_count > 0 && opp_count == 0) {
                    score += pattern_score[ai_count] * (empty_count + 1);
                } else if (opp_count > 0 && ai_count == 0) {
                    score -= pattern_score[opp_count] * (empty_count + 1);
                }
            }
        }
    }
    return score;
}


static unsigned int game_interval(void) { return ai_active ? 1000 : 250000; }
static int computer_turn(void)
{
    return mode == 3 || (mode == 1 && current_player != human_marker);
}
static void update_status(void)
{
    snprintf(status, sizeof(status), "%s | %s %c to move%s", mode_names[mode],
             computer_turn() ? "Computer" : "Player", current_player,
             mode == 1 ? (human_marker == 'X' ? " (you: X)" : " (you: O)") : "");
}
static void game_reset(void)
{
    memset(board, ' ', sizeof(board));
    current_player = 'X';
    move_count = ai_active = 0;
    cursor = 144;
    last_move = -1;
    update_status();
}
static void place(int square)
{
    if (square < 0 || square >= BOARD_SIZE * BOARD_SIZE) return;
    if (board[square / BOARD_SIZE][square % BOARD_SIZE] != ' ') { effect(5); return; }
    board[square / BOARD_SIZE][square % BOARD_SIZE] = current_player;
    cursor = last_move = square;
    ++move_count;
    ai_active = 0;
    char winner = check_winner(board);
    finished = winner || board_full(board);
    if (finished) {
        if (winner) snprintf(status, sizeof(status), "%c wins! R: new game.", winner);
        else snprintf(status, sizeof(status), "Draw! R: new game.");
        effect(winner ? 4 : 3);
    } else {
        current_player = current_player == 'X' ? 'O' : 'X';
        update_status();
        effect(1);
    }
    /* Leave time for the move's sound before the next computer turn. */
    next_tick = host_api->get_time_ms() * 1000ULL + 250000;
}
static int winning_square(char marker)
{
    for (int square = 0; square < BOARD_SIZE * BOARD_SIZE; ++square) {
        int r = square / BOARD_SIZE, c = square % BOARD_SIZE;
        if (board[r][c] != ' ') continue;
        board[r][c] = marker;
        int wins = check_winner(board) == marker;
        board[r][c] = ' ';
        if (wins) return square;
    }
    return -1;
}
static void game_tick(void)
{
    if (!computer_turn()) return;
    char opponent = current_player == 'X' ? 'O' : 'X';
    if (!ai_active) {
        int square = winning_square(current_player);
        if (square < 0) square = winning_square(opponent);
        if (square >= 0) { place(square); return; }
        if (!move_count) { place(144); return; }
        ai_active = 1;
        ai_index = 0;
        ai_best = -1;
        ai_score = INT_MIN;
    }
    /* Four candidate evaluations per draw, keeping desktop input responsive. */
    for (int budget = 0; budget < 4 && ai_index < BOARD_SIZE * BOARD_SIZE; ++budget) {
        int square = ai_index++, r = square / BOARD_SIZE, c = square % BOARD_SIZE;
        if (board[r][c] != ' ') continue;
        board[r][c] = current_player;
        int score = evaluate_board(board, current_player, opponent);
        board[r][c] = ' ';
        if (score > ai_score || (score == ai_score && (random_number() & 1))) {
            ai_score = score;
            ai_best = square;
        }
    }
    if (ai_index == BOARD_SIZE * BOARD_SIZE && ai_best >= 0) place(ai_best);
}
static int game_key(int key)
{
    if (key == 'b' || key == 'B' || key == 'h' || key == 'H') {
        if (move_count) {
            snprintf(status, sizeof(status), "Start a new game before changing mode or first player.");
            return 1;
        }
        if (key == 'b' || key == 'B') mode = mode % 3 + 1;
        else human_marker = human_marker == 'X' ? 'O' : 'X';
        ai_active = 0;
        update_status();
        return 1;
    }
    if (paused || finished || computer_turn()) return 0;
    if (key == 13 || key == ' ') place(cursor);
    else if (key == (0x100 | 72) && cursor >= BOARD_SIZE) cursor -= BOARD_SIZE;
    else if (key == (0x100 | 80) && cursor < BOARD_SIZE * (BOARD_SIZE - 1)) cursor += BOARD_SIZE;
    else if (key == (0x100 | 75) && cursor % BOARD_SIZE) --cursor;
    else if (key == (0x100 | 77) && cursor % BOARD_SIZE < BOARD_SIZE - 1) ++cursor;
    else return 0;
    return 1;
}
static void game_click(const Layout *l, int x, int y)
{
    if (!computer_turn()) place((y - l->by) / l->cell * BOARD_SIZE + (x - l->bx) / l->cell);
}
static void mark(int x, int y, int size, char marker)
{
    if (marker == 'X') {
        for (int i = 2; i < size - 2; ++i) {
            host_api->fill_rect_rgb(x + i, y + i, 1, 1, 0x205890);
            host_api->fill_rect_rgb(x + size - i - 1, y + i, 1, 1, 0x205890);
        }
    } else if (marker == 'O') {
        int inset = size / 4;
        host_api->fill_rect_rgb(x + inset, y + 2, size - inset * 2, 2, 0xb04838);
        host_api->fill_rect_rgb(x + inset, y + size - 4, size - inset * 2, 2, 0xb04838);
        host_api->fill_rect_rgb(x + 2, y + inset, 2, size - inset * 2, 0xb04838);
        host_api->fill_rect_rgb(x + size - 4, y + inset, 2, size - inset * 2, 0xb04838);
    }
}
static void game_draw(const Layout *l)
{
    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            int square = r * BOARD_SIZE + c;
            int x = l->bx + c * l->cell, y = l->by + r * l->cell;
            host_api->fill_rect_rgb(x, y, l->cell, l->cell, 0x708080);
            host_api->fill_rect_rgb(x + 1, y + 1, l->cell - 1, l->cell - 1,
                                    square == last_move ? 0xe8d890 : 0xf0e8d0);
            mark(x, y, l->cell, board[r][c]);
            if (square == cursor) host_api->draw_rect(x + 1, y + 1, l->cell - 2, l->cell - 2,
                                                      color(BUDO_SYS_COLOR_ACCENT));
        }
    }
}
static void game_icon(int x, int y)
{
    static const unsigned int palette[] = {0,0xf0e8d0,0x708080,0x205890,0xb04838};
    static const char pixels[] =
        "0001111111111000" "0011111111111100" "0113121112111110" "0111321112111110"
        "0113121112111110" "0122222222222210" "0111124442111110" "0111124142111110"
        "0111124442111110" "0122222222222210" "0111121112131310" "0111121112113110"
        "0111121112131310" "0111121112111110" "0011111111111100" "0001111111111000";
    icon_sprite(x, y, pixels, palette);
}
