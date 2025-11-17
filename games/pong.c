#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 600
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define FIELD_WIDTH 60
#define FIELD_HEIGHT 20
#define PADDLE_HEIGHT 5
#define MAX_SCORE 3
#define FRAME_DURATION_NS 41666666LL

#define LEFT_PADDLE_X 2
#define RIGHT_PADDLE_X (FIELD_WIDTH - 3)

typedef struct {
    double y;
} Paddle;

typedef struct {
    double x;
    double y;
    double vx;
    double vy;
} Ball;

typedef enum {
    MODE_MENU,
    MODE_PLAYING
} RunMode;

typedef enum {
    MATCH_ACTIVE,
    MATCH_WON
} MatchState;

static struct termios orig_termios;
static Paddle left_paddle;
static Paddle right_paddle;
static Ball ball;
static int left_score = 0;
static int right_score = 0;
static int ai_enabled = 0;
static RunMode run_mode = MODE_MENU;
static MatchState match_state = MATCH_ACTIVE;

static void disableRawMode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    write(STDOUT_FILENO, "\033[?25h", 6);
}

static void enableRawMode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    atexit(disableRawMode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
    write(STDOUT_FILENO, "\033[?25l", 6);
}

static void clearScreen(void) {
    write(STDOUT_FILENO, "\033[2J\033[H", 7);
}

static struct timespec frameStart(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return ts;
}

static void capFrameRate(const struct timespec *start) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    long long elapsed_ns = (long long)(now.tv_sec - start->tv_sec) * 1000000000LL +
                           (long long)(now.tv_nsec - start->tv_nsec);
    long long remaining = FRAME_DURATION_NS - elapsed_ns;
    if (remaining <= 0) {
        return;
    }
    struct timespec sleep_time;
    sleep_time.tv_sec = (time_t)(remaining / 1000000000LL);
    sleep_time.tv_nsec = (long)(remaining % 1000000000LL);
    nanosleep(&sleep_time, NULL);
}

static int readInput(void) {
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1) {
        return (int)c;
    }
    return -1;
}

static void centerPaddles(void) {
    double center = (FIELD_HEIGHT - 2) / 2.0;
    left_paddle.y = center;
    right_paddle.y = center;
}

static double clamp(double value, double min, double max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static void resetBall(int towards_right) {
    ball.x = FIELD_WIDTH / 2.0;
    ball.y = FIELD_HEIGHT / 2.0;
    double speed = 0.8;
    ball.vx = towards_right ? speed : -speed;
    double angle = ((double)(rand() % 100) / 100.0) - 0.5;
    ball.vy = angle;
}

static void resetMatch(void) {
    left_score = 0;
    right_score = 0;
    match_state = MATCH_ACTIVE;
    centerPaddles();
    resetBall(rand() % 2);
}

static void drawMenu(int selection) {
    clearScreen();
    printf("====================\n");
    printf("        PONG        \n");
    printf("====================\n\n");
    printf("Use W/S or arrow keys to choose a mode.\n");
    printf("Press Enter to start, Q to quit.\n\n");
    printf("%s Player vs Player\n", selection == 0 ? ">" : " ");
    printf("%s Player vs Computer\n\n", selection == 1 ? ">" : " ");
    printf("All paddles and ball render as white blocks during play.\n");
    fflush(stdout);
}

static void drawGame(const char *status_line) {
    clearScreen();
    const char *block = "\u2588"; // Unicode full block
    int ball_row = (int)llround(ball.y);
    int ball_col = (int)llround(ball.x);
    int left_top = (int)floor(left_paddle.y - (double)PADDLE_HEIGHT / 2.0);
    int right_top = (int)floor(right_paddle.y - (double)PADDLE_HEIGHT / 2.0);

    for (int y = 0; y < FIELD_HEIGHT; y++) {
        for (int x = 0; x < FIELD_WIDTH; x++) {
            int boundary = (y == 0 || y == FIELD_HEIGHT - 1 || x == 0 || x == FIELD_WIDTH - 1);
            int draw_left = 0;
            int draw_right = 0;
            if (x == LEFT_PADDLE_X && y >= left_top && y < left_top + PADDLE_HEIGHT) {
                draw_left = 1;
            }
            if (x == RIGHT_PADDLE_X && y >= right_top && y < right_top + PADDLE_HEIGHT) {
                draw_right = 1;
            }
            int draw_ball = (x == ball_col && y == ball_row);
            if (boundary || draw_left || draw_right || draw_ball) {
                printf("%s", block);
            } else {
                printf(" ");
            }
        }
        printf("\n");
    }
    printf("Score P1: %d  P2: %d  Mode: %s  %s\n", left_score, right_score,
           ai_enabled ? "Player vs Computer" : "Player vs Player", status_line);
    printf("Controls: W/S left | O/L right%s | R restart | Q quit\n",
           ai_enabled ? " (CPU controls right paddle)" : "");
    fflush(stdout);
}

static void updatePaddle(Paddle *p, double delta) {
    double min_y = 1 + (double)PADDLE_HEIGHT / 2.0;
    double max_y = (FIELD_HEIGHT - 2) - (double)PADDLE_HEIGHT / 2.0;
    p->y = clamp(p->y + delta, min_y, max_y);
}

static void updateBall(void) {
    if (match_state == MATCH_WON) {
        return;
    }
    ball.x += ball.vx;
    ball.y += ball.vy;

    if (ball.y <= 1) {
        ball.y = 1;
        ball.vy = -ball.vy;
    } else if (ball.y >= FIELD_HEIGHT - 2) {
        ball.y = FIELD_HEIGHT - 2;
        ball.vy = -ball.vy;
    }

    double left_top = left_paddle.y - (double)PADDLE_HEIGHT / 2.0;
    double left_bottom = left_paddle.y + (double)PADDLE_HEIGHT / 2.0;
    if (ball.x <= LEFT_PADDLE_X + 0.5 && ball.x >= LEFT_PADDLE_X - 0.5 && ball.y >= left_top && ball.y <= left_bottom) {
        ball.x = LEFT_PADDLE_X + 0.6;
        ball.vx = fabs(ball.vx) * 1.03;
        double relative = (ball.y - left_paddle.y) / ((double)PADDLE_HEIGHT / 2.0);
        ball.vy = relative * 0.9;
    }

    double right_top = right_paddle.y - (double)PADDLE_HEIGHT / 2.0;
    double right_bottom = right_paddle.y + (double)PADDLE_HEIGHT / 2.0;
    if (ball.x >= RIGHT_PADDLE_X - 0.5 && ball.x <= RIGHT_PADDLE_X + 0.5 && ball.y >= right_top && ball.y <= right_bottom) {
        ball.x = RIGHT_PADDLE_X - 0.6;
        ball.vx = -fabs(ball.vx) * 1.03;
        double relative = (ball.y - right_paddle.y) / ((double)PADDLE_HEIGHT / 2.0);
        ball.vy = relative * 0.9;
    }

    if (ball.x < 1) {
        right_score++;
        if (right_score >= MAX_SCORE) {
            match_state = MATCH_WON;
        }
        centerPaddles();
        resetBall(0);
    } else if (ball.x > FIELD_WIDTH - 2) {
        left_score++;
        if (left_score >= MAX_SCORE) {
            match_state = MATCH_WON;
        }
        centerPaddles();
        resetBall(1);
    }
}

static void handleAIPaddle(void) {
    if (!ai_enabled || match_state == MATCH_WON) {
        return;
    }
    double target = ball.y;
    double delta = 0.0;
    if (target < right_paddle.y - 0.3) {
        delta = -0.5;
    } else if (target > right_paddle.y + 0.3) {
        delta = 0.5;
    }
    updatePaddle(&right_paddle, delta);
}

static void processInput(void) {
    int c;
    while ((c = readInput()) != -1) {
        if (c == 'q' || c == 'Q') {
            clearScreen();
            exit(0);
        } else if (c == 'r' || c == 'R') {
            resetMatch();
        } else if (run_mode == MODE_MENU) {
            // handled elsewhere
            break;
        } else {
            if (c == 'w' || c == 'W') {
                updatePaddle(&left_paddle, -1.0);
            } else if (c == 's' || c == 'S') {
                updatePaddle(&left_paddle, 1.0);
            } else if (!ai_enabled && (c == 'o' || c == 'O')) {
                updatePaddle(&right_paddle, -1.0);
            } else if (!ai_enabled && (c == 'l' || c == 'L')) {
                updatePaddle(&right_paddle, 1.0);
            }
        }
    }
}

static int runMenu(void) {
    int selection = 0;
    while (1) {
        struct timespec frame_start = frameStart();
        drawMenu(selection);
        int c = readInput();
        if (c == -1) {
            capFrameRate(&frame_start);
            continue;
        }
        if (c == 'q' || c == 'Q') {
            return 0;
        } else if (c == '\n' || c == '\r') {
            ai_enabled = (selection == 1);
            resetMatch();
            run_mode = MODE_PLAYING;
            capFrameRate(&frame_start);
            return 1;
        } else if (c == 'w' || c == 'W') {
            selection = 0;
        } else if (c == 's' || c == 'S') {
            selection = 1;
        } else if (c == '\033') {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) {
                continue;
            }
            if (read(STDIN_FILENO, &seq[1], 1) != 1) {
                continue;
            }
            if (seq[0] == '[') {
                if (seq[1] == 'A') {
                    selection = 0;
                } else if (seq[1] == 'B') {
                    selection = 1;
                }
            }
        }
        capFrameRate(&frame_start);
    }
}

int main(void) {
    enableRawMode();
    srand((unsigned int)time(NULL));

    while (1) {
        struct timespec frame_start = frameStart();
        if (run_mode == MODE_MENU) {
            if (!runMenu()) {
                break;
            }
        }

        char status[128];
        if (match_state == MATCH_WON) {
            const char *winner = left_score >= MAX_SCORE ? "Player 1" : (ai_enabled ? "Computer" : "Player 2");
            snprintf(status, sizeof(status), "%s wins the match! Press R to restart or Q to quit.", winner);
        } else {
            snprintf(status, sizeof(status), "First to %d points wins.", MAX_SCORE);
        }

        processInput();
        if (ai_enabled) {
            handleAIPaddle();
        }
        updateBall();
        drawGame(status);
        capFrameRate(&frame_start);
    }

    disableRawMode();
    clearScreen();
    return 0;
}
