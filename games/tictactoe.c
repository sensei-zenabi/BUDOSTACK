#define _POSIX_C_SOURCE 200809L

#include "../lib/terminal_layout.h"
#include "../lib/terminal_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BOARD_SIZE 17
#define WIN_CONDITION 5
#define MAX_LINE 128
#define INF 1000000000

static void init_board(char board[BOARD_SIZE][BOARD_SIZE]);
static void render_game(char board[BOARD_SIZE][BOARD_SIZE], char current_player,
                        int cursor_row, int cursor_col, int show_cursor,
                        int last_move_row, int last_move_col, const char *mode_name);
static char check_winner(char board[BOARD_SIZE][BOARD_SIZE]);
static int board_full(char board[BOARD_SIZE][BOARD_SIZE]);
static int prompt_mode(void);
static int prompt_first_player(void);
static void enable_raw_mode(void);
static void disable_raw_mode(void);
static void human_turn(char board[BOARD_SIZE][BOARD_SIZE], char player,
                       int *cursor_row, int *cursor_col, int *last_move_row,
                       int *last_move_col, const char *mode_name);
static void cpu_turn(char board[BOARD_SIZE][BOARD_SIZE], char ai_marker,
                     char opponent_marker, int *move_row, int *move_col);
static int evaluate_board(char board[BOARD_SIZE][BOARD_SIZE], char ai_marker,
                          char opponent_marker);
static int minimax(char board[BOARD_SIZE][BOARD_SIZE], int depth, int alpha, int beta,
                   char current_player, char ai_marker, char opponent_marker,
                   int empties, int *best_row, int *best_col);
static int collect_moves(char board[BOARD_SIZE][BOARD_SIZE], int moves[][2]);
static void sort_moves_by_proximity(int moves[][2], int count);
static int remaining_spaces(char board[BOARD_SIZE][BOARD_SIZE]);
static int read_key(void);
static int find_winning_move(char board[BOARD_SIZE][BOARD_SIZE], char marker,
                             int *best_row, int *best_col);

static int raw_mode_enabled = 0;
static char status_line[256];

enum InputKey {
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_SELECT,
    KEY_QUIT
};

int main(void) {
    setvbuf(stdin, NULL, _IONBF, 0);
    char board[BOARD_SIZE][BOARD_SIZE];
    init_board(board);

    printf("%dx%d Tic-Tac-Toe (connect %d)\n\n", BOARD_SIZE, BOARD_SIZE, WIN_CONDITION);
    printf("Arrow keys move, Space/Enter place, q quits.\n");
    printf("The board adapts to the current terminal size.\n\n");

    int mode = prompt_mode();
    if (mode == 0) {
        printf("Exiting...\n");
        return 0;
    }

    int human_x = 0;
    int human_o = 0;
    char current_player = 'X';

    if (mode == 1) {
        int first = prompt_first_player();
        if (first == 0) {
            printf("Exiting...\n");
            return 0;
        }
        if (first == 1) {
            human_x = 1;
            human_o = 0;
        } else {
            human_x = 0;
            human_o = 1;
        }
        current_player = 'X';
    } else if (mode == 2) {
        human_x = 1;
        human_o = 1;
    } else {
        human_x = 0;
        human_o = 0;
    }

    const char *mode_name = "Player vs Computer";
    if (mode == 2) {
        mode_name = "Player vs Player";
    } else if (mode == 3) {
        mode_name = "Computer vs Computer";
    }

    enable_raw_mode();

    int cursor_row = BOARD_SIZE / 2;
    int cursor_col = BOARD_SIZE / 2;
    int last_move_row = -1;
    int last_move_col = -1;

    if ((current_player == 'X' && human_x) || (current_player == 'O' && human_o)) {
        snprintf(status_line, sizeof(status_line), "Player %c to move.", current_player);
    } else {
        snprintf(status_line, sizeof(status_line), "Computer (%c) is thinking...", current_player);
    }

    int running = 1;
    while (running && !budostack_terminal_interrupted()) {
        if (budostack_get_target_cols() < 21 || budostack_get_target_rows() < 24) {
            render_game(board, current_player, cursor_row, cursor_col, 0,
                        last_move_row, last_move_col, mode_name);
            if (read_key() == KEY_QUIT) {
                break;
            }
            continue;
        }
        int human_turn_flag = (current_player == 'X') ? human_x : human_o;
        if (human_turn_flag) {
            render_game(board, current_player, cursor_row, cursor_col, 1,
                        last_move_row, last_move_col, mode_name);
            human_turn(board, current_player, &cursor_row, &cursor_col,
                       &last_move_row, &last_move_col, mode_name);
        } else {
            int input = budostack_terminal_read_key(0);
            if (input == BUDOSTACK_KEY_EOF || input == 'q' || input == 'Q') {
                break;
            }
            snprintf(status_line, sizeof(status_line), "Computer (%c) is thinking...", current_player);
            render_game(board, current_player, cursor_row, cursor_col, 0,
                        last_move_row, last_move_col, mode_name);
            int move_row = -1;
            int move_col = -1;
            cpu_turn(board, current_player, current_player == 'X' ? 'O' : 'X',
                     &move_row, &move_col);
            if (move_row >= 0 && move_col >= 0) {
                last_move_row = move_row;
                last_move_col = move_col;
                cursor_row = move_row;
                cursor_col = move_col;
            }
            render_game(board, current_player, cursor_row, cursor_col, 0,
                        last_move_row, last_move_col, mode_name);
            if (mode == 3) {
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = 200000000L;
                nanosleep(&ts, NULL);
            }
        }

        char winner = check_winner(board);
        if (winner == 'X' || winner == 'O') {
            snprintf(status_line, sizeof(status_line), "Player %c wins!", winner);
            render_game(board, winner, cursor_row, cursor_col, 0,
                        last_move_row, last_move_col, mode_name);
            running = 0;
            break;
        }

        if (board_full(board)) {
            snprintf(status_line, sizeof(status_line), "It's a draw!");
            render_game(board, current_player, cursor_row, cursor_col, 0,
                        last_move_row, last_move_col, mode_name);
            running = 0;
            break;
        }

        current_player = (current_player == 'X') ? 'O' : 'X';
        if ((current_player == 'X' && human_x) || (current_player == 'O' && human_o)) {
            snprintf(status_line, sizeof(status_line), "Player %c to move.", current_player);
        } else {
            snprintf(status_line, sizeof(status_line), "Computer (%c) is thinking...", current_player);
        }
    }

    disable_raw_mode();
    printf("%.*s\n", budostack_get_target_cols() - 1, status_line);
    return 0;
}

static void init_board(char board[BOARD_SIZE][BOARD_SIZE]) {
    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            board[r][c] = ' ';
        }
    }
}

static void render_game(char board[BOARD_SIZE][BOARD_SIZE], char current_player,
                        int cursor_row, int cursor_col, int show_cursor,
                        int last_move_row, int last_move_col, const char *mode_name) {
    int cols = budostack_get_target_cols();
    int rows = budostack_get_target_rows();
    int cell_w = (cols - 4) / BOARD_SIZE;
    int cell_h = (rows - 7) / BOARD_SIZE;
    if (cell_w < 1 || cell_h < 1) {
        budostack_terminal_begin_frame();
        printf("\033[2K%.*s", cols, "Connect 5 needs 21x24; enlarge window.");
        fflush(stdout);
        return;
    }
    int width = cell_w * BOARD_SIZE;
    int height = cell_h * BOARD_SIZE;
    int left = (cols - width - 2) / 2 + 1;
    int top = (rows - height - 7) / 2 + 1;
    budostack_terminal_begin_frame();
    printf("\033[%d;1H\033[2K\033[%d;%dHCONNECT %d  Player %c", top, top, left,
           WIN_CONDITION, current_player);
    printf("\033[%d;1H\033[2K\033[%d;%dH%.*s", top + 1, top + 1, left, width, mode_name);
    printf("\033[%d;1H\033[2K\033[%d;%dH%.*s", top + 2, top + 2, left, width, status_line);
    printf("\033[%d;%dH┌", top + 3, left);
    for (int x = 0; x < width; x++) {
        fputs("─", stdout);
    }
    fputs("┐", stdout);
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int line = 0; line < cell_h; line++) {
            int separator = cell_h > 1 && r > 0 && line == 0;
            printf("\033[%d;%dH", top + 4 + r * cell_h + line, left);
            fputs(separator ? "├" : "│", stdout);
            for (int c = 0; c < BOARD_SIZE; c++) {
                int cursor = show_cursor && r == cursor_row && c == cursor_col;
                int last = r == last_move_row && c == last_move_col;
                if (cursor) {
                    printf("\033[7m");
                } else if (last) {
                    printf("\033[1m");
                }
                for (int x = 0; x < cell_w; x++) {
                    if (separator) {
                        fputs(x == 0 && c > 0 ? "┼" : "─", stdout);
                    } else if (x == cell_w / 2 && line == cell_h / 2) {
                        putchar(board[r][c] == ' ' ? '.' : board[r][c]);
                    } else if (cell_w > 2 && c > 0 && x == 0) {
                        fputs("│", stdout);
                    } else {
                        putchar(' ');
                    }
                }
                printf("\033[0m");
            }
            fputs(separator ? "┤" : "│", stdout);
        }
    }
    printf("\033[%d;%dH└", top + height + 4, left);
    for (int x = 0; x < width; x++) {
        fputs("─", stdout);
    }
    fputs("┘", stdout);
    printf("\033[%d;%dH%.*s", top + height + 5, left, width + 2,
           "WASD/Arrows Space place Q quit");
    fflush(stdout);
}

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

static int prompt_mode(void) {
    while (1) {
        char buffer[MAX_LINE];
        printf("Select mode:\n");
        printf(" 1) Player vs Computer\n");
        printf(" 2) Player vs Player\n");
        printf(" 3) Computer vs Computer demo\n");
        printf("Choice: ");
        if (!fgets(buffer, sizeof(buffer), stdin)) {
            return 0;
        }
        if (buffer[0] == 'q' || buffer[0] == 'Q') {
            return 0;
        }
        if (buffer[0] == '\0' || buffer[0] == '\n') {
            continue;
        }
        int choice = buffer[0] - '0';
        if (choice >= 1 && choice <= 3) {
            return choice;
        }
        printf("Invalid selection. Please enter 1, 2, or 3.\n\n");
    }
}

static int prompt_first_player(void) {
    while (1) {
        char buffer[MAX_LINE];
        printf("Who should go first?\n");
        printf(" 1) Player\n");
        printf(" 2) Computer\n");
        printf("Choice: ");
        if (!fgets(buffer, sizeof(buffer), stdin)) {
            return 0;
        }
        if (buffer[0] == 'q' || buffer[0] == 'Q') {
            return 0;
        }
        if (buffer[0] == '\0' || buffer[0] == '\n') {
            continue;
        }
        int choice = buffer[0] - '0';
        if (choice == 1 || choice == 2) {
            return choice;
        }
        printf("Invalid selection. Please enter 1 or 2.\n\n");
    }
}

static void enable_raw_mode(void) {
    if (budostack_terminal_input_start() != 0) {
        exit(EXIT_FAILURE);
    }
    raw_mode_enabled = 1;
}

static void disable_raw_mode(void) {
    budostack_terminal_input_stop();
    raw_mode_enabled = 0;
}

static void human_turn(char board[BOARD_SIZE][BOARD_SIZE], char player,
                       int *cursor_row, int *cursor_col, int *last_move_row,
                       int *last_move_col, const char *mode_name) {
    while (1) {
        int key = read_key();
        if (key == KEY_NONE) {
            render_game(board, player, *cursor_row, *cursor_col, 1,
                        *last_move_row, *last_move_col, mode_name);
            continue;
        }
        if (key == KEY_QUIT) {
            disable_raw_mode();
            printf("\nPlayer quit the game.\n");
            exit(0);
        }
        if (budostack_get_target_cols() < 21 || budostack_get_target_rows() < 24) {
            continue;
        }
        if (key == KEY_UP) {
            if (*cursor_row > 0) {
                --(*cursor_row);
            }
            render_game(board, player, *cursor_row, *cursor_col, 1,
                        *last_move_row, *last_move_col, mode_name);
            continue;
        }
        if (key == KEY_DOWN) {
            if (*cursor_row < BOARD_SIZE - 1) {
                ++(*cursor_row);
            }
            render_game(board, player, *cursor_row, *cursor_col, 1,
                        *last_move_row, *last_move_col, mode_name);
            continue;
        }
        if (key == KEY_LEFT) {
            if (*cursor_col > 0) {
                --(*cursor_col);
            }
            render_game(board, player, *cursor_row, *cursor_col, 1,
                        *last_move_row, *last_move_col, mode_name);
            continue;
        }
        if (key == KEY_RIGHT) {
            if (*cursor_col < BOARD_SIZE - 1) {
                ++(*cursor_col);
            }
            render_game(board, player, *cursor_row, *cursor_col, 1,
                        *last_move_row, *last_move_col, mode_name);
            continue;
        }
        if (key == KEY_SELECT) {
            if (board[*cursor_row][*cursor_col] != ' ') {
                snprintf(status_line, sizeof(status_line),
                         "Cell (%d, %d) is occupied. Choose another square.",
                         *cursor_row + 1, *cursor_col + 1);
                render_game(board, player, *cursor_row, *cursor_col, 1,
                            *last_move_row, *last_move_col, mode_name);
                continue;
            }
            board[*cursor_row][*cursor_col] = player;
            *last_move_row = *cursor_row;
            *last_move_col = *cursor_col;
            snprintf(status_line, sizeof(status_line),
                     "Player %c placed at row %d, column %d.",
                     player, *cursor_row + 1, *cursor_col + 1);
            render_game(board, player, *cursor_row, *cursor_col, 0,
                        *last_move_row, *last_move_col, mode_name);
            return;
        }
    }
}

static int remaining_spaces(char board[BOARD_SIZE][BOARD_SIZE]) {
    int count = 0;
    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            if (board[r][c] == ' ') {
                ++count;
            }
        }
    }
    return count;
}

static int find_winning_move(char board[BOARD_SIZE][BOARD_SIZE], char marker,
                             int *best_row, int *best_col) {
    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            if (board[r][c] != ' ') {
                continue;
            }
            board[r][c] = marker;
            char winner = check_winner(board);
            board[r][c] = ' ';
            if (winner == marker) {
                *best_row = r;
                *best_col = c;
                return 1;
            }
        }
    }
    return 0;
}

static void cpu_turn(char board[BOARD_SIZE][BOARD_SIZE], char ai_marker,
                     char opponent_marker, int *move_row, int *move_col) {
    int row = -1;
    int col = -1;

    if (find_winning_move(board, ai_marker, &row, &col)) {
        board[row][col] = ai_marker;
    } else if (find_winning_move(board, opponent_marker, &row, &col)) {
        board[row][col] = ai_marker;
    } else {
        int empties = remaining_spaces(board);
        int depth_limit;
        if (empties > 200) {
            depth_limit = 2;
        } else if (empties > 60) {
            depth_limit = 3;
        } else if (empties > 20) {
            depth_limit = 4;
        } else {
            depth_limit = 5;
        }
        row = -1;
        col = -1;
        minimax(board, depth_limit, -INF, INF, ai_marker, ai_marker,
                opponent_marker, empties, &row, &col);
        if (row < 0 || col < 0) {
            for (int r = 0; r < BOARD_SIZE && row < 0; ++r) {
                for (int c = 0; c < BOARD_SIZE; ++c) {
                    if (board[r][c] == ' ') {
                        row = r;
                        col = c;
                        break;
                    }
                }
            }
        }
        if (row >= 0 && col >= 0) {
            board[row][col] = ai_marker;
        }
    }

    if (row >= 0 && col >= 0) {
        if (move_row != NULL) {
            *move_row = row;
        }
        if (move_col != NULL) {
            *move_col = col;
        }
        snprintf(status_line, sizeof(status_line),
                 "Computer (%c) placed at row %d, column %d.",
                 ai_marker, row + 1, col + 1);
    }
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

static int minimax(char board[BOARD_SIZE][BOARD_SIZE], int depth, int alpha, int beta,
                   char current_player, char ai_marker, char opponent_marker,
                   int empties, int *best_row, int *best_col) {
    char winner = check_winner(board);
    if (winner == ai_marker) {
        return 1000000 + depth;
    }
    if (winner == opponent_marker) {
        return -1000000 - depth;
    }
    if (empties == 0 || depth == 0) {
        return evaluate_board(board, ai_marker, opponent_marker);
    }

    int moves[BOARD_SIZE * BOARD_SIZE][2];
    int move_count = collect_moves(board, moves);
    if (move_count == 0) {
        return evaluate_board(board, ai_marker, opponent_marker);
    }

    char next_player = (current_player == 'X') ? 'O' : 'X';

    if (current_player == ai_marker) {
        int best_value = -INF;
        for (int i = 0; i < move_count; ++i) {
            int r = moves[i][0];
            int c = moves[i][1];
            if (board[r][c] != ' ') {
                continue;
            }
            board[r][c] = current_player;
            int value = minimax(board, depth - 1, alpha, beta, next_player,
                                ai_marker, opponent_marker, empties - 1, NULL, NULL);
            board[r][c] = ' ';
            if (value > best_value) {
                best_value = value;
                if (best_row != NULL && best_col != NULL) {
                    *best_row = r;
                    *best_col = c;
                }
            }
            if (value > alpha) {
                alpha = value;
            }
            if (beta <= alpha) {
                break;
            }
        }
        return best_value;
    }

    int best_value = INF;
    for (int i = 0; i < move_count; ++i) {
        int r = moves[i][0];
        int c = moves[i][1];
        if (board[r][c] != ' ') {
            continue;
        }
        board[r][c] = current_player;
        int value = minimax(board, depth - 1, alpha, beta, next_player,
                            ai_marker, opponent_marker, empties - 1, NULL, NULL);
        board[r][c] = ' ';
        if (value < best_value) {
            best_value = value;
            if (best_row != NULL && best_col != NULL) {
                *best_row = r;
                *best_col = c;
            }
        }
        if (value < beta) {
            beta = value;
        }
        if (beta <= alpha) {
            break;
        }
    }
    return best_value;
}

static int collect_moves(char board[BOARD_SIZE][BOARD_SIZE], int moves[][2]) {
    int has_marker = 0;
    int min_r = BOARD_SIZE;
    int max_r = -1;
    int min_c = BOARD_SIZE;
    int max_c = -1;

    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            if (board[r][c] != ' ') {
                has_marker = 1;
                if (r < min_r) {
                    min_r = r;
                }
                if (r > max_r) {
                    max_r = r;
                }
                if (c < min_c) {
                    min_c = c;
                }
                if (c > max_c) {
                    max_c = c;
                }
            }
        }
    }

    if (!has_marker) {
        moves[0][0] = BOARD_SIZE / 2;
        moves[0][1] = BOARD_SIZE / 2;
        return 1;
    }

    const int margin = 3;
    int start_r = min_r - margin;
    int end_r = max_r + margin;
    int start_c = min_c - margin;
    int end_c = max_c + margin;
    if (start_r < 0) {
        start_r = 0;
    }
    if (start_c < 0) {
        start_c = 0;
    }
    if (end_r >= BOARD_SIZE) {
        end_r = BOARD_SIZE - 1;
    }
    if (end_c >= BOARD_SIZE) {
        end_c = BOARD_SIZE - 1;
    }

    int count = 0;
    for (int r = start_r; r <= end_r; ++r) {
        for (int c = start_c; c <= end_c; ++c) {
            if (board[r][c] == ' ') {
                moves[count][0] = r;
                moves[count][1] = c;
                ++count;
            }
        }
    }

    if (count == 0) {
        for (int r = 0; r < BOARD_SIZE; ++r) {
            for (int c = 0; c < BOARD_SIZE; ++c) {
                if (board[r][c] == ' ') {
                    moves[count][0] = r;
                    moves[count][1] = c;
                    ++count;
                }
            }
        }
    }

    sort_moves_by_proximity(moves, count);
    return count;
}

static void sort_moves_by_proximity(int moves[][2], int count) {
    int center = BOARD_SIZE / 2;
    for (int i = 0; i < count - 1; ++i) {
        for (int j = i + 1; j < count; ++j) {
            int di = abs(moves[i][0] - center) + abs(moves[i][1] - center);
            int dj = abs(moves[j][0] - center) + abs(moves[j][1] - center);
            if (dj < di) {
                int tmp_r = moves[i][0];
                int tmp_c = moves[i][1];
                moves[i][0] = moves[j][0];
                moves[i][1] = moves[j][1];
                moves[j][0] = tmp_r;
                moves[j][1] = tmp_c;
            }
        }
    }
}

static int read_key(void) {
    int c = budostack_terminal_read_key(100);
    switch (c) {
        case BUDOSTACK_KEY_EOF: return KEY_QUIT;
        case BUDOSTACK_KEY_UP: return KEY_UP;
        case BUDOSTACK_KEY_DOWN: return KEY_DOWN;
        case BUDOSTACK_KEY_LEFT: return KEY_LEFT;
        case BUDOSTACK_KEY_RIGHT: return KEY_RIGHT;
        default: break;
    }
    if (c == 'q' || c == 'Q') {
        return KEY_QUIT;
    }
    if (c == ' ' || c == '\r' || c == '\n') {
        return KEY_SELECT;
    }
    if (c == 'w' || c == 'W') {
        return KEY_UP;
    }
    if (c == 's' || c == 'S') {
        return KEY_DOWN;
    }
    if (c == 'a' || c == 'A') {
        return KEY_LEFT;
    }
    if (c == 'd' || c == 'D') {
        return KEY_RIGHT;
    }
    return KEY_NONE;
}
