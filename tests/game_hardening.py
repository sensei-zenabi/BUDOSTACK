#!/usr/bin/env python3
"""Gameplay, escape decoding, resize, uniform scaling and terminal cleanup tests."""
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import subprocess
import tempfile
import termios
import time

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic']

SNAKE = r'''
    for (int n = 0; n < 500; n++) {
        initGame();
        for (int k = 0; k < snake_length; k++) {
            assert(fruit.x != snake[k].x || fruit.y != snake[k].y);
        }
    }
    snake_length = 4;
    snake[0] = (Point){1, 1}; snake[1] = (Point){1, 2};
    snake[2] = (Point){2, 2}; snake[3] = (Point){2, 1};
    fruit = (Point){10, 10}; dir = RIGHT; game_over = 0;
    updateSnake(); assert(!game_over && snake[0].x == 2);
    dir = LEFT; updateSnake(); assert(game_over);
    initGame(); snake_length = MAX_SNAKE_LENGTH - 1;
    snake[0] = (Point){10, 10};
    for (int k = 1; k < snake_length; k++) {
        snake[k] = (Point){k % WIDTH, k / WIDTH};
    }
    fruit = (Point){11, 10}; dir = RIGHT;
    updateSnake(); assert(snake_length == MAX_SNAKE_LENGTH && game_over);
    for (int k = 0; k < snake_length; k++) {
        assert(snake[k].x >= 0 && snake[k].x < WIDTH);
        assert(snake[k].y >= 0 && snake[k].y < HEIGHT);
    }
'''

INVADERS = r'''
    init_game(); memset(invaders, 0, sizeof(invaders));
    invaders[0][0] = 1; invader_offset_x = 5; invader_offset_y = 10;
    bullet = (Bullet){1, 5, 11}; frame_count = 0;
    update_game(); assert(game_win && !game_over && score == 10 && !bullet.active);
    init_game(); bullet = (Bullet){1, BOARD_WIDTH - 1, 18}; frame_count = 1;
    update_game(); assert(bullet.y == 17);
    init_game(); invader_offset_x = BOARD_WIDTH - 1 - (INV_COLS - 1) * INV_SPACING_X;
    int old_y = invader_offset_y; frame_count = 0;
    update_invaders(); assert(invader_dir == -1 && invader_offset_y == old_y + 1);
    for (int n = 0; n < 10000 && !game_over; n++) {
        frame_count++; update_game();
        for (int i = 0; i < INV_ROWS; i++) {
            for (int j = 0; j < INV_COLS; j++) {
                int x = invader_offset_x + j * INV_SPACING_X;
                assert(!invaders[i][j] || (x >= 0 && x < BOARD_WIDTH));
            }
        }
    }
    assert(game_over);
    init_game(); setenv("COLUMNS", "20", 1); setenv("LINES", "10", 1);
    int x = invader_offset_x; old_y = invader_offset_y;
    update_game(); assert(invader_offset_x == x && invader_offset_y == old_y);
'''

CHESS_HELPER = r'''
static unsigned long perft(const GameState *state, int depth) {
    if (!depth) { return 1; }
    MoveList moves; generate_legal_moves(state, &moves);
    unsigned long count = 0;
    for (int i = 0; i < moves.count; i++) {
        GameState next = *state; make_move(&next, moves.items[i]);
        count += perft(&next, depth - 1);
    }
    return count;
}
'''
CHESS = r'''
    GameState state; init_game(&state);
    assert(perft(&state, 1) == 20);
    assert(perft(&state, 2) == 400);
    assert(perft(&state, 3) == 8902);
'''
CONNECT = r'''
    char b[BOARD_SIZE][BOARD_SIZE]; init_board(b);
    for (int i = 0; i < 5; i++) { b[4 + i][12 - i] = 'X'; }
    assert(check_winner(b) == 'X');
    init_board(b); for (int i = 0; i < 4; i++) { b[16][i] = 'O'; }
    int row, col; cpu_turn(b, 'O', 'X', &row, &col);
    assert(row == 16 && col == 4 && check_winner(b) == 'O');
    init_board(b); for (int i = 0; i < 4; i++) { b[i][16] = 'X'; }
    cpu_turn(b, 'O', 'X', &row, &col);
    assert(row == 4 && col == 16 && b[row][col] == 'O');
'''

KEYS = r'''
    int fds[2]; assert(pipe(fds) == 0); assert(dup2(fds[0], 0) == 0); close(fds[0]);
    assert(write(fds[1], "\033", 1) == 1);
    assert(budostack_terminal_read_key(0) == BUDOSTACK_KEY_NONE);
    struct timespec delay = {0, 120000000}; nanosleep(&delay, NULL);
    assert(write(fds[1], "[", 1) == 1);
    assert(budostack_terminal_read_key(0) == BUDOSTACK_KEY_NONE);
    nanosleep(&delay, NULL); assert(write(fds[1], "D", 1) == 1);
    assert(budostack_terminal_read_key(0) == BUDOSTACK_KEY_LEFT);
    const char sequences[] = "\033OA\033[1;5C\033[3~q";
    assert(write(fds[1], sequences, sizeof(sequences) - 1) == sizeof(sequences) - 1);
    assert(budostack_terminal_read_key(0) == BUDOSTACK_KEY_UP);
    assert(budostack_terminal_read_key(0) == BUDOSTACK_KEY_RIGHT);
    assert(budostack_terminal_read_key(0) == BUDOSTACK_KEY_NONE);
    assert(budostack_terminal_read_key(0) == 'q');
    assert(budostack_terminal_read_key(0) == BUDOSTACK_KEY_NONE);
    close(fds[1]); assert(budostack_terminal_read_key(0) == BUDOSTACK_KEY_EOF);
'''


def screen(data, rows, cols):
    grid = [[' '] * cols for _ in range(rows)]
    row = col = 0
    for match in re.finditer(r'\x1b\[[0-9;?]*[A-Za-z]|[^\x1b]', data):
        token = match.group()
        if token.startswith('\x1b['):
            args = token[2:-1].split(';')
            if token[-1] in 'Hf':
                row = int(args[0] or 1) - 1
                col = int(args[1] or 1) - 1 if len(args) > 1 else 0
                assert 0 <= row < rows and 0 <= col < cols, (token, rows, cols)
            elif token == '\x1b[2J':
                grid = [[' '] * cols for _ in range(rows)]
            elif token == '\x1b[2K':
                grid[row] = [' '] * cols
        elif token == '\n':
            row += 1
            col = 0
            assert row < rows, 'newline scrolled the frame'
        elif token == '\r':
            col = 0
        elif token >= ' ':
            assert 0 <= row < rows and 0 <= col < cols, (row, col, rows, cols)
            grid[row][col] = token
            col += 1
    return [''.join(line) for line in grid]


with tempfile.TemporaryDirectory(dir=ROOT) as directory:
    work = Path(directory)
    def compile_source(name, source, sanitize=False):
        path = work / (name + '.c')
        source += '\nint budostack_terminal_layout_enabled(void) { return 0; }\n'
        if sanitize:
            source += '\nconst char *__asan_default_options(void) { return "detect_leaks=0"; }\n'
        path.write_text(source)
        exe = work / name
        extra = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if sanitize else []
        subprocess.run(['cc', *FLAGS, *extra, '-I', str(ROOT), str(path),
                        str(ROOT / 'lib/terminal_layout.c'), str(ROOT / 'lib/terminal_input.c'),
                        '-o', str(exe)], check=True)
        return exe

    # LeakSanitizer cannot enumerate threads in restricted execution environments.
    env = dict(os.environ, COLUMNS='80', LINES='60', ASAN_OPTIONS='detect_leaks=0')
    for name, body in [('snake', SNAKE), ('invaders', INVADERS),
                       ('chess', CHESS), ('tictactoe', CONNECT)]:
        source = f'#define main game_main\n#include "games/{name}.c"\n#undef main\n'
        source += '#include <assert.h>\n#include <string.h>\n'
        if name == 'chess':
            source += CHESS_HELPER
        exe = compile_source(name, source + 'int main(void) {' + body + 'return 0;}\n', True)
        subprocess.run([exe], env=env, check=True, timeout=20)
        print(f'{name}: sanitized gameplay checks passed')

    exe = compile_source('keys', '#define _POSIX_C_SOURCE 200809L\n'
                         '#include "lib/terminal_input.h"\n#include <assert.h>\n'
                         '#include <unistd.h>\n#include <time.h>\n'
                         'int main(void) {' + KEYS + 'return 0;}\n', True)
    subprocess.run([exe], env=env, check=True, timeout=3)
    print('Fragmented CSI/SS3, modifiers, unknown keys, idle and EOF passed')

    # Sweep dimensions including small windows and odd custom resolutions.
    for name, body in [
        ('invaders', 'init_game(); draw_game(); player_x++; draw_game();'),
        ('snake', 'initGame(); drawBoard(); drawBoard();'),
        ('chess', 'GameState s; init_game(&s); Move m = {-1,-1,-1,-1,0,0,0,-1,-1}; '
         'render_board(&s,"White to move",MODE_PVC,DIFF_EASY,m,4,4,-1,-1,1);'),
        ('tictactoe', 'char b[BOARD_SIZE][BOARD_SIZE]; init_board(b); '
         'render_game(b,\'X\',8,8,1,-1,-1,"Player vs Computer");'),
    ]:
        exe = compile_source(name + '_render', f'#define main game_main\n#include "games/{name}.c"\n'
                             '#undef main\nint main(void) {' + body + 'return 0;}\n')
        for rows, cols in [(1, 1), (10, 20), (24, 21), (27, 32), (30, 40),
                           (37, 53), (45, 80), (60, 80), (60, 81), (80, 120)]:
            data = subprocess.check_output([exe], env=dict(env, LINES=str(rows), COLUMNS=str(cols)), text=True)
            grid = screen(data, rows, cols)
            assert data.count('\x1b[2J') == 1, 'full-screen clear on an unchanged viewport'
            if name == 'invaders' and cols >= 40 and rows >= 26:
                scale = min((cols - 2) // 38, (rows - 6) // 20)
                assert sum(line.count('W') for line in grid) == 48 * scale * scale
                assert sum(line.count('┌') for line in grid) == 1
                assert sum(line.count('┐') for line in grid) == 1
                assert sum(line.count('└') for line in grid) == 1
                assert sum(line.count('┘') for line in grid) == 1
                ship_rows = [line for line in grid if 'A' in line and 'INVADERS' not in line and 'Arrows' not in line]
                assert len(ship_rows) == scale and all(line.count('A') == scale for line in ship_rows)
            if name == 'snake' and cols >= 22 and rows >= 26:
                assert any('█' in line for line in grid)
                assert any('▓' in line for line in grid)
            if name == 'tictactoe' and cols >= 21 and rows >= 24:
                assert all(sum(line.count(corner) for line in grid) == 1 for corner in '┌┐└┘')
        print(f'{name}: viewport sweep and repaint checks passed')

    # Exercise real terminals: raw settings/cursor recover after Ctrl+C and quit.
    for name, setup in [('invaders', b''), ('snake', b''), ('chess', b'2\n'), ('tictactoe', b'2\n')]:
        for stop in ['quit', 'interrupt', 'terminate', 'hangup']:
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 60, 80, 0, 0))
            original = termios.tcgetattr(slave)
            proc = subprocess.Popen([ROOT / 'games' / name], stdin=slave, stdout=slave, stderr=slave,
                                    env=dict(env, BUDOSTACK_TERM_ACTIVE='TRUE'))
            output = bytearray()
            try:
                if setup:
                    os.write(master, setup)
                deadline = time.monotonic() + 5
                while b'\x1b[?25l' not in output and time.monotonic() < deadline:
                    if select.select([master], [], [], .05)[0]:
                        output.extend(os.read(master, 65536))
                assert b'\x1b[?25l' in output, (name, output[-200:])
                assert not (termios.tcgetattr(slave)[3] & termios.ICANON)
                # Fragment a real arrow sequence, then resize away and back.
                for part in [b'\x1b', b'[', b'D']:
                    os.write(master, part)
                    time.sleep(.02)
                for rows, cols in [(10, 20), (30, 40), (60, 80)]:
                    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
                    time.sleep(.12)
                    while select.select([master], [], [], 0)[0]:
                        output.extend(os.read(master, 65536))
                if stop == 'quit':
                    os.write(master, b'q')
                else:
                    proc.send_signal({'interrupt': signal.SIGINT,
                                      'terminate': signal.SIGTERM,
                                      'hangup': signal.SIGHUP}[stop])
                deadline = time.monotonic() + 3
                while proc.poll() is None and time.monotonic() < deadline:
                    if select.select([master], [], [], .05)[0]:
                        output.extend(os.read(master, 65536))
                proc.wait(timeout=1)
                while select.select([master], [], [], 0)[0]:
                    output.extend(os.read(master, 65536))
                assert proc.returncode == 0, (name, proc.returncode)
                assert termios.tcgetattr(slave) == original, name
                assert b'\x1b[?25h' in output, name
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                os.close(master)
                os.close(slave)
        print(f'{name}: live PTY arrows, resize, quit, interrupt and restoration passed')
