#ifndef BUDOSTACK_CHESS_ENGINE_H
#define BUDOSTACK_CHESS_ENGINE_H

#include <ctype.h>
#include <stdlib.h>

typedef enum {
    MODE_PVP,
    MODE_PVC
} GameMode;

typedef enum {
    DIFF_EASY,
    DIFF_MEDIUM,
    DIFF_HARD
} Difficulty;

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
static inline int is_white_piece(char piece) {
    return piece >= 'A' && piece <= 'Z';
}

static inline int is_black_piece(char piece) {
    return piece >= 'a' && piece <= 'z';
}

static inline int piece_color(char piece) {
    if (is_white_piece(piece)) {
        return 1;
    }
    if (is_black_piece(piece)) {
        return -1;
    }
    return 0;
}

static inline int in_bounds(int row, int col) {
    return row >= 0 && row < 8 && col >= 0 && col < 8;
}

static inline char lower_piece(char piece) {
    return (char)tolower((unsigned char)piece);
}

static inline int same_square(int row_a, int col_a, int row_b, int col_b) {
    return row_a == row_b && col_a == col_b;
}

static inline void init_game(GameState *state) {
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

static inline void add_move(MoveList *list, int from_row, int from_col, int to_row, int to_col,
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

static inline void add_promotion_moves(MoveList *list, int from_row, int from_col, int to_row, int to_col,
                                int white, int en_passant) {
    const char white_promos[] = { 'Q', 'R', 'B', 'N' };
    const char black_promos[] = { 'q', 'r', 'b', 'n' };
    const char *promos = white ? white_promos : black_promos;

    for (int i = 0; i < 4; i++) {
        add_move(list, from_row, from_col, to_row, to_col, promos[i], 0, en_passant);
    }
}

static inline int square_attacked(const GameState *state, int row, int col, int by_white) {
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

static inline int king_in_check(const GameState *state, int white_king) {
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

static inline void generate_pawn_moves(const GameState *state, MoveList *list, int row, int col, int white) {
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

static inline void generate_knight_moves(const GameState *state, MoveList *list, int row, int col, int color) {
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

static inline void generate_sliding_moves(const GameState *state, MoveList *list, int row, int col, int color,
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

static inline void generate_king_moves(const GameState *state, MoveList *list, int row, int col, int white) {
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

static inline void generate_pseudo_moves(const GameState *state, MoveList *list) {
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

static inline char make_move(GameState *state, Move move) {
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

static inline void generate_legal_moves(const GameState *state, MoveList *legal) {
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

static inline int piece_value(char piece) {
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

static inline int center_bonus(int row, int col) {
    int row_center = row > 3 ? row - 3 : 4 - row;
    int col_center = col > 3 ? col - 3 : 4 - col;
    return 14 - ((row_center + col_center) * 2);
}

static inline int evaluate_board(const GameState *state) {
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

static inline int move_order_score(const GameState *state, Move move) {
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

static inline void sort_moves(const GameState *state, MoveList *moves) {
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

static inline int minimax(GameState *state, int depth, int alpha, int beta) {
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

static inline Move choose_ai_move(const GameState *state, Difficulty difficulty) {
    MoveList moves;
    Move best_move;
    int best_score;
    int ai_white = state->white_to_move;
    int depth = difficulty == DIFF_HARD ? 3 : 1;

    generate_legal_moves(state, &moves);
    if (!moves.count) return (Move){0};
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

#endif
