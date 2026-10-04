#!/usr/bin/env python3
"""Exercise startup decisions through the real TASK interpreter, without audio/UI."""
import json
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
config = {}
for line in (root / 'config.ini').read_text().splitlines():
    if '=' in line and not line.lstrip().startswith('#'):
        key, value = line.split('=', 1)
        config[key.strip()] = value.split('#')[0].strip()
source = (root / 'tasks/autoexec.task').read_text()
for active, answer in [(1, 'y'), (1, 'Y'), (1, 'n'), (0, 'y')]:
    lines = []
    for line in source.splitlines():
        if line.lstrip().startswith('#'):
            continue
        match = re.match(r'(\s*)RUN (.*)', line)
        if match:
            indent, command = match.groups()
            if command.startswith('_CONFIG -read '):
                _, _, key, _, variable = command.split()
                line = indent + 'SET ' + variable + ' = ' + json.dumps(config[key])
            elif command.startswith('_TERM_ACTIVE TO '):
                line = indent + 'SET ' + command.split()[-1] + ' = ' + str(active)
            elif command.startswith('_WAIT '):
                continue
            else:
                line = indent + 'PRINT ' + json.dumps('\n@RUN ' + command + '\n')
        lines.append(line)
    with tempfile.NamedTemporaryFile(mode='w', suffix='.task', dir=root) as task:
        task.write('\n'.join(lines) + '\n')
        task.flush()
        result = subprocess.run([root / 'apps/runtask', task.name, '-d'],
                                input=answer + '\n', text=True, capture_output=True,
                                check=True, timeout=10)
    assert not re.search('Unrecognized|unexpected|failed|invalid', result.stderr, re.I)
    launch = '@RUN ../budo/BUDOWIN'
    assert (launch in result.stdout) == (active and answer.lower() == 'y')
    if launch in result.stdout:
        preceding = result.stdout.split(launch)[0]
        assert preceding.rstrip().endswith('@RUN _TERM_TYPING_SOUND disable')
        assert '@RUN _TERM_RESOLUTION HIGH' in preceding
    assert '@RUN _TERM_CURSOR_BLINK enable' in result.stdout
print('BUDOWIN startup: y/Y launch, n bypass, inactive guard and cleanup passed.')
