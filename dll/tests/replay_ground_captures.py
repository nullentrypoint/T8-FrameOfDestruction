"""Verify ground continuations with the production tracker and local CE captures."""
import argparse
import struct
import subprocess
from pathlib import Path
from replay_throw_captures import convert, RANGES, PLAYER


def check(exe, path, out):
    fixture = out / (path.stem + '.txt')
    convert(path, fixture)
    raw = path.read_bytes()
    stride = 4 + 2 * PLAYER
    frames = []
    for pos in range(8, len(raw), stride):
        players = []
        for side in range(2):
            cursor = pos + 8 + side * PLAYER
            values = {}
            for start, size in RANGES:
                for offset, fmt in ((0x550, 'I'), (0x1564, 'I'), (0x1588, 'I'), (0x6e8, 'B')):
                    if start <= offset < start + size:
                        values[offset] = struct.unpack_from('<' + fmt, raw, cursor + offset - start)[0]
                cursor += size
            players.append(values)
        frames.append((struct.unpack_from('<I', raw, pos)[0], players))
    attempts = []
    for i, (frame, players) in enumerate(frames[1:], 1):
        if frame != frames[i-1][0] + 1:
            continue
        for side, player in enumerate(players):
            move = player[0x550]
            if move not in (2163, 2165, 2167) or move == frames[i-1][1][side][0x550]:
                continue
            end = next(j for j in range(i+1, len(frames))
                       if frames[j][0] != frames[j-1][0]+1 or frames[j][1][side][0x550] != move)
            assert frames[end][0] == frames[end-1][0]+1
            won = frames[end][1][side][0x550] == 2080
            assert won == (players[1-side][0x1564] == {2163: 1, 2165: 2, 2167: 3}[move])
            attempts.append((i, end, side, move, won))
    # Explicitly established provenance examples, not guesses from acceptance.
    origins = {
        'throw_buffer_boundary_20261105_132736': {12097: 3, 13217: 1, 13704: 1, 16183: 2},
        'throw_followup_20261105_130501': {69977: 3, 70335: 3, 70693: 3, 71469: 1, 72583: 2},
        'throw_followup1_20261105_131725': {82720: 1, 83102: 1, 83484: 2, 85394: 2},
    }.get(path.stem, {})
    for swap in (False, True):
        run = subprocess.run([str(exe), str(fixture)] + (['swap'] if swap else []),
                             check=True, capture_output=True, text=True)
        rows = {}
        for line in run.stdout.splitlines():
            index, side, *view = map(int, line.split())
            rows[index, side] = view
        for start, end, side, move, won in attempts:
            first, last = rows[start, side], rows[end, side]
            assert first[-2] == last[-2] == 1, (path, start, first, last)
            assert first[6] == 0, (path, start, 'must await target/damage')
            assert last[0] == {2163: 1, 2165: 2, 2167: 4}[move]
            assert last[1:4] == [0, -1, -1], (path, start, 'invented startup/window/reaction')
            assert last[6] == (1 if won else 2), (path, start, last)
            assert last[4] == (0 if won else -1)
            assert last[5] == (end-start if won else -1)
            if won:
                assert last[-1] in (1, 2, 3)
                frame = frames[start][0]
                if frame in origins:
                    assert last[-1] == origins[frame], (path, frame, last)
        if path.stem.startswith('throw_no_followup_'):
            assert not attempts and all(not row[-2] for row in rows.values()), 'fabricated ground event'
    print(f'PASS {path.name}: {len(attempts)*2} ground outcomes/origins, both orientations')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('captures', type=Path, nargs='+')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for capture in args.captures:
        check(args.exe, capture, args.out)
