"""Replay local CE T8TW0002 captures against the production C++ tracker.

Requires the research analyzer's adjacent .events.json files (and the chain
.chain.events.json / .extra.moves.txt where applicable). Captures are not bundled.
"""
import argparse
import json
import math
import re
import struct
import subprocess
from pathlib import Path

RANGES = [(0x300, 0x600), (0x2300, 0x100), (0xd500, 0x100),
          (0x3e80, 0x20), (0xf50, 0xc0), (0x1500, 0x100),
          (0x1c00, 0x20), (0x39e0, 0x40)]
PLAYER = 4 + sum(size for _, size in RANGES)


def metadata(path):
    result = {}
    paths = [Path(str(path) + '.moves.txt')]
    extra = path.with_suffix('.extra.moves.txt')
    if extra.exists():
        paths.append(extra)
    for source in paths:
        for line in source.read_text().splitlines():
            if 'pointer=' in line:
                pointer = int(re.search(r'pointer=([0-9A-F]+)', line)[1], 16)
                result[pointer] = []
            elif line.startswith('cancel='):
                fields = dict(item.split('=', 1) for item in line.split())
                command = int(fields['cmd'], 16)
                if command not in (0, 0x4000000300000000):
                    continue
                buttons, counter, held, supported, terminated, tackle = 0, -1, 0, True, False, 0
                ground, marker = 0, False
                for pair in fields['req'].split(','):
                    code, param = (int(x, 16) for x in pair.split(':'))
                    if code == 0x44c:
                        terminated = True
                        break
                    if code == 0x1af:
                        supported &= ground == 0 and param in (1, 2, 3)
                        ground = param
                    elif code == 0x840d:
                        supported &= not marker and param == 1
                        marker = True
                    elif code in (0x1cc, 0x1cd):
                        supported &= param == 0 and counter == -1
                        counter = int(code == 0x1cc)
                    elif code in (0x83, 0x84, 0x85):
                        supported &= param == 0 and buttons == 0
                        buttons = 1 << (code - 0x83)
                    elif code == 0x87:
                        supported &= param in (3, 5) and buttons == 0
                        buttons, held, tackle = (4, 0, 1) if param == 3 else (1, 1, 0)
                    else:
                        supported = False
                if ground or marker:
                    supported &= bool(ground and marker and not buttons and counter == -1 and command == 0)
                    if supported:
                        buttons = 1 << (ground - 1)
                        supported &= all(int(fields[k]) == 1 for k in ('start', 'end', 'transition'))
                if supported and terminated and buttons and (command == 0 or tackle):
                    result[pointer].append([buttons, int(fields['start']), int(fields['end']),
                                            int(fields['transition']), int(fields['target']), counter, held, tackle, ground])
    return result


def convert(path, output):
    raw = path.read_bytes()
    stride = 4 + 2 * PLAYER
    assert raw[:8] == b'T8TW0002' and (len(raw) - 8) % stride == 0
    rules = metadata(path)
    frames = []
    with output.open('w') as out:
        for pos in range(8, len(raw), stride):
            frame = struct.unpack_from('<I', raw, pos)[0]
            frames.append(frame)
            values = [frame]
            for side in range(2):
                base = pos + 4 + side * PLAYER

                def get(offset, fmt='I'):
                    cursor = base + 4
                    for start, size in RANGES:
                        if start <= offset and offset + struct.calcsize('<' + fmt) <= start + size:
                            return struct.unpack_from('<' + fmt, raw, cursor + offset - start)[0]
                        cursor += size
                    raise ValueError(offset)

                branches = rules.get(get(0x3e0, 'Q'), [])
                values += [struct.unpack_from('<I', raw, base)[0], get(0x550), get(0xd538),
                           get(0x1588), get(0x390), get(0x3bc, 'f'), get(0x698), get(0x3e8c),
                           int(bool(get(0x6e8, 'B'))), int(bool(get(0x1c08, 'B'))), get(0x1564), len(branches)]
                for branch in branches:
                    values += branch
            out.write(' '.join(map(str, values)) + '\n')
    return frames


def check(exe, capture, output):
    fixture = output / (capture.stem + '.txt')
    convert(capture, fixture)
    events_path = capture.with_suffix('.events.json')
    events = json.loads(events_path.read_text()) if events_path.exists() else []
    chain_path = capture.with_suffix('.chain.events.json')
    if chain_path.exists():
        # Chain event records include initial grabs and every breakable link.
        events = json.loads(chain_path.read_text())
    assert events, f'No reference events: {capture}'
    checked = 0
    for swap in (False, True):
        command = [str(exe), str(fixture)] + (['swap'] if swap else [])
        result = subprocess.run(command, check=True, capture_output=True, text=True)
        rows = {}
        for line in result.stdout.splitlines():
            index, side, *view = map(int, line.split())
            rows[index, side] = view
        for event in events:
            side = event.get('defender', 2) - 1
            start = event['index']
            duration = event.get('duration', event.get('length'))
            # Inspect before transition/reset; this also covers interrupted events.
            row = rows.get((start + duration - 1, side))
            assert row is not None, (capture.name, event, 'missing timeline')
            buttons, startup, window, edge, accepted, animation, outcome, held = row[:8]
            if capture.name.startswith('throw_tackle_'):
                # Recorded normal Dragunov entry/reversal, not the ground action.
                assert event['move'] in (2079, 2091) and event['ch'] == 0
                assert buttons == 4 and startup == 26 and not held, (event, row)
                within = [f for f in event['accepted'] if f < duration]
                assert accepted == (within[0] if within else -1), (event, row)
                assert outcome == (1 if within else 2), (event, row)
                assert row[8:13] == ([2, 0, 6, -1, -1] if duration == 13 else [2, 0, 6, 22, 24]), (event, row)
                # No FAILED/reveal between the two windows. The first deadline
                # must not start the card expiry timer before the reversal.
                if not within or within[0] >= 22:
                    for offset in range(6, 23):
                        assert rows[start + offset, side][6] == 0, (event, offset)
                after = rows[start + duration, side]
                if not after[-2]:
                    assert after[4] == accepted, (event, after, 'ground acceptance leaked')
                    assert after[5] == (duration if within else -1), (event, after)
                checked += 1
                continue
            expected_buttons = sum({'1': 1, '2': 2, '1+2': 4}[b] for b in event.get('buttons', ['1']))
            assert buttons == expected_buttons, (capture.name, event, row, 'buttons')
            expected_window = event['closed']
            if expected_window is None:
                expected_window = math.ceil(event['bounds'][0][1] / event['rate']) if event['rate'] > 0 else -1
            assert window == expected_window, (capture.name, event, row, 'window')
            if 'startup' in event:
                assert startup == event['startup'], (capture.name, event, row, 'startup')
            expected_accepted = event['accepted'][0] if event['accepted'] else -1
            assert accepted == expected_accepted, (capture.name, event, row, 'acceptance')
            assert outcome == (1 if expected_accepted >= 0 else 2), (capture.name, event, row, 'outcome')
            expected_held = event.get('held_before', 0) > 0
            assert bool(held) == expected_held, (capture.name, event, row, 'held')
            expected_edge = event['edges'][0][0] if event['edges'] and not expected_held else -1
            assert edge == expected_edge, (capture.name, event, row, 'input')
            if event['accepted'] and not event['interrupted']:
                after = rows[start + duration, side]
                assert after[5] == duration, (capture.name, event, after, 'animation')
            checked += 1
    print(f'PASS {capture.name}: {checked} event checks across both orientations')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('captures', type=Path, nargs='+')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for capture in args.captures:
        check(args.exe, capture, args.out)
