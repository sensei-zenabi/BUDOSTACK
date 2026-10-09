#define _POSIX_C_SOURCE 200809L

#include "../lib/chess_engine.h"
#include "../lib/terminal_layout.h"
#include "../lib/terminal_input.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

#define INPUT_SIZE 64

static int raw_mode_enabled = 0;

static char piece_glyph(char piece) {
    if (piece == ' ') {
        return ' ';
    }
    return piece;
}

static void clear_screen(void) {
    printf("\033[2J\033[H");
}


static void disable_raw_mode(void) {
    budostack_terminal_input_stop();
    raw_mode_enabled = 0;
}

static void enable_raw_mode(void) {
    if (raw_mode_enabled || !isatty(STDIN_FILENO)) {
        return;
    }
    if (budostack_terminal_input_start() != 0) {
        exit(EXIT_FAILURE);
    }
    raw_mode_enabled = 1;
}

static int read_key(void) {
    int ch = budostack_terminal_read_key(100);
    switch (ch) {
        case BUDOSTACK_KEY_EOF: return KEY_QUIT;
        case BUDOSTACK_KEY_UP: return KEY_UP;
        case BUDOSTACK_KEY_DOWN: return KEY_DOWN;
        case BUDOSTACK_KEY_LEFT: return KEY_LEFT;
        case BUDOSTACK_KEY_RIGHT: return KEY_RIGHT;
        default: break;
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
    int padding = (budostack_get_target_cols() - width) / 2;

    if (padding < 0) {
        padding = 0;
    }
    printf("\033[2K");
    print_spaces(padding);
    printf("%.*s\n", budostack_get_target_cols() - padding - 1, text);
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
    int cols = budostack_get_target_cols();
    int field = (cols - 4) / 2;
    if (field > 24) {
        field = 24;
    }
    printf("\033[2K");
    print_spaces((cols - 2 * field - 2) / 2);
    printf("%-*.*s  %-*.*s\n", field, field, left, field, field, right);
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

    budostack_terminal_begin_frame();
    int cols = budostack_get_target_cols();
    int rows = budostack_get_target_rows();
    if (cols < 32 || rows < 27) {
        printf("\033[2K%.*s", cols, "Chess needs 32x27; enlarge window.");
        fflush(stdout);
        return;
    }
    int cell_w = (cols - 8) / 8;
    int cell_h = (rows - 19) / 8;
    if (cell_w < 3) {
        cell_w = 3;
    }
    if (cell_h < 1) {
        cell_h = 1;
    }
    int padding = (cols - cell_w * 8 - 6) / 2;
    if (padding < 0) {
        padding = 0;
    }
    int top = (rows - (19 + cell_h * 8)) / 2;
    for (int i = 0; i < top; i++) {
        putchar('\n');
    }
    print_centered_text("BUDOSTACK CHESS");
    if (mode == MODE_PVC) {
        (void)snprintf(line, sizeof(line), "%s  |  %s", mode_name, difficulty_name(difficulty));
    } else {
        (void)snprintf(line, sizeof(line), "%s", mode_name);
    }
    print_centered_text(line);
    print_centered_text(cols < 60 ? "WASD Space select R restart Q quit" :
                        "Arrows/WASD move   SPACE selects   R restarts   Q quits");
    printf("\n");
    print_centered_text(status);
    printf("\n");

    print_spaces(padding + 3);
    for (int col = 0; col < 8; col++) {
        printf("%*c%*s", cell_w / 2 + 1, 'a' + col, cell_w - cell_w / 2 - 1, "");
    }
    putchar('\n');
    print_spaces(padding + 2);
    putchar('+');
    for (int i = 0; i < cell_w * 8; i++) {
        putchar('-');
    }
    printf("+\n");
    for (int row = 0; row < 8; row++) {
        for (int line_no = 0; line_no < cell_h; line_no++) {
            print_spaces(padding);
            if (line_no == cell_h / 2) {
                printf("%d |", 8 - row);
            } else {
                printf("  |");
            }
            for (int col = 0; col < 8; col++) {
                if (line_no == cell_h / 2) {
                    print_spaces((cell_w - 3) / 2);
                    print_board_cell(state, row, col, show_cursor, cursor_row, cursor_col,
                                     selected_row, selected_col, last_move);
                    print_spaces(cell_w - 3 - (cell_w - 3) / 2);
                } else {
                    print_spaces(cell_w);
                }
            }
            printf("|\n");
        }
    }
    print_spaces(padding + 2);
    putchar('+');
    for (int i = 0; i < cell_w * 8; i++) {
        putchar('-');
    }
    printf("+\n");
    print_spaces(padding + 3);
    for (int col = 0; col < 8; col++) {
        printf("%*c%*s", cell_w / 2 + 1, 'a' + col, cell_w - cell_w / 2 - 1, "");
    }
    printf("\n\n");

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
        if (budostack_get_target_cols() < 32 || budostack_get_target_rows() < 27) {
            continue;
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
    setvbuf(stdin, NULL, _IONBF, 0);
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

        if (computer_turn && (budostack_get_target_cols() < 32 || budostack_get_target_rows() < 27)) {
            render_board(&state, status, mode, difficulty, last_move, cursor_row, cursor_col,
                         -1, -1, 0);
            if (read_key() == KEY_QUIT) {
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
