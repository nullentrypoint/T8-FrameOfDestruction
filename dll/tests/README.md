# Frame advantage replay checks

## Lounge diagnostic reader

From `dll/` in an x64 Visual Studio developer prompt (keep assertions enabled):

```bat
set "T8_TEST_OUT=%LOCALAPPDATA%\Temp\opencode"
cl /nologo /std:c++20 /EHsc /MD /DNOMINMAX /I. tests\lounge_reader.cpp VoiceChat\LoungeReader.cpp /Fo"%T8_TEST_OUT%\\" /Fe"%T8_TEST_OUT%\t8_lounge_reader.exe"
"%T8_TEST_OUT%\t8_lounge_reader.exe"
cl /nologo /std:c++20 /EHsc /MD /DNOMINMAX /I. tests\lounge_probe.cpp VoiceChat\LoungeReader.cpp /Fo"%T8_TEST_OUT%\\" /Fe"%T8_TEST_OUT%\t8_lounge_probe.exe"
"%T8_TEST_OUT%\t8_lounge_probe.exe" GAME_PID ..\config\memory_address.ini
```

The synthetic tests cover sparse slots spanning multiple bitmap words, poisoned
free entries, world rather than relative coordinates, UTF-16 display names,
Steam-candidate platform filtering, stale room state in the main menu, build
guards, unsupported allocation layouts, failed reads, malformed data and mutation
of the world, container, identities and room string during rereads. The external
probe uses the production reader with `ReadProcessMemory` and read-only process
access; it does not inject a library or send packets. Run elevated if process
access requires it. Exit 0 means either a valid lounge snapshot or a positively
identified non-lounge mode; always check the printed status.

In the DLL console, `lounge_info` reads a single fresh snapshot and prints the
opaque room key, local position, and participants sorted by 3D distance in game
units. It reports raw platform values and whether a Steam ID candidate is present,
without printing raw player IDs. Each invocation reloads the independent
`[VoiceLounge]` section from the normal runtime INI location. Failed snapshots
contain no participants; there is no fallback to the previous room.

This is an on-demand diagnostic, not a voice session or a proximity audio mixer.
The reader gates fixed structure offsets on PE timestamp and image size and
validates classes before lounge-specific traversals. Only the observed inline
128-bit allocation bitmap is supported. Larger/heap-backed layouts report
`unsupported-layout`. Structural and identity rereads detect observed changes,
but cannot guarantee an atomic snapshot or detect every ABA change. Positions
are sampled independently. See `lounge_voice_research.md` for live evidence and
remaining room-key/zone/transport validation.

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
cl /nologo /std:c++20 /EHsc /MD /DT8_MENU_LAYOUT_TEST /I. tests\menu_layout.cpp menu.cpp ini.cpp imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_widgets.cpp imgui\imgui_tables.cpp /Fo%TEMP%\ /Fe%TEMP%\t8_menu_layout.exe /link /OPT:REF
%TEMP%\t8_menu_layout.exe
```

Runs the real menu headlessly with Consolas Bold. Checks window bounds after
1440p/720p/4K/800x600 and aspect-ratio changes, reopening after a resolution
change while hidden, scaled font size, and restoration of shared font/style state.
Also exercises the compact six-column voice table with 64 participants, long
names and unavailable connection data at 800x600, 1080p and 4K, checking table
bounds, positive column widths and vertical scrolling. Visual appearance and
mouse interaction still need in-game verification.

## Voice participant presentation and lounge HUD

```bat
cl /nologo /std:c++20 /EHsc /MD /DNOMINMAX /I. tests\voice_participants.cpp /Fo%TEMP%\ /Fe%TEMP%\t8_voice_participants.exe
%TEMP%\t8_voice_participants.exe
```

Checks lounge/match row selection, stale-snapshot expiry, independent game and
Steam names, disconnected/stale signal suppression, direct Unreal-coordinate
projection, behind-camera/off-screen rejection and world changes during camera
reads. These mathematical tests do not validate visual head alignment.

The existing guarded Steam diagnostic/voice worker now publishes lounge members
every 200 ms when the voice probe is enabled. Profile refresh work is bounded to
two Steam users per update and cached per room. It uses the existing SDK shutdown
guard and does not introduce a render-thread Steam call. Runtime diagnostics and
voice-probe opt-in marker files are still required as before. `[VoiceLounge]` and
`[SpatialCamera]` must be present in the deployed INI.

The voice page shows a scrollable list with avatar, game name / Steam persona,
connection ping, mute checkbox, receive-volume slider, and fresh signal meter.
The local row is labelled `You`; microphone gain remains in the audio controls.
Left-click an avatar to open its Steam profile in the overlay; right-click to
open the profile menu or copy its link. Per-peer mute/volume use the existing
Steam-ID preference store. Console/non-Steam rows are omitted. Self is first,
then connected peers, then disconnected peers; each peer group is sorted by game
nickname using case-insensitive Unicode collation, with stable identity ties.
The room key is retained internally but is not displayed in the menu.
In matches, a previously observed lounge nickname can be reused for the same
Steam ID; otherwise the game-name field explicitly reports unavailable rather
than substituting the Steam persona.

Lounge Steam avatars use the same icon/badge renderer as the match HUD. Current
camera matrices and actor world positions are read for each rendered frame;
only confirmed voice connections are drawn in the world (self is shown once a
peer connects). Disconnected Steam participants remain available in the menu.
world mismatch or snapshots older than one second suppress the lounge overlay.
The head anchor currently uses root world Z + 110 game units; visual calibration
is still needed. World markers render on the background draw list, beneath menu
and console windows. They are drawn far-to-near by camera distance, so nearby
icons cover distant icons. Markers beyond 2000 Unreal world units from the camera,
outside the viewport or behind the camera are omitted; there is no wall-occlusion
test. `lounge_avatar_distance` reports the current camera-distance limit;
`lounge_avatar_distance 3500` changes it immediately and saves
`[VoiceChat] lounge_avatar_distance` in user settings. Valid integer values are
0..100000; 0 hides world markers and `reset` restores 2000. Profile visibility
alone is not a voice connection: peers need the compatible lounge transport and
a completed nonce handshake before ping/signal/connected badges are shown.

## Lounge voice transport

Fight Lounge hearing is always **unlimited within the same room**. The former
`lounge_voice_distance` command and settings read/write have been removed;
old INI values are ignored. Use the voice-chat enable checkbox to disable audio.
`lounge_avatar_distance` only controls world-marker visibility.
Mute blocks both send and receive for that peer; volume affects receive only.

The guarded voice worker uses Steam Messages channel 21561, separate from match
channel 21560. Room-bound versioned envelopes carry fresh per-pair nonces,
PCM16 mono 16 kHz / 20 ms blocks, and microphone state. Each peer has replay
protection and a bounded jitter queue with 40 ms startup; streams mix with
saturated output. Connected peers publish Steam session ping/quality, audio
counters, microphone state and attenuated signal level. Steam session byte
statistics can include other traffic sharing that Steam session.

Roster reads occur every 200 ms; world/mode/active flags are checked each worker
tick. Stale snapshots, failed reads, room/world changes and disabling voice end
lounge transport and flush playback. Audio routes exclusively to lounge or match;
match routing resumes only after a confirmed non-lounge snapshot.

Standalone deterministic transport test (from `dll/`, developer cmd):

```bat
cl /nologo /std:c++20 /EHsc /MD /DNOMINMAX /I. tests\lounge_voice.cpp /Fo"%T8_TEST_OUT%\\" /Fe"%T8_TEST_OUT%\t8_lounge_voice.exe"
"%T8_TEST_OUT%\t8_lounge_voice.exe"
```

Tests cover three clients, simultaneous speakers, clipping, attenuation,
mute/volume, malformed/wrong-room/wrong-sender packets, sequence wrap/reordering,
nonce rotation, timeout/reconnect, room changes and disabling transport.
Real Steam delivery, microphone playback quality and multi-speaker audio still
require compatible live clients. Test room exit, entering a match, mute/unmute,
moving through the hearing boundary, and a third simultaneous speaker.

## Custom-room voice (Player Match / Lobby)

Requires the new `[VoiceCustomLobby]` section in the runtime memory INI and the
same transport-capable build on each Steam client. `CustomLobbyReader` follows
the session owner's live shared pointer and bounded member map, checks the PE
fingerprint/vtables, validates duplicate Steam identities and rereads captured
fields to reject observed mutation. It never uses the persistent global room-ID
cache as proof of membership.

Custom rooms reuse the multi-peer transport on channel 21561 with a distinct
`custom:PLM...` room key. Every member can hear every connected member; both
`lounge_voice_distance` and `lounge_avatar_distance` are irrelevant here.
The local Steam identity must be present in the current roster to enable audio.
Room voice takes priority over opponent-only voice while the custom session is
active. A session-pointer change is detected on each worker tick; complete
rosters refresh every 200 ms. Room/mode changes reset queued capture and playback.

The HUD uses one horizontal row from the upper-right corner towards the left,
under menu windows. Only confirmed voice connections are shown, including self
once at least one peer has connected. Existing speech glow, microphone-state and
per-peer mute badges are reused. The row shrinks to fit narrow viewports rather
than wrapping; menu participants retain the complete Steam roster and controls.

From `dll/` in a developer command prompt:

```bat
cl /nologo /std:c++20 /EHsc /MD /DNOMINMAX /I. tests\custom_lobby_reader.cpp /Fo"%T8_TEST_OUT%\\" /Fe"%T8_TEST_OUT%\t8_custom_reader.exe"
"%T8_TEST_OUT%\t8_custom_reader.exe"
cl /nologo /std:c++20 /EHsc /MD /I. tests\custom_lobby_probe.cpp /Fo"%T8_TEST_OUT%\\" /Fe"%T8_TEST_OUT%\t8_custom_probe.exe"
"%T8_TEST_OUT%\t8_custom_probe.exe" GAME_PID ..\config\memory_address.ini
```

The probe is an external read-only check of the production reader. It matched
both live room participants after process restart. Synthetic tests cover member
removal, local exit, malformed/cyclic lists, build guards, mutation rejection,
room-wide mixing with hearing radius zero, local-membership gating, HUD filtering
and horizontal layout. Real Steam audio and HUD appearance still require an
in-game test with compatible clients. Host migration, fighting and spectator
transitions remain live-verification cases.

## Voice regression diagnostics

`voice_debug on` / `voice_debug off` (or **Detailed voice debug log** under
**Settings → SYSTEM / Diagnostics & defaults**) persists `[VoiceChat] debug_log`. With the existing voice
worker enabled, detailed snapshots are appended every two seconds to
`plugins/steam_diagnostics.log`. `voice_debug` prints the state and full path.
Logs include room-reader status, room key, local Steam identity, roster/ranges,
eligible/connected peers, capture/mute state, HELLO failures, ACK age, incoming
and rejected envelopes, and PCM send/receive counters; audio content is not saved.
For a Fight Lounge failure, enable on both clients before entering, stand within
the configured hearing radius, toggle the microphones and retain both logs.

The mixer now advances by fixed 20 ms sample deadlines and drains due blocks,
instead of resetting each deadline from a coarse polling timestamp. Playback
primes two blocks of scheduling headroom after an underrun. Repeated muted-peer
keepalives no longer flush everyone else's audio. The transport harness simulates
ten seconds of 31 ms worker ticks with a muted third client and asserts sustained
50-block/s output without recurring resets.

For a verified local/opponent identity pair, the custom-room HUD shows only
those connected fighters at the shared `voice_preview` positions; spectators
remain in the menu. Live spectator-viewer fighter identity mapping is not yet
established. The self microphone badge reflects the user's mute/test/enable
setting independently of whether capture has opened. Hotkey and both microphone
checkboxes now show a green enabled / red crossed-out muted microphone notice
instead of the generic successful-settings-save notice.

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
