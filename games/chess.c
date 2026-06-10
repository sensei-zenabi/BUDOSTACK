#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

typedef enum {
    MODE_PVP,
    MODE_PVC
} GameMode;

typedef enum {
    DIFF_EASY,
    DIFF_MEDIUM,
    DIFF_HARD
} Difficulty;

typedef enum {
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_SELECT,
    KEY_QUIT,
    KEY_RESTART
} InputKey;

typedef struct {
    int from_row;
    int from_col;
    int to_row;
    int to_col;
    char promotion;
    int castle;
    int en_passant;
    int captured_row;
    int captured_col;
} Move;

typedef struct {
    char board[8][8];
    int white_to_move;
    int white_castle_king;
    int white_castle_queen;
    int black_castle_king;
    int black_castle_queen;
    int en_passant_row;
    int en_passant_col;
    int halfmove_clock;
    int fullmove_number;
} GameState;

typedef struct {
    Move items[256];
    int count;
} MoveList;

#define INF_SCORE 1000000
#define MATE_SCORE 900000
#define INPUT_SIZE 64

static struct termios orig_termios;
static int raw_mode_enabled = 0;

static char piece_glyph(char piece) {
    if (piece == ' ') {
        return ' ';
    }
    return piece;
}

static int is_white_piece(char piece) {
    return piece >= 'A' && piece <= 'Z';
}

static int is_black_piece(char piece) {
    return piece >= 'a' && piece <= 'z';
}

static int piece_color(char piece) {
    if (is_white_piece(piece)) {
        return 1;
    }
    if (is_black_piece(piece)) {
        return -1;
    }
    return 0;
}

static int in_bounds(int row, int col) {
    return row >= 0 && row < 8 && col >= 0 && col < 8;
}

static char lower_piece(char piece) {
    return (char)tolower((unsigned char)piece);
}

static int same_square(int row_a, int col_a, int row_b, int col_b) {
    return row_a == row_b && col_a == col_b;
}

static void init_game(GameState *state) {
    static const char *start[8] = {
        "rnbqkbnr",
        "pppppppp",
        "        ",
        "        ",
        "        ",
        "        ",
        "PPPPPPPP",
        "RNBQKBNR"
    };

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            state->board[row][col] = start[row][col];
        }
    }

    state->white_to_move = 1;
    state->white_castle_king = 1;
    state->white_castle_queen = 1;
    state->black_castle_king = 1;
    state->black_castle_queen = 1;
    state->en_passant_row = -1;
    state->en_passant_col = -1;
    state->halfmove_clock = 0;
    state->fullmove_number = 1;
}

static void add_move(MoveList *list, int from_row, int from_col, int to_row, int to_col,
                     char promotion, int castle, int en_passant) {
    Move move;

    if (list->count >= (int)(sizeof(list->items) / sizeof(list->items[0]))) {
        return;
    }

    move.from_row = from_row;
    move.from_col = from_col;
    move.to_row = to_row;
    move.to_col = to_col;
    move.promotion = promotion;
    move.castle = castle;
    move.en_passant = en_passant;
    move.captured_row = to_row;
    move.captured_col = to_col;
    list->items[list->count] = move;
    list->count++;
}

static void add_promotion_moves(MoveList *list, int from_row, int from_col, int to_row, int to_col,
                                int white, int en_passant) {
    const char white_promos[] = { 'Q', 'R', 'B', 'N' };
    const char black_promos[] = { 'q', 'r', 'b', 'n' };
    const char *promos = white ? white_promos : black_promos;

    for (int i = 0; i < 4; i++) {
        add_move(list, from_row, from_col, to_row, to_col, promos[i], 0, en_passant);
    }
}

static int square_attacked(const GameState *state, int row, int col, int by_white) {
    int pawn_dir = by_white ? -1 : 1;
    int pawn_row = row - pawn_dir;
    char pawn = by_white ? 'P' : 'p';
    char knight = by_white ? 'N' : 'n';
    char bishop = by_white ? 'B' : 'b';
    char rook = by_white ? 'R' : 'r';
    char queen = by_white ? 'Q' : 'q';
    char king = by_white ? 'K' : 'k';
    static const int knight_dirs[8][2] = {
        { -2, -1 }, { -2, 1 }, { -1, -2 }, { -1, 2 },
        { 1, -2 }, { 1, 2 }, { 2, -1 }, { 2, 1 }
    };
    static const int bishop_dirs[4][2] = {
        { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 }
    };
    static const int rook_dirs[4][2] = {
        { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }
    };

    if (in_bounds(pawn_row, col - 1) && state->board[pawn_row][col - 1] == pawn) {
        return 1;
    }
    if (in_bounds(pawn_row, col + 1) && state->board[pawn_row][col + 1] == pawn) {
        return 1;
    }

    for (int i = 0; i < 8; i++) {
        int test_row = row + knight_dirs[i][0];
        int test_col = col + knight_dirs[i][1];
        if (in_bounds(test_row, test_col) && state->board[test_row][test_col] == knight) {
            return 1;
        }
    }

    for (int i = 0; i < 4; i++) {
        int test_row = row + bishop_dirs[i][0];
        int test_col = col + bishop_dirs[i][1];
        while (in_bounds(test_row, test_col)) {
            char piece = state->board[test_row][test_col];
            if (piece != ' ') {
                if (piece == bishop || piece == queen) {
                    return 1;
                }
                break;
            }
            test_row += bishop_dirs[i][0];
            test_col += bishop_dirs[i][1];
        }
    }

    for (int i = 0; i < 4; i++) {
        int test_row = row + rook_dirs[i][0];
        int test_col = col + rook_dirs[i][1];
        while (in_bounds(test_row, test_col)) {
            char piece = state->board[test_row][test_col];
            if (piece != ' ') {
                if (piece == rook || piece == queen) {
                    return 1;
                }
                break;
            }
            test_row += rook_dirs[i][0];
            test_col += rook_dirs[i][1];
        }
    }

    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            int test_row = row + dr;
            int test_col = col + dc;
            if ((dr != 0 || dc != 0) && in_bounds(test_row, test_col) &&
                state->board[test_row][test_col] == king) {
                return 1;
            }
        }
    }

    return 0;
}

static int king_in_check(const GameState *state, int white_king) {
    char king = white_king ? 'K' : 'k';

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (state->board[row][col] == king) {
                return square_attacked(state, row, col, !white_king);
            }
        }
    }

    return 1;
}

static void generate_pawn_moves(const GameState *state, MoveList *list, int row, int col, int white) {
    int dir = white ? -1 : 1;
    int start_row = white ? 6 : 1;
    int promote_row = white ? 0 : 7;
    int next_row = row + dir;

    if (in_bounds(next_row, col) && state->board[next_row][col] == ' ') {
        if (next_row == promote_row) {
            add_promotion_moves(list, row, col, next_row, col, white, 0);
        } else {
            add_move(list, row, col, next_row, col, 0, 0, 0);
        }
        if (row == start_row && state->board[row + (dir * 2)][col] == ' ') {
            add_move(list, row, col, row + (dir * 2), col, 0, 0, 0);
        }
    }

    for (int dc = -1; dc <= 1; dc += 2) {
        int target_col = col + dc;
        if (!in_bounds(next_row, target_col)) {
            continue;
        }
        if (state->board[next_row][target_col] != ' ' && piece_color(state->board[next_row][target_col]) == (white ? -1 : 1)) {
            if (next_row == promote_row) {
                add_promotion_moves(list, row, col, next_row, target_col, white, 0);
            } else {
                add_move(list, row, col, next_row, target_col, 0, 0, 0);
            }
        }
        if (same_square(next_row, target_col, state->en_passant_row, state->en_passant_col)) {
            add_move(list, row, col, next_row, target_col, 0, 0, 1);
        }
    }
}

static void generate_knight_moves(const GameState *state, MoveList *list, int row, int col, int color) {
    static const int dirs[8][2] = {
        { -2, -1 }, { -2, 1 }, { -1, -2 }, { -1, 2 },
        { 1, -2 }, { 1, 2 }, { 2, -1 }, { 2, 1 }
    };

    for (int i = 0; i < 8; i++) {
        int target_row = row + dirs[i][0];
        int target_col = col + dirs[i][1];
        if (in_bounds(target_row, target_col) && piece_color(state->board[target_row][target_col]) != color) {
            add_move(list, row, col, target_row, target_col, 0, 0, 0);
        }
    }
}

static void generate_sliding_moves(const GameState *state, MoveList *list, int row, int col, int color,
                                   const int dirs[][2], int dir_count) {
    for (int i = 0; i < dir_count; i++) {
        int target_row = row + dirs[i][0];
        int target_col = col + dirs[i][1];
        while (in_bounds(target_row, target_col)) {
            int target_color = piece_color(state->board[target_row][target_col]);
            if (target_color == color) {
                break;
            }
            add_move(list, row, col, target_row, target_col, 0, 0, 0);
            if (target_color != 0) {
                break;
            }
            target_row += dirs[i][0];
            target_col += dirs[i][1];
        }
    }
}

static void generate_king_moves(const GameState *state, MoveList *list, int row, int col, int white) {
    int color = white ? 1 : -1;

    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            int target_row = row + dr;
            int target_col = col + dc;
            if ((dr != 0 || dc != 0) && in_bounds(target_row, target_col) &&
                piece_color(state->board[target_row][target_col]) != color) {
                add_move(list, row, col, target_row, target_col, 0, 0, 0);
            }
        }
    }

    if (white && row == 7 && col == 4 && !king_in_check(state, 1)) {
        if (state->white_castle_king && state->board[7][5] == ' ' && state->board[7][6] == ' ' &&
            state->board[7][7] == 'R' && !square_attacked(state, 7, 5, 0) && !square_attacked(state, 7, 6, 0)) {
            add_move(list, row, col, 7, 6, 0, 1, 0);
        }
        if (state->white_castle_queen && state->board[7][1] == ' ' && state->board[7][2] == ' ' &&
            state->board[7][3] == ' ' && state->board[7][0] == 'R' && !square_attacked(state, 7, 3, 0) &&
            !square_attacked(state, 7, 2, 0)) {
            add_move(list, row, col, 7, 2, 0, 1, 0);
        }
    }

    if (!white && row == 0 && col == 4 && !king_in_check(state, 0)) {
        if (state->black_castle_king && state->board[0][5] == ' ' && state->board[0][6] == ' ' &&
            state->board[0][7] == 'r' && !square_attacked(state, 0, 5, 1) && !square_attacked(state, 0, 6, 1)) {
            add_move(list, row, col, 0, 6, 0, 1, 0);
        }
        if (state->black_castle_queen && state->board[0][1] == ' ' && state->board[0][2] == ' ' &&
            state->board[0][3] == ' ' && state->board[0][0] == 'r' && !square_attacked(state, 0, 3, 1) &&
            !square_attacked(state, 0, 2, 1)) {
            add_move(list, row, col, 0, 2, 0, 1, 0);
        }
    }
}

static void generate_pseudo_moves(const GameState *state, MoveList *list) {
    static const int bishop_dirs[4][2] = {
        { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 }
    };
    static const int rook_dirs[4][2] = {
        { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }
    };
    static const int queen_dirs[8][2] = {
        { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 },
        { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }
    };
    int color = state->white_to_move ? 1 : -1;

    list->count = 0;
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            char piece = state->board[row][col];
            if (piece_color(piece) != color) {
                continue;
            }

            switch (lower_piece(piece)) {
                case 'p':
                    generate_pawn_moves(state, list, row, col, state->white_to_move);
                    break;
                case 'n':
                    generate_knight_moves(state, list, row, col, color);
                    break;
                case 'b':
                    generate_sliding_moves(state, list, row, col, color, bishop_dirs, 4);
                    break;
                case 'r':
                    generate_sliding_moves(state, list, row, col, color, rook_dirs, 4);
                    break;
                case 'q':
                    generate_sliding_moves(state, list, row, col, color, queen_dirs, 8);
                    break;
                case 'k':
                    generate_king_moves(state, list, row, col, state->white_to_move);
                    break;
                default:
                    break;
            }
        }
    }
}

static char make_move(GameState *state, Move move) {
    char piece = state->board[move.from_row][move.from_col];
    char captured = state->board[move.to_row][move.to_col];
    int moving_white = is_white_piece(piece);

    if (move.en_passant) {
        move.captured_row = moving_white ? move.to_row + 1 : move.to_row - 1;
        move.captured_col = move.to_col;
        captured = state->board[move.captured_row][move.captured_col];
        state->board[move.captured_row][move.captured_col] = ' ';
    }

    state->board[move.to_row][move.to_col] = move.promotion ? move.promotion : piece;
    state->board[move.from_row][move.from_col] = ' ';

    if (move.castle) {
        if (move.to_col == 6) {
            state->board[move.to_row][5] = state->board[move.to_row][7];
            state->board[move.to_row][7] = ' ';
        } else if (move.to_col == 2) {
            state->board[move.to_row][3] = state->board[move.to_row][0];
            state->board[move.to_row][0] = ' ';
        }
    }

    if (piece == 'K') {
        state->white_castle_king = 0;
        state->white_castle_queen = 0;
    } else if (piece == 'k') {
        state->black_castle_king = 0;
        state->black_castle_queen = 0;
    } else if (piece == 'R' && move.from_row == 7 && move.from_col == 0) {
        state->white_castle_queen = 0;
    } else if (piece == 'R' && move.from_row == 7 && move.from_col == 7) {
        state->white_castle_king = 0;
    } else if (piece == 'r' && move.from_row == 0 && move.from_col == 0) {
        state->black_castle_queen = 0;
    } else if (piece == 'r' && move.from_row == 0 && move.from_col == 7) {
        state->black_castle_king = 0;
    }

    if (captured == 'R' && move.to_row == 7 && move.to_col == 0) {
        state->white_castle_queen = 0;
    } else if (captured == 'R' && move.to_row == 7 && move.to_col == 7) {
        state->white_castle_king = 0;
    } else if (captured == 'r' && move.to_row == 0 && move.to_col == 0) {
        state->black_castle_queen = 0;
    } else if (captured == 'r' && move.to_row == 0 && move.to_col == 7) {
        state->black_castle_king = 0;
    }

    state->en_passant_row = -1;
    state->en_passant_col = -1;
    if (lower_piece(piece) == 'p' && abs(move.to_row - move.from_row) == 2) {
        state->en_passant_row = (move.from_row + move.to_row) / 2;
        state->en_passant_col = move.from_col;
    }

    if (lower_piece(piece) == 'p' || captured != ' ') {
        state->halfmove_clock = 0;
    } else {
        state->halfmove_clock++;
    }

    if (!state->white_to_move) {
        state->fullmove_number++;
    }
    state->white_to_move = !state->white_to_move;
    return captured;
}

static void generate_legal_moves(const GameState *state, MoveList *legal) {
    MoveList pseudo;
    int moving_white = state->white_to_move;

    generate_pseudo_moves(state, &pseudo);
    legal->count = 0;
    for (int i = 0; i < pseudo.count; i++) {
        GameState copy = *state;
        make_move(&copy, pseudo.items[i]);
        if (!king_in_check(&copy, moving_white)) {
            legal->items[legal->count] = pseudo.items[i];
            legal->count++;
        }
    }
}

static int piece_value(char piece) {
    switch (lower_piece(piece)) {
        case 'p': return 100;
        case 'n': return 320;
        case 'b': return 330;
        case 'r': return 500;
        case 'q': return 900;
        case 'k': return 20000;
        default: return 0;
    }
}

static int center_bonus(int row, int col) {
    int row_center = row > 3 ? row - 3 : 4 - row;
    int col_center = col > 3 ? col - 3 : 4 - col;
    return 14 - ((row_center + col_center) * 2);
}

static int evaluate_board(const GameState *state) {
    int score = 0;

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            char piece = state->board[row][col];
            int value = piece_value(piece);
            if (piece == ' ') {
                continue;
            }
            value += center_bonus(row, col);
            if (lower_piece(piece) == 'p') {
                value += is_white_piece(piece) ? (6 - row) * 8 : (row - 1) * 8;
            }
            score += is_white_piece(piece) ? value : -value;
        }
    }

    return score;
}

static int move_order_score(const GameState *state, Move move) {
    char moving = state->board[move.from_row][move.from_col];
    char target = state->board[move.to_row][move.to_col];
    int score = 0;

    if (move.en_passant) {
        target = is_white_piece(moving) ? 'p' : 'P';
    }
    if (target != ' ') {
        score += (piece_value(target) * 10) - piece_value(moving);
    }
    if (move.promotion) {
        score += piece_value(move.promotion);
    }
    if (move.castle) {
        score += 40;
    }
    score += center_bonus(move.to_row, move.to_col);
    return score;
}

static void sort_moves(const GameState *state, MoveList *moves) {
    for (int i = 1; i < moves->count; i++) {
        Move key = moves->items[i];
        int key_score = move_order_score(state, key);
        int j = i - 1;
        while (j >= 0 && move_order_score(state, moves->items[j]) < key_score) {
            moves->items[j + 1] = moves->items[j];
            j--;
        }
        moves->items[j + 1] = key;
    }
}

static int minimax(GameState *state, int depth, int alpha, int beta) {
    MoveList moves;
    int maximizing = state->white_to_move;
    int best;

    generate_legal_moves(state, &moves);
    if (moves.count == 0) {
        if (king_in_check(state, state->white_to_move)) {
            return maximizing ? -MATE_SCORE - depth : MATE_SCORE + depth;
        }
        return 0;
    }
    if (depth == 0) {
        return evaluate_board(state);
    }

    sort_moves(state, &moves);
    best = maximizing ? -INF_SCORE : INF_SCORE;
    for (int i = 0; i < moves.count; i++) {
        GameState copy = *state;
        int score;
        make_move(&copy, moves.items[i]);
        score = minimax(&copy, depth - 1, alpha, beta);
        if (maximizing) {
            if (score > best) {
                best = score;
            }
            if (best > alpha) {
                alpha = best;
            }
        } else {
            if (score < best) {
                best = score;
            }
            if (best < beta) {
                beta = best;
            }
        }
        if (beta <= alpha) {
            break;
        }
    }

    return best;
}

static Move choose_ai_move(const GameState *state, Difficulty difficulty) {
    MoveList moves;
    Move best_move;
    int best_score;
    int ai_white = state->white_to_move;
    int depth = difficulty == DIFF_HARD ? 3 : 1;

    generate_legal_moves(state, &moves);
    best_move = moves.items[0];

    if (difficulty == DIFF_EASY) {
        return moves.items[rand() % moves.count];
    }

    sort_moves(state, &moves);
    best_score = ai_white ? -INF_SCORE : INF_SCORE;
    for (int i = 0; i < moves.count; i++) {
        GameState copy = *state;
        int score;
        make_move(&copy, moves.items[i]);
        score = minimax(&copy, depth - 1, -INF_SCORE, INF_SCORE);
        if (difficulty == DIFF_MEDIUM) {
            score += ai_white ? move_order_score(state, moves.items[i]) : -move_order_score(state, moves.items[i]);
        }
        if ((ai_white && score > best_score) || (!ai_white && score < best_score) ||
            (score == best_score && (rand() % 2) == 0)) {
            best_score = score;
            best_move = moves.items[i];
        }
    }

    return best_move;
}

static void clear_screen(void) {
    printf("\033[2J\033[H");
}


static void disable_raw_mode(void) {
    if (!raw_mode_enabled) {
        return;
    }
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        perror("chess: tcsetattr");
    }
    raw_mode_enabled = 0;
}

static void enable_raw_mode(void) {
    if (raw_mode_enabled || !isatty(STDIN_FILENO)) {
        return;
    }
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("chess: tcgetattr");
        exit(EXIT_FAILURE);
    }

    struct termios raw = orig_termios;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("chess: tcsetattr");
        exit(EXIT_FAILURE);
    }
    raw_mode_enabled = 1;
    atexit(disable_raw_mode);
}

static int read_key(void) {
    int ch = getchar();

    if (ch == EOF) {
        return KEY_QUIT;
    }
    if (ch == '\033') {
        int first = getchar();
        int second = getchar();
        if (first == '[') {
            if (second == 'A') {
                return KEY_UP;
            }
            if (second == 'B') {
                return KEY_DOWN;
            }
            if (second == 'C') {
                return KEY_RIGHT;
            }
            if (second == 'D') {
                return KEY_LEFT;
            }
        }
        return KEY_NONE;
    }
    if (ch == 'w' || ch == 'W') {
        return KEY_UP;
    }
    if (ch == 's' || ch == 'S') {
        return KEY_DOWN;
    }
    if (ch == 'a' || ch == 'A') {
        return KEY_LEFT;
    }
    if (ch == 'd' || ch == 'D') {
        return KEY_RIGHT;
    }
    if (ch == ' ' || ch == '\n' || ch == '\r') {
        return KEY_SELECT;
    }
    if (ch == 'r' || ch == 'R') {
        return KEY_RESTART;
    }
    if (ch == 'q' || ch == 'Q') {
        return KEY_QUIT;
    }
    return KEY_NONE;
}

static const char *difficulty_name(Difficulty difficulty) {
    switch (difficulty) {
        case DIFF_EASY: return "EASY";
        case DIFF_MEDIUM: return "MEDIUM";
        case DIFF_HARD: return "HARD";
        default: return "UNKNOWN";
    }
}

static void print_spaces(int count) {
    for (int i = 0; i < count; i++) {
        putchar(' ');
    }
}

static void print_centered_text(const char *text) {
    int width = (int)strlen(text);
    int padding = (80 - width) / 2;

    if (padding < 0) {
        padding = 0;
    }
    print_spaces(padding);
    printf("%s\n", text);
}

static void square_name(int row, int col, char *buffer, size_t buffer_size) {
    if (row < 0 || col < 0) {
        (void)snprintf(buffer, buffer_size, "--");
        return;
    }
    (void)snprintf(buffer, buffer_size, "%c%d", (char)('a' + col), 8 - row);
}

static void move_name(Move move, char *buffer, size_t buffer_size) {
    char from[4];
    char to[4];

    if (move.from_row < 0 || move.to_row < 0) {
        (void)snprintf(buffer, buffer_size, "--");
        return;
    }
    square_name(move.from_row, move.from_col, from, sizeof(from));
    square_name(move.to_row, move.to_col, to, sizeof(to));
    (void)snprintf(buffer, buffer_size, "%s-%s", from, to);
}

static void print_board_cell(const GameState *state, int row, int col, int show_cursor,
                             int cursor_row, int cursor_col, int selected_row, int selected_col,
                             Move last_move) {
    char piece = piece_glyph(state->board[row][col]);
    int cursor = show_cursor && row == cursor_row && col == cursor_col;
    int selected = row == selected_row && col == selected_col;
    int last = same_square(row, col, last_move.from_row, last_move.from_col) ||
               same_square(row, col, last_move.to_row, last_move.to_col);

    if (piece == ' ') {
        piece = '.';
    }

    if (cursor) {
        printf("\033[7m");
    } else if (selected) {
        printf("\033[4m");
    } else if (last) {
        printf("\033[1m");
    }

    printf(" %c ", piece);

    if (cursor || selected || last) {
        printf("\033[0m");
    }
}

static void print_info_pair(const char *left, const char *right) {
    print_spaces(14);
    printf("%-24s  %-24s\n", left, right);
}

static void render_board(const GameState *state, const char *status, GameMode mode, Difficulty difficulty,
                         Move last_move, int cursor_row, int cursor_col,
                         int selected_row, int selected_col, int show_cursor) {
    const char *mode_name = mode == MODE_PVP ? "Player vs Player" : "Player vs Computer";
    const char *side_name = state->white_to_move ? "White" : "Black";
    char line[96];
    char selected[8];
    char last[16];
    char castle[24];
    char left[40];
    char right[40];

    square_name(selected_row, selected_col, selected, sizeof(selected));
    move_name(last_move, last, sizeof(last));
    (void)snprintf(castle, sizeof(castle), "W%s%s B%s%s",
                   state->white_castle_king ? "K" : "-", state->white_castle_queen ? "Q" : "-",
                   state->black_castle_king ? "k" : "-", state->black_castle_queen ? "q" : "-");

    clear_screen();
    printf("\n\n\n\n\n\n\n\n\n\n");
    print_centered_text("BUDOSTACK CHESS");
    if (mode == MODE_PVC) {
        (void)snprintf(line, sizeof(line), "%s  |  %s", mode_name, difficulty_name(difficulty));
    } else {
        (void)snprintf(line, sizeof(line), "%s", mode_name);
    }
    print_centered_text(line);
    print_centered_text("Arrows/WASD move   SPACE selects   R restarts   Q quits");
    printf("\n");
    print_centered_text(status);
    printf("\n");

    print_spaces(25);
    printf("    a  b  c  d  e  f  g  h\n");
    print_spaces(25);
    printf("  +------------------------+\n");

    for (int row = 0; row < 8; row++) {
        print_spaces(25);
        printf("%d |", 8 - row);
        for (int col = 0; col < 8; col++) {
            print_board_cell(state, row, col, show_cursor, cursor_row, cursor_col,
                             selected_row, selected_col, last_move);
        }
        printf("| %d\n", 8 - row);
    }

    print_spaces(25);
    printf("  +------------------------+\n");
    print_spaces(25);
    printf("    a  b  c  d  e  f  g  h\n");
    printf("\n");

    (void)snprintf(left, sizeof(left), "Turn: %s", side_name);
    (void)snprintf(right, sizeof(right), "Selected: %s", selected);
    print_info_pair(left, right);
    (void)snprintf(left, sizeof(left), "Move: %d", state->fullmove_number);
    (void)snprintf(right, sizeof(right), "Last: %s", last);
    print_info_pair(left, right);
    (void)snprintf(left, sizeof(left), "Mode: %s", mode == MODE_PVP ? "PVP" : "PVC");
    (void)snprintf(right, sizeof(right), "Castle: %s", castle);
    print_info_pair(left, right);
    if (mode == MODE_PVC) {
        (void)snprintf(left, sizeof(left), "AI: %s", difficulty_name(difficulty));
    } else {
        (void)snprintf(left, sizeof(left), "AI: none");
    }
    (void)snprintf(right, sizeof(right), "Fifty: %d", state->halfmove_clock);
    print_info_pair(left, right);
    printf("\n");
    print_centered_text("Uppercase pieces are White. Lowercase pieces are Black. Dots are empty squares.");
    fflush(stdout);
}

static int read_line(char *buffer, size_t buffer_size) {
    if (fgets(buffer, (int)buffer_size, stdin) == NULL) {
        if (ferror(stdin)) {
            perror("chess: fgets");
        }
        return 0;
    }
    buffer[strcspn(buffer, "\n")] = '\0';
    return 1;
}

static GameMode choose_mode(void) {
    char input[INPUT_SIZE];

    for (;;) {
        clear_screen();
        printf("BUDOSTACK Chess\n\n");
        printf("1) Player vs Computer\n");
        printf("2) Player vs Player\n\n");
        printf("Choice: ");
        fflush(stdout);
        if (!read_line(input, sizeof(input))) {
            return MODE_PVC;
        }
        if (input[0] == 'q' || input[0] == 'Q') {
            return MODE_PVC;
        }
        if (strcmp(input, "1") == 0) {
            return MODE_PVC;
        }
        if (strcmp(input, "2") == 0) {
            return MODE_PVP;
        }
    }
}

static Difficulty choose_difficulty(void) {
    char input[INPUT_SIZE];

    for (;;) {
        clear_screen();
        printf("Choose difficulty.\n\n");
        printf("1) EASY   - random legal moves\n");
        printf("2) MEDIUM - quick tactical search\n");
        printf("3) HARD   - deeper alpha-beta search\n\n");
        printf("Difficulty [1-3]: ");
        fflush(stdout);
        if (!read_line(input, sizeof(input))) {
            return DIFF_EASY;
        }
        if (strcmp(input, "1") == 0) {
            return DIFF_EASY;
        }
        if (strcmp(input, "2") == 0) {
            return DIFF_MEDIUM;
        }
        if (strcmp(input, "3") == 0) {
            return DIFF_HARD;
        }
    }
}

static int legal_source_has_move(const MoveList *legal, int row, int col) {
    for (int i = 0; i < legal->count; i++) {
        if (legal->items[i].from_row == row && legal->items[i].from_col == col) {
            return 1;
        }
    }
    return 0;
}

static int find_selected_move(const MoveList *legal, int from_row, int from_col,
                              int to_row, int to_col, Move *chosen) {
    for (int i = 0; i < legal->count; i++) {
        Move move = legal->items[i];
        if (move.from_row == from_row && move.from_col == from_col &&
            move.to_row == to_row && move.to_col == to_col) {
            *chosen = move;
            return 1;
        }
    }
    return 0;
}

static void move_cursor(int key, int *cursor_row, int *cursor_col) {
    if (key == KEY_UP && *cursor_row > 0) {
        (*cursor_row)--;
    } else if (key == KEY_DOWN && *cursor_row < 7) {
        (*cursor_row)++;
    } else if (key == KEY_LEFT && *cursor_col > 0) {
        (*cursor_col)--;
    } else if (key == KEY_RIGHT && *cursor_col < 7) {
        (*cursor_col)++;
    }
}

static int human_turn(GameState *state, GameMode mode, Difficulty difficulty,
                      int *cursor_row, int *cursor_col, Move *last_move) {
    int selected_row = -1;
    int selected_col = -1;
    char status[128];

    (void)snprintf(status, sizeof(status), "%s to move.", state->white_to_move ? "White" : "Black");
    for (;;) {
        MoveList legal;
        int key;

        generate_legal_moves(state, &legal);
        render_board(state, status, mode, difficulty, *last_move, *cursor_row, *cursor_col,
                     selected_row, selected_col, 1);
        key = read_key();
        if (key == KEY_QUIT) {
            return 0;
        }
        if (key == KEY_RESTART) {
            return 2;
        }
        if (key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT || key == KEY_RIGHT) {
            move_cursor(key, cursor_row, cursor_col);
            continue;
        }
        if (key != KEY_SELECT) {
            continue;
        }

        if (selected_row < 0 || selected_col < 0) {
            int color = state->white_to_move ? 1 : -1;
            char piece = state->board[*cursor_row][*cursor_col];
            if (piece_color(piece) != color) {
                (void)snprintf(status, sizeof(status), "Select one of your own pieces first.");
                continue;
            }
            if (!legal_source_has_move(&legal, *cursor_row, *cursor_col)) {
                (void)snprintf(status, sizeof(status), "That piece has no legal moves.");
                continue;
            }
            selected_row = *cursor_row;
            selected_col = *cursor_col;
            (void)snprintf(status, sizeof(status), "Piece selected. Choose a destination.");
            continue;
        }

        if (*cursor_row == selected_row && *cursor_col == selected_col) {
            selected_row = -1;
            selected_col = -1;
            (void)snprintf(status, sizeof(status), "Selection cleared.");
            continue;
        }

        {
            Move chosen;
            int color = state->white_to_move ? 1 : -1;
            if (find_selected_move(&legal, selected_row, selected_col, *cursor_row, *cursor_col, &chosen)) {
                *last_move = chosen;
                make_move(state, chosen);
                return 1;
            }
            if (piece_color(state->board[*cursor_row][*cursor_col]) == color &&
                legal_source_has_move(&legal, *cursor_row, *cursor_col)) {
                selected_row = *cursor_row;
                selected_col = *cursor_col;
                (void)snprintf(status, sizeof(status), "Selection changed.");
            } else {
                (void)snprintf(status, sizeof(status), "Illegal destination for that piece.");
            }
        }
    }
}

static void status_after_move(const GameState *state, char *status, size_t status_size) {
    MoveList replies;

    generate_legal_moves(state, &replies);
    if (replies.count == 0) {
        if (king_in_check(state, state->white_to_move)) {
            (void)snprintf(status, status_size, "CHECKMATE -- %s wins.", state->white_to_move ? "Black" : "White");
        } else {
            (void)snprintf(status, status_size, "STALEMATE -- no legal moves.");
        }
    } else if (king_in_check(state, state->white_to_move)) {
        (void)snprintf(status, status_size, "%s is in CHECK.", state->white_to_move ? "White" : "Black");
    } else if (state->halfmove_clock >= 100) {
        (void)snprintf(status, status_size, "Draw available by the fifty-move rule.");
    } else {
        (void)snprintf(status, status_size, "Awaiting %s command.", state->white_to_move ? "White" : "Black");
    }
}

static int game_over(const GameState *state) {
    MoveList moves;

    generate_legal_moves(state, &moves);
    return moves.count == 0;
}

int main(void) {
    GameState state;
    GameMode mode;
    Difficulty difficulty = DIFF_EASY;
    char status[128];
    Move last_move = { -1, -1, -1, -1, 0, 0, 0, -1, -1 };
    int cursor_row = 6;
    int cursor_col = 4;

    srand((unsigned int)time(NULL));
    mode = choose_mode();
    if (mode == MODE_PVC) {
        difficulty = choose_difficulty();
    }
    init_game(&state);
    enable_raw_mode();

    for (;;) {
        int computer_turn;

        status_after_move(&state, status, sizeof(status));
        computer_turn = mode == MODE_PVC && !state.white_to_move;
        if (game_over(&state)) {
            int key;
            render_board(&state, status, mode, difficulty, last_move, cursor_row, cursor_col,
                         -1, -1, 0);
            printf("Game over. Press r to restart or q to quit.\n");
            fflush(stdout);
            key = read_key();
            if (key == KEY_RESTART) {
                init_game(&state);
                last_move = (Move){ -1, -1, -1, -1, 0, 0, 0, -1, -1 };
                cursor_row = 6;
                cursor_col = 4;
                continue;
            }
            if (key == KEY_QUIT) {
                break;
            }
            continue;
        }

        if (computer_turn) {
            Move ai_move;
            (void)snprintf(status, sizeof(status), "Computer is thinking...");
            render_board(&state, status, mode, difficulty, last_move, cursor_row, cursor_col,
                         -1, -1, 0);
            ai_move = choose_ai_move(&state, difficulty);
            last_move = ai_move;
            make_move(&state, ai_move);
            cursor_row = ai_move.to_row;
            cursor_col = ai_move.to_col;
            continue;
        }

        {
            int result = human_turn(&state, mode, difficulty, &cursor_row, &cursor_col, &last_move);
            if (result == 0) {
                break;
            }
            if (result == 2) {
                init_game(&state);
                last_move = (Move){ -1, -1, -1, -1, 0, 0, 0, -1, -1 };
                cursor_row = 6;
                cursor_col = 4;
            }
        }
    }

    disable_raw_mode();
    clear_screen();
    printf("Thanks for playing BUDOSTACK Chess.\n");
    return EXIT_SUCCESS;
}
