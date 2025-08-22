/*
 * drawdemo.c — simple Arkanoid-like game using libdraw framebuffer.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/libdraw.c"

static volatile sig_atomic_t running = 1;
static void on_sigint(int sig){ (void)sig; running = 0; }

/* --- timing helpers --- */
static inline uint64_t now_ns(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000000ull + (uint64_t)ts.tv_nsec;
}
static inline void sleep_until_ns(uint64_t target){
    uint64_t t = now_ns();
    if (target <= t) return;
    struct timespec ts;
    uint64_t d = target - t;
    ts.tv_sec = (time_t)(d/1000000000ull);
    ts.tv_nsec = (long)(d%1000000000ull);
    clock_nanosleep(CLOCK_MONOTONIC,0,&ts,NULL);
}

/* --- minimal raw keyboard handling --- */
static struct termios orig_termios;
static int orig_fl;

static void disable_raw(void){
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    fcntl(STDIN_FILENO, F_SETFL, orig_fl);
}

static void enable_raw(void){
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    orig_fl = fcntl(STDIN_FILENO, F_GETFL);
    fcntl(STDIN_FILENO, F_SETFL, orig_fl | O_NONBLOCK);
    atexit(disable_raw);
}

static int read_key(void){
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return 0;
    if (c == '\033'){
        unsigned char seq[2];
        if (read(STDIN_FILENO,&seq[0],1) != 1) return 0;
        if (read(STDIN_FILENO,&seq[1],1) != 1) return 0;
        if (seq[0]=='['){
            if (seq[1]=='C') return 'R'; // right
            if (seq[1]=='D') return 'L'; // left
        }
        return 0;
    }
    return c;
}

/* --- brick definition --- */
#define BRICK_ROWS 5
#define BRICK_COLS 10

typedef struct {
    int x,y,w,h;
    int alive;
    uint8_t r,g,b;
} Brick;

static Brick bricks[BRICK_ROWS*BRICK_COLS];

static void init_bricks(int W, int H){
    int bw = W / BRICK_COLS;
    int bh = H / 20;
    uint8_t colors[BRICK_ROWS][3] = {
        {255,0,0}, {255,128,0}, {255,255,0}, {0,128,0}, {0,0,255}
    };
    for(int r=0;r<BRICK_ROWS;r++){
        for(int c=0;c<BRICK_COLS;c++){
            Brick *b = &bricks[r*BRICK_COLS + c];
            b->x = c*bw + 1;
            b->y = r*bh + 40;
            b->w = bw - 2;
            b->h = bh - 2;
            b->alive = 1;
            b->r = colors[r%BRICK_ROWS][0];
            b->g = colors[r%BRICK_ROWS][1];
            b->b = colors[r%BRICK_ROWS][2];
        }
    }
}

int main(void){
    if (draw_open(320, 200) != 0) return 1;
    signal(SIGINT, on_sigint);
    enable_raw();

    const int W = draw_w();
    const int H = draw_h();
    size_t pitch = draw_stride();
    uint8_t *background = malloc(pitch * (size_t)H);
    if (!background) { draw_close(); return 1; }

    init_bricks(W,H);

    // Render static background once
    draw_clear(COLOR(0,0,0));
    for (int i=0;i<BRICK_ROWS*BRICK_COLS;i++){
        Brick *b = &bricks[i];
        draw_rect_fill(b->x, b->y, b->w, b->h, COLOR(b->r, b->g, b->b));
        draw_rect(b->x, b->y, b->w, b->h, COLOR(0,0,0));
    }
    draw_line(0, H-1, W, H-1, COLOR(50,50,50));
    draw_text(5, 5, "Q to quit", COLOR(255,255,255));
    memcpy(background, draw_pixels(), pitch * (size_t)H);

    int paddle_w = W/8;
    int paddle_h = H/40; if (paddle_h < 5) paddle_h = 5;
    int paddle_x = (W - paddle_w)/2;
    int paddle_y = H - paddle_h - 20;
    int paddle_speed = W/30; if (paddle_speed < 8) paddle_speed = 8;

    int ball_r = H/60; if(ball_r < 3) ball_r = 3;
    int ball_x = W/2;
    int ball_y = paddle_y - ball_r - 1;
    int ball_dx = 6;
    int ball_dy = -6;

    uint64_t next = now_ns();
    const uint64_t frame_ns = 33333333ull; // ~30Hz

    while (running){
        int key = read_key();
        if (key == 'q' || key == 'Q') break;
        if (key == 'L') {
            paddle_x -= paddle_speed;
            if (paddle_x < 0) paddle_x = 0;
        } else if (key == 'R') {
            paddle_x += paddle_speed;
            if (paddle_x + paddle_w >= W) paddle_x = W - paddle_w - 1;
        } else if (key == 'a' || key == 'A') {
            paddle_x -= paddle_speed;
            if (paddle_x < 0) paddle_x = 0;
        } else if (key == 'd' || key == 'D') {
            paddle_x += paddle_speed;
            if (paddle_x + paddle_w >= W) paddle_x = W - paddle_w - 1;
        }

        /* update ball position */
        ball_x += ball_dx;
        ball_y += ball_dy;

        /* wall collisions */
        if (ball_x - ball_r <= 0){ ball_x = ball_r; ball_dx = -ball_dx; }
        if (ball_x + ball_r >= W){ ball_x = W - ball_r - 1; ball_dx = -ball_dx; }
        if (ball_y - ball_r <= 0){ ball_y = ball_r; ball_dy = -ball_dy; }
        if (ball_y - ball_r > H){ break; } // missed paddle

        /* paddle collision */
        if (ball_y + ball_r >= paddle_y &&
            ball_y + ball_r <= paddle_y + paddle_h &&
            ball_x >= paddle_x && ball_x <= paddle_x + paddle_w &&
            ball_dy > 0){
            ball_y = paddle_y - ball_r;
            ball_dy = -ball_dy;
            int rel = ball_x - (paddle_x + paddle_w/2);
            ball_dx = rel / (paddle_w/4);
            if (ball_dx==0) ball_dx = (rel>0)?1:-1;
        }

        /* brick collisions */
        int remaining = 0;
        int bricks_dirty = 0;
        for (int i=0;i<BRICK_ROWS*BRICK_COLS;i++){
            Brick *b = &bricks[i];
            if (!b->alive) continue;
            remaining++;
            if (ball_x + ball_r > b->x && ball_x - ball_r < b->x + b->w &&
                ball_y + ball_r > b->y && ball_y - ball_r < b->y + b->h){
                b->alive = 0;
                ball_dy = -ball_dy;
                bricks_dirty = 1;
            }
        }
        if (remaining == 0) break; // win

        /* render */
        if (bricks_dirty) {
            draw_clear(COLOR(0,0,0));
            for (int i=0;i<BRICK_ROWS*BRICK_COLS;i++){
                Brick *b = &bricks[i];
                if (!b->alive) continue;
                draw_rect_fill(b->x, b->y, b->w, b->h, COLOR(b->r, b->g, b->b));
                draw_rect(b->x, b->y, b->w, b->h, COLOR(0,0,0));
            }
            draw_line(0, H-1, W, H-1, COLOR(50,50,50));
            memcpy(background, draw_pixels(), pitch * (size_t)H);
        } else {
            memcpy(draw_pixels(), background, pitch * (size_t)H);
        }

        draw_rect_fill(paddle_x, paddle_y, paddle_w, paddle_h, COLOR(200,200,200));
        draw_rect(paddle_x, paddle_y, paddle_w, paddle_h, COLOR(0,0,0));
        draw_circle_fill(ball_x, ball_y, ball_r, COLOR(255,255,255));
        draw_present();

        next += frame_ns;
        sleep_until_ns(next);
    }

    free(background);
    draw_close();
    return 0;
}
