# Frame advantage replay checks

## Memory reader crash regression

From `dll/` in an x64 Visual Studio developer prompt (assertions enabled):

```bat
cl /nologo /std:c++20 /EHsc /MD /I. tests\reader_memory.cpp TekkenBot\MoveInfoEnums.cpp /Fo%TEMP%\ /Fe%TEMP%\t8_reader_memory.exe /link /OPT:REF
%TEMP%\t8_reader_memory.exe
```

Exercises valid reads, the reported `0xffffffffffffffff` address, inaccessible
pages, reads across a protected page boundary, freed memory, and short frame
buffers. Checks that a failed read logs an update instruction once and disables
subsequent game-data reads. Uses a temporary config; no game required.

## Menu layout check

From `dll/` in an x64 Visual Studio developer prompt (keep assertions enabled):

```bat
cl /nologo /std:c++20 /EHsc /MD /I. tests\menu_layout.cpp menu.cpp ini.cpp imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_widgets.cpp imgui\imgui_tables.cpp /Fo%TEMP%\ /Fe%TEMP%\t8_menu_layout.exe /link /OPT:REF
%TEMP%\t8_menu_layout.exe
```

Runs the real menu headlessly with Consolas Bold. Checks window bounds after
1440p/720p/4K/800x600 and aspect-ratio changes, reopening after a resolution
change while hidden, scaled font size, and restoration of shared font/style state.
Visual appearance and mouse interaction still need in-game verification.

## Replay checks

Run from `dll/` in an x64 Visual Studio developer prompt:

```bat
cl /nologo /std:c++20 /EHsc /MD /I. tests\replay.cpp TekkenBot\MoveInfoEnums.cpp /Fo%TEMP%\ /Fe%TEMP%\t8_replay.exe /link /OPT:REF
python tests/compare_replay.py --exe "%TEMP%\t8_replay.exe" --python-root "PATH\TekkenBot420" --out "%TEMP%\cpp_replay"
```

The Python checkout must contain `tools/replay_fields.py` and the captures
`jin_defs1`, `lars_df3d_01`, `jin_heat_engager_01`, `lars_f21_stance_01`,
`kunimitsu_qcf1_01`, `victor_iai_block_01`, and `victor_d12_01`
(`.fields.jsonl`).
The harness feeds the real signed DWORD decoder, PlayerSnapshot and GameLog;
only rendering and logging are stubbed. It compares every emitted FA and its
frame against Python, then repeats with the players exchanged. No game process
is opened. All 482 emission/update events matched across twenty-two replays,
including `dragunov_throws_01` and `dragunov_throw_defs_02`. Agreement between
ports does not resolve the reported +1 for Dragunov 1+3: both captures yield 0
from equal recovery lengths/timers. f3+2 (-1) and uf1+2 (-10) are confirmed.
`dragunov_throw_breaks_01` additionally checks successful breaks: f3+2/1+3
produce -2 and uf1+2 produces -6, from the thrower's perspective. The separate
break entry waits for paired publications matching both recovery timers.

`victor_b12_f12_heat_smash_01` adds five b1+2 hits (+16), four f1+2 Heat
Engagers (+9), and four Heat Smash hits (+5). It covers the alternative Heat
run/recovery animations, retained recovery endpoints after the attacker starts
walking, and synchronized cinematic recovery changes. b1+2 and Heat Smash
revise the existing floating label; Heat Engager waits for the final publication.

Verified values (block / hit):

- Jin f4: -8 / +4; f4~f: -1 / +11.
- Jin 3,1: -1 / +7; 3,1~f: +4 / +12.
- Lars df3~d: -4 / +15; ordinary df3: -11 / +8.
- Five Jin Heat Engagers: +9, read from the game's signed publication.
- Victor d1+2: -9 on block, JGL on launch. Category 0x100003 can publish
  within the original animation too; a restarted recovery is not required
  when a fresh signed pair agrees with the recovery timers. Launch/knockdown
  labels remain in the log, but their flying text is hidden. Multi-hit parent updates
  cancel the matching pending continuation to avoid duplicate labels.
- Victor Iai: 1+2 and 1,2 update to +3 (seven cases), delayed 1,2 to -19
  (three cases), d2 to +4 (four cases). Updates follow recovery chains and
  timer-preserving multi-hit continuations, requiring a fresh opposite-signed
  publication matching both remaining recovery timers. The existing floating
  label is revised by ID after publication; an initial estimate may appear first.
- Kunimitsu qcf1: eight hits (-4) and seven blocks (-13), close and far.
  Category 0x100003 waits for actual contact, restarted recovery and a published
  FA matching both recovery timers. Intermediate +37/-2 and pre-contact -36 are
  not emitted. The original strike identity is retained, without move-ID or FA
  overrides. Other projectile categories are not validated by this capture.
- Lars f2,1: four late stance transitions revise the existing -6 display to -5.
  Text is updated by a stable ID, preserving its position/fade and avoiding a
  duplicate floating label or changes to the other player's label.

Action frames are learned from changes at +0x5e4, including changes on entry
to a new animation. Live GameLog loads `actionable_frames.json` beside
`memory_address.ini` on its first update and saves changed values through a
temporary file and atomic replacement. The JSON format matches Python:
`{"6:1889": 26, "17:1719": 26}`. Missing/malformed files are ignored; a save
failure is logged without discarding the in-memory learning. Resetting practice
retains learned values. The replay harness disables live persistence and tests
round-trip, changed values, Python JSON compatibility and malformed/missing
files using a separate temporary file. Jin's first stance
entry waits one frame when necessary. Heat entries wait for the matching signed
pair at +0x2354 during the run-in, retaining the original strike metadata.
No move overrides or expression evaluator are used.

### Dragunov Heat Engagers against Lars and Jin

`dragunov_heat_live.txt` (external, 10058 frames) reproduces WS1+2 +60,
1,2,1 +43, qcf4 +46 and b4,3 +49 before the eventual +9 update.
Dragunov's run is 369. Lars uses reaction 378 for 1,2,1/b4,3, 379 for
qcf4 (both retain the animation through recovery), and 380 -> 381 for
WS1+2. Animation variants belong to each player's moveset independently.
The pending Heat entry waits for opposite signed publications corroborated
by both recovery timers; the value is read from the game, not fixed to +9.
Earlier string labels stop following publications once the engager starts,
so the final publication does not also revise an earlier hit's label.

```bat
python tests/replay_heat_capture.py --exe "%TEMP%\t8_replay.exe" --capture "PATH\dragunov_heat_live.txt"
```

Checks all 27 events in both orientations, including reset boundaries,
preliminary string hits, final +9 values and absence of duplicate Heat labels.

`dragunov_heat_jin.txt` (external, 2740 frames) confirms the same three
reaction paths against Jin. They are shared animation variants, not
Lars-specific IDs. All four engagers emit only the final +9; the preceding
1,2 and b4 string hits retain their own labels (seven events total).

```bat
python tests/replay_heat_capture.py --exe "%TEMP%\t8_replay.exe" --capture "PATH\dragunov_heat_jin.txt" --opponent jin
```

Build the DLL from the repository root:

```bat
msbuild dll\dll.vcxproj /p:Configuration=Release /p:Platform=x64 /m
```

Deploy the updated `config/memory_address.ini` alongside the DLL at its expected
runtime path: `<DLL directory>\T8FrameOfDestruction\config\memory_address.ini`.
The old config lacks the actionable/Heat fields and uses offsets from a previous
game build. DLL output for the direct project build is `dll/x64/Release/dll.dll`.

## Flying text position

`SETTINGS > Frame Advantage Hints > Start Position` selects the saved setting
`startPositionMode` (0: legacy random center, 1: impact / hitbox). Missing,
invalid, behind-camera or off-screen position data falls back to screen center
in mode 2. TEST buttons have no associated strike and use that fallback.

The optional `SpatialPlayerData` and `SpatialCamera` INI sections were located
on 3.02.02 in practice. Receiver +0x3560 is the cached contact point; attacker
+0x2950 is the first attack hitbox position. Captures verified blocked strikes,
whiffs, four Victor 1 hits and four d+4 hits. Only fresh contact is consumed;
whiffs use the attack's active-window hitbox, never an idle/recovery value.
Heat entries preserve the original world point while waiting for publication.
Late FA revisions retain the existing label's position and animation.

Projection uses the local player's FSceneViewState double view matrix (+0x8e0)
and projection without temporal jitter (+0x7e0). PlayerCameraManager's cached
90-degree FOV did **not** match the rendered projection. The matrix-based
projection was checked against character joints on a captured game image.
The Tekken-to-Unreal conversion is `(z, -x, y) * 0.1`.

Run origin selection, stale-contact, player-swap, reset, delayed-contact,
projection and invalid-data checks from `dll/` (keep assertions enabled):

```bat
cl /nologo /std:c++20 /EHsc /MD /I. tests\spatial.cpp TekkenBot\MoveInfoEnums.cpp /Fo%TEMP%\ /Fe%TEMP%\t8_spatial.exe /link /OPT:REF
%TEMP%\t8_spatial.exe
```

Optional argument: `t8_spatial_capture2.bin` from the live investigation (not
bundled). Its 1500 records contain a little-endian u32 frame, 64 bytes of the
old camera POV (ignored), and 0x1b900 bytes of rollback frame data. The replay
checks all eight fresh contact origins and eight emitted labels in both player
orientations. The fourteen existing FA replays also still match (346 events).
Menu interaction and animated text rendering need an injected in-game check.

## Hit rings

`SETTINGS > Frame Advantage Hints > Hit rings` controls expanding hollow rings
on hit/block publications. Rings follow the flying label's center and opacity;
whiffs have no ring. Launch/knockdown contacts can show a ring while their
`JGL`/`KND` labels stay hidden. Delayed Heat publications retain contact status
and attack type from the original strike.

Enabled by default, with a 3-pixel line (adjustable from 1 to 12 pixels).
Default colors: S.MID green, S.LOW cyan, MID yellow, HIGH red, LOW blue.
MID_UNBLOCKABLE uses the MID color; unknown types have no ring.
Settings persist in the user-settings INI under `[HitRings]`: `enabled`,
`thickness`, and each type's `_r`, `_g`, `_b` components. Reset to defaults
restores all ring settings. Each color has a `TEST ring` button for checking
animation, thickness and fade in-game without a strike.

## World-locked hints

`Lock text and ring to world position` is enabled by default and saved as
`[FrameAdvantageHints] lockToWorld=true`. New labels preserve the original
3D contact/attack point and reproject it using the current camera each frame,
including when the simulation frame is unchanged. Upward flight is applied to
the world's Y axis before projection; Fly Speed uses game units per frame
in this mode (pixels per frame otherwise). Ring expansion and fade continue.
This overrides Start Position while
enabled. Toggle off for the previous start-position and flight behavior.

The setting is captured at label creation. Missing world data (including TEST)
uses screen-center placement with ordinary screen-space flight. Missing cameras, points behind the
camera and off-screen anchors temporarily hide world-locked labels while their
lifetimes continue. Late FA updates retain the original world point.

Headless overlay checks, from `dll/` in an x64 developer cmd prompt:

```bat
cl /nologo /std:c++20 /EHsc /MD /I. tests\world_lock.cpp overlay.cpp imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp /Fo%LOCALAPPDATA%\Temp\opencode\ /Fe%LOCALAPPDATA%\Temp\opencode\t8_world_lock.exe /link /OPT:REF
%LOCALAPPDATA%\Temp\opencode\t8_world_lock.exe
```

Checks camera motion, copied world origins, resize, late FA revisions,
missing-camera/behind-camera hiding, expiration, TEST fallback, world-up flight
under a rolled camera, and opt-out screen-space flight.

## Default settings / reset

Defaults use the Neon text/ring palette, with pale sand `#FFE0AD` for -1..-9,
orange `#FF792E` for punishable -10..-14, and pink-red `#FF527A` for -15 and below.
Defaults also use contact / attack hitbox placement
(`startPositionMode=1`), world locking and rings enabled. Reset assigns the
complete default settings object, recomputes font size from the current display
height (`height / 18`: 60 px at 1080p, 80 at 1440p, 120 at 2160p), then saves
all settings including `debugInfo`. Existing saved customizations take priority
until Reset to defaults is pressed.

The isolated INI roundtrip test checks the exact Neon RGB values, custom
settings preservation, reset and disk reload, plus reset at all three display
heights. From `dll/` in an x64 developer cmd prompt:

```bat
cl /nologo /std:c++20 /EHsc /MD /Gy /I. tests\settings.cpp menu.cpp ini.cpp imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp /Fo%LOCALAPPDATA%\Temp\opencode\ /Fe%LOCALAPPDATA%\Temp\opencode\t8_settings.exe /link /OPT:REF
%LOCALAPPDATA%\Temp\opencode\t8_settings.exe
```
