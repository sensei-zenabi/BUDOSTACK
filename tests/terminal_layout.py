#!/usr/bin/env python3
"""Compile real game renderers and check LOW/HIGH bounds and PTY ownership."""
import fcntl
import os
from pathlib import Path
import pty
import re
import struct
import subprocess
import tempfile
import termios

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic']


def check_frame(data, rows, cols):
    row = col = 1
    painted = []
    for match in re.finditer(r'\x1b\[[0-9;?]*[A-Za-z]|[^\x1b]', data):
        token = match.group()
        if token.startswith('\x1b['):
            if token[-1] in 'Hf':
                values = token[2:-1].split(';')
                row = int(values[0] or 1)
                col = int(values[1] or 1) if len(values) > 1 else 1
            elif token[-1] == 't':
                raise AssertionError('game changed terminal size')
        elif token == '\n':
            row += 1
            col = 1
        elif token == '\r':
            col = 1
        elif token >= ' ':
            assert 1 <= row <= rows and 1 <= col <= cols, (row, col, rows, cols)
            if token != ' ':
                painted.append((row, col))
            col += 1
    assert painted
    return painted


with tempfile.TemporaryDirectory(dir=ROOT) as directory:
    work = Path(directory)
    def compile_source(name, source):
        path = work / (name + '.c')
        path.write_text(source)
        exe = work / name
        subprocess.run(['cc', *FLAGS, '-I', str(ROOT), str(path),
                        str(ROOT / 'lib/terminal_layout.c'), str(ROOT / 'lib/terminal_input.c'), '-o', str(exe)], check=True)
        return exe

    bodies = {
        'snake': 'initGame(); drawBoard(); game_over = 1; drawBoard();',
        'invaders': 'init_game(); draw_game(); game_over = 1; draw_game();',
        'tictactoe': 'char b[BOARD_SIZE][BOARD_SIZE]; init_board(b); '
                    'render_game(b, \'X\', 8, 8, 1, -1, -1, "Player vs Computer");',
        'stats': 'struct system_snapshot s = {0}; draw_snapshot(&s, 80);',
        'chess': 'GameState s; init_game(&s); Move m = {0}; '
                 'render_board(&s, "White to move", MODE_PVC, DIFF_MEDIUM, '
                 'm, 4, 4, -1, -1, 1);',
    }
    for name, body in bodies.items():
        exe = compile_source(name, '#define main game_main\n'
                             f'#include "{"apps" if name == "stats" else "games"}/{name}.c"\n#undef main\n'
                             f'int main(void) {{ {body} return 0; }}\n')
        modes = [('HIGH', 60, 80)] if name == 'stats' else [('LOW', 30, 40), ('HIGH', 60, 80)]
        for mode, rows, cols in modes:
            env = dict(os.environ, LINES=str(rows), COLUMNS=str(cols),
                       BUDOSTACK_RES_MODE=mode)
            data = subprocess.check_output([exe], env=env, text=True)
            painted = check_frame(data, rows, cols)
            if mode == 'HIGH':
                assert max(r for r, _ in painted) - min(r for r, _ in painted) >= 35
            print(f'{name}: {mode} fits {cols}x{rows}')

    exe = compile_source('layout', '#include "lib/terminal_layout.h"\n'
                         '#include <stdio.h>\nint main(void) { '
                         'printf("%d %d", budostack_get_target_cols(), '
                         'budostack_get_target_rows()); return 0; }\n')
    for mode, expected in [('LOW', '40 30'), ('320x240', '40 30'),
                           ('HIGH', '80 60'), ('640x480', '80 60')]:
        env = dict(os.environ, BUDOSTACK_RES_MODE=mode)
        env.pop('LINES', None)
        env.pop('COLUMNS', None)
        assert subprocess.check_output([exe], env=env, text=True) == expected

    master, slave = pty.openpty()
    try:
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 37, 53, 0, 0))
        env = dict(os.environ, BUDOSTACK_TERM_ACTIVE='TRUE', BUDOSTACK_RES_MODE='HIGH')
        subprocess.run([exe], stdout=slave, env=env, check=True)
        assert os.read(master, 4096) == b'53 37'
        assert struct.unpack('HHHH', fcntl.ioctl(slave, termios.TIOCGWINSZ,
                                               bytes(8)))[:2] == (37, 53)
    finally:
        os.close(master)
        os.close(slave)
    print('Presets and active PTY dimensions passed.')

    for mode, expected in [('LOW', b'\x1b]777;resolution=320x240\x07'),
                           ('HIGH', b'\x1b]777;resolution=640x480\x07')]:
        assert subprocess.check_output([ROOT / 'commands/_TERM_RESOLUTION', mode]) == expected
    print('Resolution command protocol passed.')
