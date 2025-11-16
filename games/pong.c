#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_COLUMNS 118
#define DEFAULT_ROWS 66
#define MIN_COLUMNS 60
#define MIN_ROWS 24

#define FRAME_INTERVAL_NS 16000000L

enum GameMode {
    GAME_MODE_PVP = 1,
    GAME_MODE_PVC = 2
};

struct GameState {
    enum GameMode mode;
    int running;
    int paused;
    int columns;
    int rows;
    int field_left;
    int field_right;
    int field_top;
    int field_bottom;
    int field_height;
    int left_paddle_x;
    int right_paddle_x;
    int paddle_height;
    double left_paddle_y;
    double right_paddle_y;
    double ball_x;
    double ball_y;
    double ball_vx;
    double ball_vy;
    int score_left;
    int score_right;
    int serve_direction;
};

static struct termios orig_termios;
static int termios_saved = 0;
static int stdin_flags = -1;
static int cursor_hidden = 0;
static int escape_state = 0;

static void disable_raw_mode(void);

static void show_cursor(void) {
    if(cursor_hidden) {
        printf("\033[?25h");
        cursor_hidden = 0;
        fflush(stdout);
    }
}

static void hide_cursor(void) {
    if(!cursor_hidden) {
        printf("\033[?25l");
        cursor_hidden = 1;
        fflush(stdout);
    }
}

static void disable_raw_mode(void) {
    show_cursor();
    if(termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        termios_saved = 0;
    }
    if(stdin_flags != -1) {
        fcntl(STDIN_FILENO, F_SETFL, stdin_flags);
        stdin_flags = -1;
    }
}

static void enable_raw_mode(void) {
    if(termios_saved)
        return;
    if(tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        exit(1);
    }
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(1);
    }
    termios_saved = 1;
    stdin_flags = fcntl(STDIN_FILENO, F_GETFL);
    if(stdin_flags == -1) {
        perror("fcntl");
        exit(1);
    }
    if(fcntl(STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK) == -1) {
        perror("fcntl");
        exit(1);
    }
    atexit(disable_raw_mode);
    hide_cursor();
}

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void sleep_frame(void) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = FRAME_INTERVAL_NS;
    nanosleep(&ts, NULL);
}

static void query_terminal_size(int *columns, int *rows) {
    struct winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *columns = ws.ws_col;
        *rows = ws.ws_row;
        return;
    }
    *columns = DEFAULT_COLUMNS;
    *rows = DEFAULT_ROWS;
}

static double clamp_double(double value, double min_value, double max_value) {
    if(value < min_value)
        return min_value;
    if(value > max_value)
        return max_value;
    return value;
}

static void recenter_paddles(struct GameState *state) {
    double mid = ((double)state->field_top + (double)state->field_bottom) / 2.0;
    state->left_paddle_y = mid;
    state->right_paddle_y = mid;
}

static void apply_dimensions(struct GameState *state, int columns, int rows, int recenter_positions) {
    if(columns < MIN_COLUMNS)
        columns = MIN_COLUMNS;
    if(rows < MIN_ROWS)
        rows = MIN_ROWS;
    state->columns = columns;
    state->rows = rows;
    state->field_left = 1;
    state->field_right = columns - 2;
    state->field_top = 2;
    state->field_bottom = rows - 3;
    if(state->field_bottom <= state->field_top)
        state->field_bottom = state->field_top + 1;
    state->field_height = state->field_bottom - state->field_top + 1;
    state->left_paddle_x = 2;
    state->right_paddle_x = columns - 3;
    state->paddle_height = state->field_height / 5;
    if(state->paddle_height < 4)
        state->paddle_height = 4;
    if(state->paddle_height % 2 == 1)
        state->paddle_height++;
    if(recenter_positions) {
        recenter_paddles(state);
        state->ball_x = ((double)state->field_left + (double)state->field_right) / 2.0;
        state->ball_y = ((double)state->field_top + (double)state->field_bottom) / 2.0;
    } else {
        double half = (double)state->paddle_height / 2.0;
        double min_y = (double)state->field_top + half;
        double max_y = (double)state->field_bottom - half;
        if(max_y < min_y)
            min_y = max_y = ((double)state->field_top + (double)state->field_bottom) / 2.0;
        state->left_paddle_y = clamp_double(state->left_paddle_y, min_y, max_y);
        state->right_paddle_y = clamp_double(state->right_paddle_y, min_y, max_y);
        state->ball_x = clamp_double(state->ball_x, (double)state->field_left, (double)state->field_right);
        state->ball_y = clamp_double(state->ball_y, (double)state->field_top, (double)state->field_bottom);
    }
}

static void launch_ball(struct GameState *state, int direction) {
    double center_x = ((double)state->field_left + (double)state->field_right) / 2.0;
    double center_y = ((double)state->field_top + (double)state->field_bottom) / 2.0;
    state->ball_x = center_x;
    state->ball_y = center_y;
    double base_speed = (double)state->columns / 3.0;
    if(base_speed < 20.0)
        base_speed = 20.0;
    double spread = ((double)(rand() % 201) - 100.0) / 100.0;
    state->ball_vx = base_speed * (direction >= 0 ? 1.0 : -1.0);
    state->ball_vy = base_speed * 0.5 * spread;
    if(fabs(state->ball_vy) < base_speed * 0.1) {
        state->ball_vy = base_speed * 0.1 * (state->ball_vy >= 0.0 ? 1.0 : -1.0);
    }
    state->serve_direction = direction >= 0 ? 1 : -1;
}

static void reset_scores(struct GameState *state) {
    state->score_left = 0;
    state->score_right = 0;
    state->serve_direction = (rand() % 2) ? 1 : -1;
    recenter_paddles(state);
    launch_ball(state, state->serve_direction);
}

static void clamp_paddle(const struct GameState *state, double *y) {
    double half = (double)state->paddle_height / 2.0;
    double min_y = (double)state->field_top + half;
    double max_y = (double)state->field_bottom - half;
    if(max_y < min_y)
        min_y = max_y = ((double)state->field_top + (double)state->field_bottom) / 2.0;
    *y = clamp_double(*y, min_y, max_y);
}

static void award_point(struct GameState *state, int left_player_scored) {
    if(left_player_scored)
        state->score_left++;
    else
        state->score_right++;
    state->serve_direction = left_player_scored ? 1 : -1;
    recenter_paddles(state);
    launch_ball(state, state->serve_direction);
}

static void apply_player_movement(struct GameState *state, int left_moves, int right_moves) {
    double step = (double)state->field_height * 0.04;
    if(step < 1.0)
        step = 1.0;
    state->left_paddle_y += (double)left_moves * step;
    clamp_paddle(state, &state->left_paddle_y);
    if(state->mode == GAME_MODE_PVP) {
        state->right_paddle_y += (double)right_moves * step;
        clamp_paddle(state, &state->right_paddle_y);
    }
}

static void update_ai(struct GameState *state, double dt) {
    if(state->mode != GAME_MODE_PVC)
        return;
    double target = state->ball_y;
    double diff = target - state->right_paddle_y;
    double tolerance = (double)state->paddle_height * 0.1 + 0.5;
    if(fabs(diff) < tolerance)
        return;
    double ai_speed = (double)state->field_height * 1.1;
    state->right_paddle_y += (diff > 0 ? 1.0 : -1.0) * ai_speed * dt;
    clamp_paddle(state, &state->right_paddle_y);
}

static void update_ball(struct GameState *state, double dt) {
    state->ball_x += state->ball_vx * dt;
    state->ball_y += state->ball_vy * dt;

    if(state->ball_y <= (double)state->field_top) {
        state->ball_y = (double)state->field_top;
        state->ball_vy = fabs(state->ball_vy);
    } else if(state->ball_y >= (double)state->field_bottom) {
        state->ball_y = (double)state->field_bottom;
        state->ball_vy = -fabs(state->ball_vy);
    }

    double half = (double)state->paddle_height / 2.0;
    double influence = half > 0.0 ? half : 1.0;

    if(state->ball_vx < 0.0 && state->ball_x <= (double)state->left_paddle_x + 0.5) {
        if(state->ball_y >= state->left_paddle_y - influence - 0.5 &&
           state->ball_y <= state->left_paddle_y + influence + 0.5) {
            state->ball_x = (double)state->left_paddle_x + 0.5;
            double offset = (state->ball_y - state->left_paddle_y) / influence;
            state->ball_vx = fabs(state->ball_vx);
            state->ball_vy = (double)state->columns * 0.25 * offset;
        }
    } else if(state->ball_vx > 0.0 && state->ball_x >= (double)state->right_paddle_x - 0.5) {
        if(state->ball_y >= state->right_paddle_y - influence - 0.5 &&
           state->ball_y <= state->right_paddle_y + influence + 0.5) {
            state->ball_x = (double)state->right_paddle_x - 0.5;
            double offset = (state->ball_y - state->right_paddle_y) / influence;
            state->ball_vx = -fabs(state->ball_vx);
            state->ball_vy = (double)state->columns * 0.25 * offset;
        }
    }

    if(state->ball_x < (double)state->field_left - 0.5) {
        award_point(state, 0);
        return;
    }
    if(state->ball_x > (double)state->field_right + 0.5) {
        award_point(state, 1);
        return;
    }
}

static void clear_screen(void) {
    printf("\033[2J\033[H");
}

static void print_row(const char *text, int columns) {
    int width = columns > 0 ? columns : 0;
    int len = (int)strlen(text);
    if(len > width)
        len = width;
    fwrite(text, 1, (size_t)len, stdout);
    for(int i = len; i < width; i++)
        putchar(' ');
    putchar('\n');
}

static void draw_playfield(const struct GameState *state) {
    int interior_width = state->columns - 2;
    int center_column = state->columns / 2;
    int ball_draw_x = (int)lround(state->ball_x);
    int ball_draw_y = (int)lround(state->ball_y);
    int left_center = (int)lround(state->left_paddle_y);
    int right_center = (int)lround(state->right_paddle_y);
    int half = state->paddle_height / 2;

    for(int y = state->field_top; y <= state->field_bottom; y++) {
        printf("│");
        for(int x = 1; x <= interior_width; x++) {
            int draw_x = x;
            int printed = 0;
            if(draw_x == state->left_paddle_x && y >= left_center - half && y <= left_center + half) {
                printf("█");
                printed = 1;
            } else if(draw_x == state->right_paddle_x && y >= right_center - half && y <= right_center + half) {
                printf("█");
                printed = 1;
            } else if(draw_x == ball_draw_x && y == ball_draw_y) {
                printf("●");
                printed = 1;
            } else if(draw_x == center_column) {
                printf("┊");
                printed = 1;
            }
            if(!printed)
                printf(" ");
        }
        printf("│\n");
    }
}

static void draw_frame(const struct GameState *state) {
    clear_screen();
    char header[256];
    const char *mode_text = state->mode == GAME_MODE_PVP ? "Player vs Player" : "Player vs Computer";
    snprintf(header,
             sizeof(header),
             "BUDO PONG | Mode: %s | Score %d - %d%s",
             mode_text,
             state->score_left,
             state->score_right,
             state->paused ? " | PAUSED" : "");
    print_row(header, state->columns);

    printf("┌");
    for(int x = 0; x < state->columns - 2; x++)
        printf("─");
    printf("┐\n");

    draw_playfield(state);

    printf("└");
    for(int x = 0; x < state->columns - 2; x++)
        printf("─");
    printf("┘\n");

    char banner[256];
    if(state->mode == GAME_MODE_PVP) {
        snprintf(banner,
                 sizeof(banner),
                 "Keys: Player1 W/S | Player2 ↑/↓ arrows or I/K | P Pause | R Reset | Q Quit");
    } else {
        snprintf(banner,
                 sizeof(banner),
                 "Keys: Player1 W/S | Computer tracks the ball | P Pause | R Reset | Q Quit");
    }
    print_row(banner, state->columns);
    fflush(stdout);
}

static void process_input(struct GameState *state, int *left_moves, int *right_moves, int *reset_round) {
    char buffer[64];
    ssize_t bytes;
    while((bytes = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) {
        for(ssize_t i = 0; i < bytes; i++) {
            unsigned char c = (unsigned char)buffer[i];
            if(escape_state == 1) {
                if(c == '[') {
                    escape_state = 2;
                } else {
                    escape_state = 0;
                }
                continue;
            }
            if(escape_state == 2) {
                if(c == 'A') {
                    (*right_moves)--;
                } else if(c == 'B') {
                    (*right_moves)++;
                }
                escape_state = 0;
                continue;
            }
            if(c == '\033') {
                escape_state = 1;
                continue;
            }
            switch(c) {
                case 'w':
                case 'W':
                    (*left_moves)--;
                    break;
                case 's':
                case 'S':
                    (*left_moves)++;
                    break;
                case 'i':
                case 'I':
                    (*right_moves)--;
                    break;
                case 'k':
                case 'K':
                    (*right_moves)++;
                    break;
                case 'p':
                case 'P':
                    state->paused = !state->paused;
                    break;
                case 'r':
                case 'R':
                    *reset_round = 1;
                    break;
                case 'q':
                case 'Q':
                    state->running = 0;
                    break;
                default:
                    break;
            }
        }
    }
    if(bytes == 0)
        return;
    if(bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        perror("read");
}

static enum GameMode prompt_mode(void) {
    printf("BUDO PONG\n");
    printf("1) Player vs Player\n");
    printf("2) Player vs Computer\n");
    printf("Select mode [1-2]: ");
    fflush(stdout);
    char buffer[32];
    if(fgets(buffer, sizeof(buffer), stdin)) {
        if(buffer[0] == '2')
            return GAME_MODE_PVC;
    }
    return GAME_MODE_PVP;
}

int main(void) {
    enum GameMode mode = prompt_mode();
    struct GameState state;
    memset(&state, 0, sizeof(state));
    state.mode = mode;
    state.running = 1;
    state.paused = 0;
    srand((unsigned int)time(NULL));

    int columns = 0;
    int rows = 0;
    query_terminal_size(&columns, &rows);
    apply_dimensions(&state, columns, rows, 1);
    reset_scores(&state);

    enable_raw_mode();

    double last_time = now_seconds();
    while(state.running) {
        int new_columns = 0;
        int new_rows = 0;
        query_terminal_size(&new_columns, &new_rows);
        if(new_columns != state.columns || new_rows != state.rows) {
            apply_dimensions(&state, new_columns, new_rows, 0);
        }

        int left_moves = 0;
        int right_moves = 0;
        int reset_round = 0;
        process_input(&state, &left_moves, &right_moves, &reset_round);
        if(reset_round)
            reset_scores(&state);
        if(left_moves != 0 || right_moves != 0)
            apply_player_movement(&state, left_moves, right_moves);

        double now = now_seconds();
        double dt = now - last_time;
        last_time = now;
        if(dt > 0.1)
            dt = 0.1;
        if(dt < 0.0)
            dt = 0.0;

        if(!state.paused && state.running) {
            update_ai(&state, dt);
            update_ball(&state, dt);
        }

        draw_frame(&state);
        sleep_frame();
    }

    return 0;
}
