#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>

#define BOARD_SIZE 5
#define WIN_CONDITION 4
#define MAX_LINE 128
#define INF 1000000000

static void init_board(char board[BOARD_SIZE][BOARD_SIZE]);
static void print_board(const char board[BOARD_SIZE][BOARD_SIZE]);
static char check_winner(const char board[BOARD_SIZE][BOARD_SIZE]);
static int board_full(const char board[BOARD_SIZE][BOARD_SIZE]);
static int prompt_mode(void);
static int prompt_first_player(void);
static void human_turn(char board[BOARD_SIZE][BOARD_SIZE], char player);
static void cpu_turn(char board[BOARD_SIZE][BOARD_SIZE], char ai_marker, char human_marker);
static int evaluate_board(const char board[BOARD_SIZE][BOARD_SIZE], char ai_marker, char human_marker);
static int minimax(char board[BOARD_SIZE][BOARD_SIZE], int depth, int alpha, int beta,
                   char current_player, char ai_marker, char human_marker, int empties,
                   int *best_row, int *best_col);
static int collect_moves(const char board[BOARD_SIZE][BOARD_SIZE], int moves[][2]);
static void sort_moves_by_proximity(int moves[][2], int count);
static int remaining_spaces(const char board[BOARD_SIZE][BOARD_SIZE]);

int main(void) {
    char board[BOARD_SIZE][BOARD_SIZE];
    init_board(board);

    printf("5x5 Tic-Tac-Toe (connect %d)\n", WIN_CONDITION);
    printf("Enter row and column numbers as values between 1 and %d.\n", BOARD_SIZE);
    printf("Type 'q' at any prompt to quit.\n\n");

    int mode = prompt_mode();
    if (mode == 0) {
        printf("Exiting...\n");
        return 0;
    }

    char current_player = 'X';
    int cpu_vs_cpu = 0;

    if (mode == 1) {
        int first = prompt_first_player();
        if (first == 0) {
            printf("Exiting...\n");
            return 0;
        }
        if (first == 2) {
            current_player = 'O';
        }
    } else if (mode == 3) {
        cpu_vs_cpu = 1;
    }

    while (1) {
        print_board(board);

        char winner = check_winner(board);
        if (winner == 'X' || winner == 'O') {
            printf("Player %c wins!\n", winner);
            break;
        }
        if (board_full(board)) {
            printf("It's a draw!\n");
            break;
        }

        if (cpu_vs_cpu || (mode == 1 && current_player != 'X')) {
            if (cpu_vs_cpu) {
                printf("CPU (%c) is thinking...\n", current_player);
            } else {
                printf("Computer's turn (%c).\n", current_player);
            }
            cpu_turn(board, current_player, current_player == 'X' ? 'O' : 'X');
        } else {
            human_turn(board, current_player);
        }

        current_player = (current_player == 'X') ? 'O' : 'X';
    }

    print_board(board);
    return 0;
}

static void init_board(char board[BOARD_SIZE][BOARD_SIZE]) {
    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            board[r][c] = ' ';
        }
    }
}

static void print_board(const char board[BOARD_SIZE][BOARD_SIZE]) {
    printf("   ");
    for (int c = 0; c < BOARD_SIZE; ++c) {
        printf(" %d  ", c + 1);
    }
    printf("\n");

    for (int r = 0; r < BOARD_SIZE; ++r) {
        printf("   ");
        for (int c = 0; c < BOARD_SIZE; ++c) {
            printf("----");
        }
        printf("-\n");

        printf(" %d ", r + 1);
        for (int c = 0; c < BOARD_SIZE; ++c) {
            printf("| %c ", board[r][c]);
        }
        printf("|\n");
    }

    printf("   ");
    for (int c = 0; c < BOARD_SIZE; ++c) {
        printf("----");
    }
    printf("-\n\n");
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

static void human_turn(char board[BOARD_SIZE][BOARD_SIZE], char player) {
    while (1) {
        char buffer[MAX_LINE];
        printf("Player %c, enter your move (row col): ", player);
        if (!fgets(buffer, sizeof(buffer), stdin)) {
            printf("Unexpected input error. Exiting.\n");
            exit(EXIT_FAILURE);
        }
        if (buffer[0] == 'q' || buffer[0] == 'Q') {
            printf("Quitting game.\n");
            exit(0);
        }
        int row = 0;
        int col = 0;
        if (sscanf(buffer, "%d %d", &row, &col) != 2) {
            printf("Invalid input. Please enter row and column numbers separated by a space.\n");
            continue;
        }
        if (row < 1 || row > BOARD_SIZE || col < 1 || col > BOARD_SIZE) {
            printf("Values must be between 1 and %d.\n", BOARD_SIZE);
            continue;
        }
        row -= 1;
        col -= 1;
        if (board[row][col] != ' ') {
            printf("That cell is already occupied. Try again.\n");
            continue;
        }
        board[row][col] = player;
        break;
    }
}

static int remaining_spaces(const char board[BOARD_SIZE][BOARD_SIZE]) {
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

static void cpu_turn(char board[BOARD_SIZE][BOARD_SIZE], char ai_marker, char human_marker) {
    int empties = remaining_spaces(board);
    int depth_limit;
    if (empties > 16) {
        depth_limit = 4;
    } else if (empties > 12) {
        depth_limit = 5;
    } else if (empties > 8) {
        depth_limit = 6;
    } else if (empties > 5) {
        depth_limit = 7;
    } else {
        depth_limit = 9;
    }

    int best_row = -1;
    int best_col = -1;
    minimax(board, depth_limit, -INF, INF, ai_marker, ai_marker, human_marker, empties, &best_row, &best_col);
    if (best_row < 0 || best_col < 0) {
        // Fallback: choose first available space (should rarely happen)
        for (int r = 0; r < BOARD_SIZE && best_row < 0; ++r) {
            for (int c = 0; c < BOARD_SIZE; ++c) {
                if (board[r][c] == ' ') {
                    best_row = r;
                    best_col = c;
                    break;
                }
            }
        }
    }
    if (best_row >= 0 && best_col >= 0) {
        board[best_row][best_col] = ai_marker;
        printf("Computer chose (%d, %d).\n", best_row + 1, best_col + 1);
    }
}

static int board_full(const char board[BOARD_SIZE][BOARD_SIZE]) {
    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            if (board[r][c] == ' ') {
                return 0;
            }
        }
    }
    return 1;
}

static char check_winner(const char board[BOARD_SIZE][BOARD_SIZE]) {
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
                int rr = r;
                int cc = c;
                int match = 1;
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

static int evaluate_board(const char board[BOARD_SIZE][BOARD_SIZE], char ai_marker, char human_marker) {
    char winner = check_winner(board);
    if (winner == ai_marker) {
        return 1000000;
    }
    if (winner == human_marker) {
        return -1000000;
    }

    static const int pattern_score[WIN_CONDITION + 1] = {0, 2, 8, 64, 2048};
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
                int human_count = 0;
                int empty_count = 0;
                for (int step = 0; step < WIN_CONDITION; ++step) {
                    char marker = board[r + step * dr][c + step * dc];
                    if (marker == ai_marker) {
                        ++ai_count;
                    } else if (marker == human_marker) {
                        ++human_count;
                    } else {
                        ++empty_count;
                    }
                }
                if (ai_count > 0 && human_count == 0) {
                    score += pattern_score[ai_count] * (empty_count + 1);
                } else if (human_count > 0 && ai_count == 0) {
                    score -= pattern_score[human_count] * (empty_count + 1);
                }
            }
        }
    }
    return score;
}

static int minimax(char board[BOARD_SIZE][BOARD_SIZE], int depth, int alpha, int beta,
                   char current_player, char ai_marker, char human_marker, int empties,
                   int *best_row, int *best_col) {
    char winner = check_winner(board);
    if (winner == ai_marker) {
        return 1000000 + depth;
    }
    if (winner == human_marker) {
        return -1000000 - depth;
    }
    if (empties == 0 || depth == 0) {
        return evaluate_board(board, ai_marker, human_marker);
    }

    int moves[BOARD_SIZE * BOARD_SIZE][2];
    int move_count = collect_moves(board, moves);

    if (current_player == ai_marker) {
        int best_value = -INF;
        for (int i = 0; i < move_count; ++i) {
            int r = moves[i][0];
            int c = moves[i][1];
            board[r][c] = current_player;
            int value = minimax(board, depth - 1, alpha, beta,
                                human_marker, ai_marker, human_marker, empties - 1, NULL, NULL);
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
        board[r][c] = current_player;
        int value = minimax(board, depth - 1, alpha, beta,
                            ai_marker, ai_marker, human_marker, empties - 1, NULL, NULL);
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

static int collect_moves(const char board[BOARD_SIZE][BOARD_SIZE], int moves[][2]) {
    int count = 0;
    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            if (board[r][c] == ' ') {
                moves[count][0] = r;
                moves[count][1] = c;
                ++count;
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

