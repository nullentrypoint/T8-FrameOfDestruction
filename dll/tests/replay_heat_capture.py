"""Check dragunov_heat_live.txt from the Dragunov vs Lars investigation.

Usage: python tests/replay_heat_capture.py --exe PATH --capture PATH
The external capture contains 10058 frames, including practice resets.
"""
import argparse
from pathlib import Path
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument('--exe', type=Path, required=True)
parser.add_argument('--capture', type=Path, required=True)
parser.add_argument('--opponent', choices=('lars', 'jin'), default='lars')
args = parser.parse_args()

# Full event sequence: retain preliminary string hits and unrelated moves,
# but never emit the bogus +60/+43/+46/+49 or duplicate the final Heat label.
expected = [line.split() for line in """
14846 +9
15879 +8
15897 +2
15972 +9
298 KND
484 +9
750 KND
914 +19
1133 0
1269 -41
318 +9
282 +9
280 +9
439 +9
201 +9
345 +9
256 +9
161 +8
179 +2
254 +9
184 +8
202 +2
277 +9
216 +7
289 +9
195 +7
268 +9
""".strip().splitlines()]

if args.opponent == 'jin':
    # dragunov_heat_jin.txt: all four engagers, 2740 captured frames.
    expected = [line.split() for line in """
354 +8
372 +2
447 +9
188 +9
241 +9
160 +7
233 +9
""".strip().splitlines()]

for swap in (False, True):
    output = subprocess.check_output(
        [str(args.exe), str(args.capture)] + (['swap'] if swap else []), text=True)
    actual = [line.split() for line in output.splitlines()]
    if actual != expected:
        raise AssertionError((swap, expected, actual))
    print(f'Dragunov Heat vs {args.opponent} swap={swap}: {len(actual)} events match')
