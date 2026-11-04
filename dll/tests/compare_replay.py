"""Compare production C++ FA to Python on recorded frames, both player sides.

python tests/compare_replay.py --exe PATH --python-root PATH --out TEMP_DIRECTORY
Run from dll/. Captures stay in the Python checkout; no game is needed.
"""
import argparse
import configparser
import json
from pathlib import Path
import struct
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--exe', type=Path, required=True)
parser.add_argument('--python-root', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
config = configparser.ConfigParser()
config.read(Path(__file__).resolve().parents[2] / 'config/memory_address.ini')
offsets = {name: int(value.split()[0], 16)
           for name, value in config['PlayerDataAddress'].items()}
for tag in ('jin_defs1', 'lars_df3d_01', 'jin_heat_engager_01', 'lars_f21_stance_01',
            'kunimitsu_qcf1_01', 'victor_iai_block_01', 'victor_d12_01',
            'dragunov_throws_01', 'dragunov_throw_defs_02', 'dragunov_throw_breaks_01',
            'victor_b12_f12_heat_smash_01'):
    capture = args.python_root / 'captures' / (tag + '.fields.jsonl')
    converted = args.out / (tag + '.txt')
    state = {}
    with capture.open(encoding='utf-8') as source, converted.open('w') as dest:
        dest.write(str(len(offsets)) + ' ' + ' '.join(offsets) + '\n')
        for n, line in enumerate(source):
            row = json.loads(line)
            values = [row['f']]
            for side in ('p1', 'p2'):
                if n == 0:
                    state[side] = row[side]
                else:
                    for i, value in row[side]:
                        state[side][i] = value
                block = struct.pack('<%di' % len(state[side]), *state[side])
                values.extend(struct.unpack_from('<I', block, off)[0]
                              for off in offsets.values())
            dest.write(' '.join(map(str, values)) + '\n')
    python_output = subprocess.check_output(
        [sys.executable, 'tools/replay_fields.py', str(capture)],
        cwd=args.python_root, text=True)
    expected = []
    for line in python_output.splitlines():
        row = json.loads(line)
        fa = row['entry'].get('fa', '')
        if fa:
            expected.append('%s\t%s' % (row['frame'], fa))
    for swap in (False, True):
        actual = subprocess.check_output(
            [str(args.exe), str(converted)] + (['swap'] if swap else []), text=True).splitlines()
        if actual != expected:
            raise AssertionError((tag, swap, expected, actual))
        print('%s swap=%s: %d entries match' % (tag, swap, len(actual)))
