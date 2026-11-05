# Throw-window investigation (2026-11-04)

Read-only Cheat Engine investigation against the running game. No game memory
writes, injected code, or debugger breakpoints were used.

## Capture

Local evidence: `%LOCALAPPDATA%/Temp/opencode/`:

- `throw_windows_20261104_163508.bin`: 4,348 consecutive game frames,
  93277 through 97624, no missing frames.
- `analyze_throws.py`, `throw_events.py`: capture decoding and event summary.
- `throw_cancel_metadata.txt`: live cancel/requirement records for the five
  captured victim animations.
- `throw_logic.txt`, `throw_input_logic.txt`: relevant disassembly.

There were 16 connected throws, 11 paired break-animation transitions and five
unbroken continuations. No independently labelled counter-hit, side, ground or
chain-throw trials were supplied. Capture character IDs were 6 and 15.

## Findings

### Requirement tag is not a stable description of allowed inputs

`player + 0x698` is encoded and is overwritten while evaluating requirements.
The break-check routine at module RVA `0x18756A0` writes its fourth argument
to this field, then invokes the encoding helper. Thus the tag can reflect the
last evaluated requirement, rather than the complete set of valid breaks.

For example, victim move 2095 initially reports `0xE000001F` (the existing
enum's TE2), but both a 1 input and a 2 input successfully break it. With a 1
input the tag changes to `0xD000001C` before the break animation begins.
Restarting the timeline on that tag change is incorrect.

`0xC000001D` persists during idle and after failed throws. It is not sufficient
evidence of success by itself.

### Accepted break versus animation transition

`player + 0x6E8` is a **byte**, set to 1 by the successful break-check path
(RVA `0x1875806`), with additional special-case conditions in that routine.
In all 11 successful captured cases it changes one snapshot after the observed
input edge and before the paired break-animation transition. It stays zero in
the five failed continuations. Scope its interpretation to a tracked throw;
other code also writes this field.

`player + 0x2359` is a separate byte set by the break notification routine at
RVA `0x190D6D3`. It also correlates with accepted breaks in this capture, but can
persist after the event.

Do **not** use `player + 0x2358` as an authoritative window-closed flag. Its
write at RVA `0x196B34D` is part of publishing frame advantage (`+0x2354` from
`+0x1000` when `+0x1004` is set). Its transition happened to coincide with the
20-frame boundary in the failed examples.

Example (victim move 2095):

| Capture start | Input offset | Accepted-break offset | Animation-transition offset |
| --- | ---: | ---: | ---: |
| 93989 | 7 | 8 | 20 |
| 96370 | 18 | 19 | 20 |
| 97030 | 15 | 16 | 20 |

Offsets are zero-based game-frame differences from the first tagged capture
snapshot. They are not three interchangeable definitions of reaction time.

### Window definitions are in move cancel records

Use the victim's **current move pointer** at `player + 0x3E0`, rather than
looking up the victim's move ID in their own character moveset. During a throw,
the victim can use a move belonging to the attacker's moveset. This occurred in
the capture (victim character 15, move 2095, pointer into character 6's moveset).

Observed layout:

| Location | Meaning |
| --- | --- |
| `current_move + 0x98` | Pointer to cancel records |
| cancel stride `0x28` | One cancel record |
| cancel `+0x00` | 64-bit command; observed list terminator command `0x8000` |
| cancel `+0x08` | Requirements pointer |
| cancel `+0x18` | Start frame |
| cancel `+0x1C` | End frame |
| cancel `+0x20` | Transition frame, distinct from end frame |
| cancel `+0x24` | 16-bit target move ID |
| cancel `+0x26` | 16-bit flags; captured break entries have `0x51` |
| requirement stride `0x14` | ID at `+0`, parameter at `+4` |
| requirement ID `0x44C` | Requirements terminator |

Requirement dispatch table: module RVA `0x187E728`, indexed by `(ID - 2)`.
The following branches call the break-check routine with arguments 1, 2, 3:

- `0x83`: break 1.
- `0x84`: break 2.
- `0x85`: break 1+2.

Conditional requirements:

- `0x1CC`: reads the other player's byte at `+0x1C08`.
- `0x1CD`: tests that byte for zero.

These select shorter and longer windows respectively. The second capture below
confirms their normal/Counter Hit selection for the tested throws. The flag writer copies bit 15
of the other actor's `+0xF5C` under another condition, at RVA `0x18F6061`.

Captured move metadata:

| Victim move | Allowed breaks | `0x1CC` window | `0x1CD` window | Transition frame |
| --- | --- | --- | --- | ---: |
| 2095 | 1 OR 2 | 1–10 | 1–15 | 15 |
| 2099 | 1 OR 2 | 1–10 | 1–15 | 15 |
| 2109 | 1+2 | 1–10 | 1–15 | 15 |
| 2071 | 2 | 1–10 | 1–15 | 1 |
| 2067 | 1+2 | 1–10 | 1–15 | 15 |

These are **animation-domain boundaries**, not directly game-frame counts.
The distinct transition frame also explains why reaction should come from input
and acceptance, rather than waiting for the eventual break animation.

### Timing domains

- `+0x390`: integer animation timer, repeats on some consecutive game frames.
- `+0x3B4`: integer elapsed timer, advances every game frame in these captures.
- `+0x3B8`: corresponding float elapsed timer.
- `+0x3BC`: fractional animation time.
- `+0x3C4`: fixed-point animation time (4096 units per animation frame).

In the first captured generic throw, `+0x3C4` starts at 4096 and increments by
3121 each game frame during the opening interval. The effective rate is
`3121 / 4096`, approximately 0.761963 animation frames per game frame.
Consequently the integer animation timer remains within 1–15 for 20 game
snapshots, and would remain within 1–10 for 14 at the same rate. Do not assume
that rate for every move, slowdown condition or future version.

## Integration implications / remaining validation

The existing `ThrowTimelineTracker` still uses the earlier tag-based heuristic;
this investigation does not silently change production offsets or semantics.
Before replacing it:

1. Read cancel definitions with bounded, checked reads, union valid button
   alternatives, and evaluate the applicable conditional branch. Unsupported
   requirements must produce unknown metadata rather than a guessed window.
2. Track the connected animation, not each `throw_tech` change. Use fresh
   accepted-break signals and keep input/acceptance/animation timings separate.
3. Convert animation-domain bounds using observed timing, and verify the exact
   boundary with inputs on either side. The 11 animation transitions at +20
   alone do not prove every endpoint/off-by-one rule of the input evaluator.
4. Record labelled Counter Hit, side, ground and chain examples. The supplied
   online values 14f/14f/7f/9f and held-input behavior are hypotheses for those
   cases, not universally validated constants.
5. For `STARTUP + WINDOW`, use the actual measured startup; 12 + 20 = 32 only
   applies to a 12-frame startup with a 20-frame window.

## Second capture: normal versus Counter Hit

Evidence: `throw_counter_20261104_171526.bin` (header `T8TW0002`), associated
`.bin.moves.txt`, and `analyze_counter.py`, in the same local evidence directory.
The user was instructed to repeat one throw normally, then with Counter Hit
set to Always. The exact input notation was not supplied, but captured victim
move IDs identify two animations, 2095 and 2099, with the same 1 OR 2 branches.

The capture contains 7,228 consecutive frames (231754–238981), no gaps:

| Branch | Throws | Accepted breaks | Unbroken continuations | First animation timer beyond window |
| --- | ---: | ---: | ---: | ---: |
| attacker `+0x1C08 = 0` | 18 | 13 | 5 | +20 game frames |
| attacker `+0x1C08 = 1` | 10 | 6 | 4 | +14 game frames |

The flag switches to the short branch in the later trial group. Both groups
have startup 12 and the same opening animation rate, `3121 / 4096`. All 19
accepted breaks set both `+0x6E8` and `+0x2359` one snapshot after the punch
edge. **All 19 paired break-animation transitions occur at +20**, including
the six Counter Hit successes: the animation transition does not measure the
short window or the player's reaction.

The flag can reset much later in a failed throw (observed at +133), so capture
the applicable condition at the start rather than deriving it from the final
recovery state.

### Boundary/input-processing evidence

All offsets below are zero-based from the first connected/tagged snapshot:

| Start frame | Branch | Raw input edge | Accepted signal | Outcome |
| --- | --- | ---: | ---: | --- |
| 233899 | Normal | +18 | +19 | Broken |
| 232909 | Normal | +20 | none | Unbroken |
| 238129 | Counter Hit | +11 | +12 | Broken |
| 238459 | Counter Hit | +13 | none | Unbroken |

In the last case, at +13 the integer animation timer still equals 10. The
input field becomes `0x200` and `+0xFA8` becomes `0x22`, but `+0xFAC` remains
`0xFFFFFFFF`. At +14, `+0xFAC` becomes `0x20`; the animation timer is now 11,
beyond the short branch's end of 10, and no acceptance flag is set. In the
successful +11 case the same latch transition occurs at +12, while the timer
is 10, and acceptance is set then.

Thus **do not infer success from raw input time alone** or move the accepted
marker onto the raw input edge. The 14/20-frame window and the input sampling
pipeline have different origins. A precise last-raw-input claim still needs
the missing immediate-boundary samples (Counter Hit +12 and normal +19).
No silent one-frame correction should be applied to captured input timestamps.

Automated checks over this capture passed for frame continuity, event counts,
14/20 branch crossings, all 19 input-to-acceptance delays, agreement of the two
acceptance bytes during the opening interval, and all paired transitions at
+20. This verifies the recorded evidence, not new production integration.

## Third capture: side throws, automatic acceptance suspected

Evidence: `throw_side_20261104_174320.bin`, its `.bin.moves.txt`, and
`analyze_throw_v2.py` in the local evidence directory. The analyzer reads window
bounds and button alternatives from saved move metadata rather than assuming
the frontal throws' 15/10-frame animation windows.

7,680 consecutive frames (242580–250259), no gaps, nine connected throws.
Defender is P1 (character 6), attacker P2 (character 15). All nine have the
attacker's `+0x1C08` clear.

| Victim move | Required break | Normal animation window | Short-branch animation window | Break target |
| --- | --- | --- | --- | ---: |
| 2061 | 1 | 1–10 | 1–5 | 280 |
| 2063 | 2 | 1–10 | 1–5 | 282 |

Five throws continue unbroken (four 2061, one 2063). All cross beyond animation
frame 10 at +14 game frames and deal 40 damage. The opening animation rate is
again `3121 / 4096`.

Four throws (two of each move) set `+0x6E8` immediately at the first connected
snapshot and transition to the matching break target one frame later, without
damage. Defender input is **zero throughout all 7,680 recorded frames**;
`+0xFA8` and `+0xFAC` also show no punch input around these acceptances. This is
consistent with training auto-break, not evidence of manual reaction or buffered
held input. The user agreed to repeat with manual breaks.

`+0x2359` is already 1 before three of those four successes, while `+0x6E8`
has a fresh rising edge. This further demonstrates why the persistent
notification byte alone must not drive success detection.

The shorter 1–5 branch is present in both side-throw definitions, but was not
selected in this capture. Do not label side windows universally fixed or claim
a tested Counter Hit side-window duration yet. Likewise, associate 2061/2063
with their actual move metadata, not an unverified anatomical left/right label.

Automated checks passed for continuity, nine events, the five 14f crossings
and damage increases, and four immediate accepted transitions to the declared
targets. Manual input-boundary checks remain pending.

## Fourth capture: manual side-throw breaks

Evidence: `throw_side_manual_20261104_175151.bin`, its `.bin.moves.txt` and
`.events.json`, analyzed by `analyze_throw_v2.py` in the local evidence directory.

6,307 recorded snapshots include **18 practice resets** (19 independent frame
sequences). There are no forward gaps within a sequence. The analyzer now splits
events at resets and identifies them by segment plus frame, not frame alone.
This is important here: many separate throws start at game frame 179.

Attacker is now character 6/P1; defender is character 15/P2. There are 16 side
throws and one incidental frontal throw (victim move 2095, successfully broken).
All captured throws use the normal branch (`attacker +0x1C08 = 0`).

| Victim move | Break | Normal animation window | Break target | Successful / failed |
| --- | --- | --- | ---: | --- |
| 2103 | 1 | 1–10 | 285 | 6 / 3 |
| 2105 | 2 | 1–10 | 287 | 5 / 2 |

The saved metadata again contains alternate 1–5 windows selected by `0x1CC`,
but this capture does not exercise that branch.

All 11 successful side breaks have an actual punch edge, followed one game
snapshot later by fresh `+0x6E8` and `+0x2359` acceptance signals. The move
changes to the declared break target **one snapshot after acceptance**, rather
than waiting until +20 as with the previously captured frontal throws.
They cause no damage.

Examples (zero-based offsets from the first connected snapshot):

| Segment | Start frame | Victim move | Input | Accepted | Break-animation transition |
| ---: | ---: | ---: | --- | ---: | ---: |
| 1 | 179 | 2103 | 1 at +3 | +4 | +5 |
| 6 | 179 | 2103 | 1 at +12 | +13 | +14 |
| 13 | 179 | 2105 | 2 at +3 | +4 | +5 |
| 18 | 179 | 2105 | 2 at +7 | +8 | +9 |

All five failed throws remain in the connected animation through the window;
the animation timer first exceeds 10 at +14 game frames, at the observed rate
`3121 / 4096`. Three move-2103 failures have no punch input and deal 43 damage.
Both move-2105 failures have **wrong-button 1 input**, including an early input
inside the window, and deal 40 damage. They are not evidence that a correctly
timed button 2 was rejected.

The +12 input / +13 acceptance example confirms a manual break near the end of
the side window. There is still no correctly keyed side-throw attempt exactly
at +13 in this recording, so do not claim a directly tested rejection at that
edge. The earlier Counter Hit +13 rejection is a different trial.

Automated assertions passed for all segment boundaries and event counts,
metadata-selected buttons/window/targets, all 11 input-to-acceptance and
acceptance-to-animation delays, the five +14 window crossings and damage
increases, the late success, and both wrong-button failures. No event was
truncated by a reset. Production reader/tracker integration remains separate.

## Fifth capture: Counter Hit side throws

Evidence: `throw_side_counter_20261104_175841.bin`, its `.bin.moves.txt` and
`.events.json`, analyzed by `analyze_throw_v2.py` in the local evidence directory.

7,467 recorded snapshots, 19 practice resets (20 independent sequences), no
forward gaps within sequences. Attacker is character 6/P1 and defender 15/P2.
There are 21 side throws, plus one incidental frontal throw (2095). All select
the short branch (`attacker +0x1C08 = 1`).

| Victim move | Required break | Selected animation window | Successful | Completed unbroken | Interrupted by reset |
| --- | --- | --- | ---: | ---: | ---: |
| 2105 | 2 | 1–5 | 5 | 6 | 0 |
| 2103 | 1 | 1–5 | 5 | 4 | 1 |

All unbroken side events cross beyond animation frame 5 at **+7 game frames**,
at the same observed opening rate `3121 / 4096`. The ten completed failures
deal damage (40 for move 2105, 43 for 2103). The interrupted side event has
enough samples to establish the window crossing but is excluded from counts of
completed outcomes. The incidental frontal event is also reset-interrupted.

All ten successful side breaks have the correct punch edge, acceptance one
snapshot later (`+0x6E8` and `+0x2359`), and the declared break-animation target
one snapshot after acceptance. None deal damage.

### Adjacent boundary trials

Offsets are zero-based from the first connected snapshot:

| Segment | Start frame | Move | Correct input | Acceptance | Result |
| ---: | ---: | ---: | --- | --- | --- |
| 4 | 179 | 2105 | 2 at +5 | +6 | Break target 287 at +7 |
| 2 | 179 | 2105 | 2 at +6 | none | Unbroken, 40 damage |
| 7 | 179 | 2105 | 2 at +6 | none | Unbroken, 40 damage |

The two rejected +6 inputs arrive while the sampled integer animation timer
still equals 5. As in the earlier Counter Hit frontal trial, input acceptance
is processed on a subsequent snapshot, after that animation-domain window has
closed. Keep the measured input and acceptance timestamps separate; displaying
7f does not mean every raw input offset below 7 is necessarily accepted.

The **normal-side 14f / Counter Hit-side 7f** distinction is now supported by
metadata, the actual branch flag, observed timer crossings, and manual input
results for these two moves. The supplied claim that side windows are always
fixed at 14f is not applicable to these Counter Hit cases. Do not extrapolate
this result to all characters or special throws without compatible metadata.

Startup is 12f in these trials, so the nominal startup-plus-window display is
19f for these Counter Hit side throws, compared with 26f for their normal side
versions. This sum is not itself a measured player reaction time.

Automated assertions passed for all segment boundaries and event counts,
selection of 1–5 windows, all ten successful timing/target/damage checks, all
eleven unbroken-window crossings, damage in the ten completed failures, and the
adjacent +5-success / +6-failure trials. Production integration is unchanged.

## Sixth capture: King chain throws and pre-held input

Evidence: `throw_chain_20261104_180858.bin`, its `.bin.moves.txt`,
`.chain.events.json`, and `analyze_chain.py` in the local evidence directory.
Both character IDs are 2 (King), P1 attacking P2. The user did not supply the
chain's input notation or name, so identification below uses recorded move IDs.

6,061 snapshots, three practice resets (four independent sequences), no forward
gaps within sequences. Six initial grabs (victim 2476) all continue unbroken;
the normal entry window remains 1–15 animation frames / 20 game frames.

### Discovery: chain tags and requirements differ

Subsequent links use raw tag `0x90000018`, which the current production enum
normalization drops to NONE. The original recorder also filtered it out of its
metadata dump. The binary capture still contains all the player snapshots.
Additional cancel records were recovered read-only from the recorded current
move pointers while the same moveset remained loaded, and saved as
`throw_chain_20261104_180858.extra.moves.txt`. The recorder now includes this
observed tag for future metadata captures.

The generic analyzer recognizes only fresh entries into the three earlier
tags; it is insufficient for chains. `analyze_chain.py` instead splits on each
observed link move transition and on practice reset. Consecutive links can
retain the exact same requirement tag.

All four observed breakable link types have requirement **`0x87`, parameter 5**,
not the simple `0x83` requirement used by ordinary break-1 throws. Dispatch
RVA `0x1877776` passes the requirement parameter as the fourth argument to the
same break-check routine. In the observed cases this supports held button 1.
Do not extrapolate the meanings of other parameters without inspecting them.

| Victim link | Instances | Window in move data | Animation rate | Transition frame | Break target | Breaks |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| 2482 | 5 | 1–15 | 1.0 | 95 | 2506 | 2 |
| 2486 | 3 | 1–15 | 1.0 | 90 | 2508 | 0 |
| 2494 | 3 | 1–15 | 1.0 | 110 | 2512 | 1 |
| 2500 | 2 | 1–15 | 1.0 | 70 | 2516 | 0 |

Moves 2480 and 2492 are intervening stages without these break cancel entries.
They must not be given a fabricated window merely because they are in a chain.

The four link definitions have **15-frame windows**, and their integer
animation timers exceed 15 at +15 game frames. Unlike the initial grab, these
links run at one animation frame per game frame. The supplied blanket 9f claim
does not match this chain's metadata/timing. Boundary inputs exactly around the
last accepted frame were not captured, so the last raw-input sampling offset
is not independently established here.

### Manual versus held input

Zero-based offsets from the start of the link:

| Segment | Start frame | Link | Input | Accepted | Break animation |
| ---: | ---: | ---: | --- | ---: | ---: |
| 3 | 341 | 2482 | Fresh 1 at +1 | +2 | +95, target 2506 |
| 3 | 828 | 2482 | 1 already held for 139 prior snapshots | +1 | +95, target 2506 |
| 3 | 1742 | 2494 | Fresh 1 at +1 | +2 | +110, target 2512 |

The held-input case has no new punch edge during the entire link. Both
`+0x6E8` and `+0x2359` rise at +1, and bit `0x800` of `+0x6F8` also sets.
It reaches the declared break target with no additional damage during the link.
This directly confirms advance holding for this link/button. The UI should
identify it as **HELD**, not invent a fresh reaction or a negative reaction time.

All three successful link instances have a fresh acceptance signal and no
additional link damage, but the break animation is delayed until the cancel's
transition frame. Ten other link instances continue and deal additional damage.
The final sequence contains first inputs at +104 (2482), +45 (2486), and +27
(2500); none are accepted. These are clearly late trials, not fine boundary
tests. No unsupported requirement or untested link should inherit a universal
9f or 15f constant.

Automated checks passed for continuity/reset segmentation, six initial grabs,
13 breakable link instances, all 15f crossings and rates, three acceptance and
target transitions, the 139-frame pre-held case, and damage/continuation for
the ten unsuccessful links. Production integration remains unchanged.

Next integration must track chain links by move transitions, support their
parameterized requirements and held-input timing, and retain separate `INPUT`,
`BREAK ACCEPTED`, and animation-transition timestamps.

## Seventh capture: King ground throw

Evidence: `throw_ground_20261104_181832.bin`, its `.bin.moves.txt` and
`.events.json`, analyzed by `analyze_throw_v2.py` in the local evidence directory.
The recorder completed automatically with 10,067 snapshots, zero reported
misses/errors, and seven practice resets (eight independent sequences).

There are 16 ground-throw events with victim move **2264**, attacker connected
move 2263, King versus King, P2 defending. Two incidental move-2238 events are
excluded. The command/name was not supplied, so these findings identify the
tested ground throw by move ID.

Both metadata branches (`1CC:0,83:0` and `1CD:0,83:0`) specify button **1**,
animation window **1–5**, transition frame 1, and break target **2276**.
All recorded ground events have CH flag 0. Their opening animation rate is
`3121 / 4096`; in every unbroken event the integer timer first exceeds 5 at
**+7 game frames**. Startup is **18f**, giving a nominal startup-plus-window
sum of **25f**, not a measured reaction time. The identical metadata branches
do not constitute a live Counter Hit test.

Outcomes: **three successful breaks, twelve completed failures, and one
reset-interrupted event**. Each completed failure deals 25 damage.

Offsets below are zero-based from the first connected snapshot:

| Segment | Start frame | Input | Accepted | Break-animation transition |
| ---: | ---: | --- | ---: | ---: |
| 2 | 1197 | 1 at +2 | +3 | +4 |
| 4 | 673 | 1 at +1 | +2 | +3 |
| 6 | 149 | 1 at +3 | +4 | +5 |

All three successes raise fresh `+0x6E8` and `+0x2359` signals one snapshot
after input, reach target 2276 one snapshot after acceptance, and deal no
damage. Thus input, acceptance, and animation remain separate timestamps.

### Late and pre-held input

At segment 3/frame 149, correct button 1 first appears at **+6** and its input
latch is processed at +7, but no break is accepted. Recovery is interrupted
by a reset after 97 samples; 25 damage has already occurred. This is evidence
of a rejected +6 input, not of a fully observed recovery or an exact last
successful input offset.

At segment 4/frame 411, button 1 was held for **20 contiguous snapshots before
connection** and remained held across early window snapshots. There is no
fresh edge or acceptance, and the throw deals 25 damage. Unlike the tested
chain link, advance holding did not break this ground throw in this trial.
The defender entered from move 105 rather than the usual grounded move 32775;
one trial with this entry state does not establish a universal held-input rule.
The button was not held for the entire connected animation.

Other failed attempts include correct 1 at +9/+17 and wrong button 2 at +2.
An input of 1+2 at +7 is already late and cannot establish whether 1+2 would
be rejected inside the valid window.

Automated assertions passed for snapshot/reset/event counts, selected button
and bounds, startup and CH flag, all three acceptance/transition delays and
targets, all unbroken 7f crossings, completed-failure damage, the rejected +6
input, and the pre-held failure. These results cover this recorded move only.

## Production integration

The timeline now consumes native-width snapshot fields and bounded cancel
metadata through `ThrowMetadata.h` / `GameReader.h`. It selects CH branches,
combines compatible alternate buttons, tracks each chain move separately,
and publishes distinct input, acceptance, and animation timestamps. Parameter-5
chain holding is displayed as `HELD`. Unknown requirements/windows remain `?`.

`replay_throw_captures.py` feeds recorded snapshots through the production C++
tracker. Six version-2 captures (frontal normal/CH, initial side, manual side,
CH side, chain, and ground) pass **226 event checks** across both orientations
(113 recorded events). The original version-1 capture lacks required fields;
normal frontal coverage instead comes from the second capture's normal trials.
Immediate auto-breaks without an observed animation delta retain an unknown
game-frame window. Live rendering/reader verification remains an in-game step.

## Eighth capture: Dragunov tackle (2026-11-05)

Evidence: `throw_tackle_20261105_125001.bin`, adjacent metadata/events and
`analyze_tackle.py` in the local evidence directory. 7,995 consecutive snapshots,
no resets, misses or errors; both characters are 15 (Dragunov), P2 defending.
The preceding `124557` attempt contains no snapshots: CE was attached to an old
process. It is not evidence. Reattachment and a recorder preflight fixed this.

There are 15 tackle entries, victim moves 2079/2091, startup 26f, all normal
branch. Both definitions contain requirement `87:3` (observed break 1+2):

| Branch | Command | Animation window | Transition | Target |
| --- | --- | --- | ---: | ---: |
| Normal (`1CD`) entry | 0 | 1–4 | 10 | 2082 |
| Normal (`1CD`) reversal | 4000000300000000 | 18–19 | 20 | 32789 |
| CH (`1CC`) entry, not exercised | 0 | 1–2 | 10 | 2082 |
| CH (`1CC`) reversal, not exercised | 4000000300000000 | 18–18 | 20 | 32789 |

The first window closes at +6 game frames. Animation initially advances by
3121/4096, but changes to rate 1 near the later window: timer 17 at +21,
18 at +22, 19 at +23, 20 at +24. Thus the second observed window is **2f**,
not an extrapolation using the initial slowdown. The gap is not breakable.

Three entry breaks: frame12192 input+2/accept+3, frame13579 input+3/accept+4,
frame14030 input+0/accept+1. All transition to 2082 at +13. Two reversals:
frame13094 and frame17949, acceptance at +23, target32789 at +25. Both have
an input edge at +23, alongside earlier attempts; do not impose the ordinary
input+1 processing rule on this command-gated branch. Ten other tackles pass
both windows without accepted entry/reversal. Do not require damage here:
the recorded damage counter does not increase during the tackle entries.

### Ground actions are not the tackle break

Victim move2165 follows six entries: four instances transition to2080 after
one snapshot, two continue for160 snapshots to101. The four immediate exits
raise byte `+0x6E8` at the *new move's* start, with no fresh notification-byte
rise. Two have no preceding punch input at all. The user subsequently confirmed
that auto-break/automatic defense was disabled: in the no-input cases the
attacker simply did not press a follow-up after the tackle. This was initially
treated as an explanation of those exits, but the later explicit no-follow-up
control (eleventh capture below) follows a different route. The immediate
ground-stage exits therefore remain unclassified; do not assign a defender
reaction time or BROKEN outcome solely from that explanation or byte.
The byte used for acceptance in verified throw windows is therefore not proof
of acceptance in this ground-stage context.
The metadata includes `B3:0`, `1AF:2,840D:1`, and `1AF:2,84:0` alternatives,
all at animation1. These conditional paths require separate research;
one-frame metadata alone does not establish a one-frame manual input window.
Moves2163/2167 also appear on P1 after reversals, with corresponding conditional
requirements; the victim can change sides during this sequence.

### Fix and verification

The original parser rejected parameter3, all nonzero commands and windows
starting after1. The tracker also discarded disjoint timings. It now accepts
the verified parameter3/command pattern, tracks the two windows separately,
and retains PENDING through the gap. Buttons stay hidden and the card expiry
does not start until a break succeeds or the final window closes. Ground-stage
acceptance cannot be attributed to the old tackle move. The card is labelled
TACKLE to distinguish its outcome from subsequent ground defense.

The C++ replay passes 30 tackle event checks in both orientations, including
all five true breaks, the two observed spans, no failure/reveal through the gap,
and no acceptance leakage at ground transitions. All 226 earlier event checks
also pass (256 total). Conditional ground-defense integration remains pending
a separately labelled manual capture; no button/reaction is guessed for it.

## Ninth capture: Dragunov ground continuation (2026-11-05)

Evidence: `throw_followup_20261105_130501.bin`, adjacent move metadata,
`.followup.events.json`, and `analyze_followup.py` in the local evidence folder.
9,057 snapshots, one forward gap from game frame71582 to71591 (eight missing
frames), no resets or read errors. The analyzer now splits at forward gaps as
well as resets. All 17 evaluated attempts are continuous; the gap occurs between
them and is excluded from timing analysis.

P1 and P2 are Dragunov. The recorded victim continuation is **2167**, with
`1AF:3,85:0` and `1AF:3,840D:1` alternatives and a separate `B3:0` route,
animation1–1, target2080. Initial tackle variants are2079,2089,2091.

- Six continuations last244 snapshots, deal45 damage, and transition to102.
- Eleven instances transition to2080 after one snapshot, with no damage;
  byte+6E8 is1 at entry. Eight have defender1+2 edges in the preceding tackle
  or at the transition; three have none in that interval.
- At frame71469, the sole preceding1+2 edge is at -12; the button has already
  been released before the transition. At72583,1+2 was pressed at -39 and is
  still held. Other exits include an edge at0 and recent edges at-1/-2.
- No fresh punch edge in the preceding tackle at69977,70335,70693. These
  remain unclassified exits, not evidence of auto-break or manual reaction.
  The user has confirmed automatic defense is off and explained that endings
  without a follow-up occurred in the preceding recording. For this recording,
  the user subsequently identified the continuation input as1+2 and said they
  probably did not perform a no-continuation control. Therefore none of these
  three exits may be labelled "no continuation" on the basis of that earlier
  explanation. Their cause remains unresolved; lack of a fresh punch edge in
  the inspected interval alone does not establish absence of buffered defense.

The continuation's1–1 cancel interval is an evaluation/transition interval,
not an established one-frame reaction window. Input can be retained from the
preceding tackle; the tracker must examine that interval before assigning
reaction/HELD/BUFFERED labels. The exact manual input window and conditional
requirements remain unresolved. Do not turn every2167->2080 transition or
+6E8 rise into BROKEN, or guess a reaction for the three no-edge exits.

Assertions passed for capture/gap/attempt counts, uninterrupted attempts,
all six damage continuations, all eleven target transitions, the three no-edge
exits, and the released -12 / held -39 examples. This is research evidence;
conditional ground-defense production support is still pending.

## Tenth capture: continuation through button1

Evidence: `throw_followup1_20261105_131725.bin`, adjacent move metadata,
`.followup.events.json`, and `analyze_followup1.py` in the local evidence folder.
6,918 consecutive snapshots, no resets/gaps/errors. The requested protocol was
to execute continuation1 every time, without a no-continuation control.
Both characters are Dragunov; P2's continuation move is2163.

13 attempts: **seven input-backed exits to2080**, each after one snapshot with
byte+6E8 set and no damage, and **six damaging continuations**, each dealing35.
Three failures have no preceding/early defender punch edges. The other three
first press1 at +21,+29,+22 relative to the ground-stage entry and fail.

| Ground-stage start frame | Defender1 edge relative to stage | Input at stage start | Outcome |
| ---: | ---: | --- | --- |
| 82720 | -12 | released | Exit2080 |
| 83102 | -16 | released | Exit2080 |
| 83484 | -2 | held | Exit2080 |
| 83866 | -7 | released | Exit2080 |
| 85394 | -41 | held | Exit2080 |
| 85776 | -52 | held | Exit2080 |
| 86158 | -37 | held | Exit2080 |

The released cases retain input-latch0x22 before the stage changes, even though
raw input is0. Long holding and previously released input both precede successful
exits. This supports buffered defense during the preceding tackle rather than
a fresh one-frame reaction to the2163 animation. All seven successes have
observed defender1 input; unlike the preceding1+2 recording there are no
unexplained no-input exits in this set.

One entry, frame85726 preceding the -52 held case, has CH selector1 in memory;
the other twelve have0. Keep it distinct despite the requested normal setting.
These trials do not establish the first/last eligible buffer frame, whether
all wrong buttons are rejected, or the full semantics of1AF/B3/840D.
Do not display the negative offsets as reaction times. The ground-stage
transition is the evaluation point, not a proven start of the input window.

Assertions passed for continuity, all13 outcomes,35 damage in every failure,
three late trials, three released-buffer cases, held durations2/37/41/52,
and the single CH-tagged entry. No new production timing constant is inferred.

## Eleventh capture: explicit no-follow-up control

Evidence: `throw_no_followup_20261105_132334.bin`, adjacent move metadata,
`.control.events.json`, and `analyze_no_followup.py` in the local evidence folder.
6,497 snapshots, zero forward gaps/read errors, one practice reset. There are
11 completed tackle entries and a twelfth entry on the final captured snapshot.
The last entry is truncated and excluded; the generic analyzer now handles a
last-snapshot entry without reading past the capture or inventing a rate.

All11 completed entries have victim move2079, normal branch, and **transition
directly to99 at +85**, with no damage increase and byte+6E8 remaining0. Neither
ground continuation2163/2165/2167 nor target2080 occurs along these entry paths.
No attacker punch edge follows connection. Ten trials have no defender punch
edge; one has late defender inputs from+33 onward and follows the same route.

This supplies a reproducible distinction between the tested no-follow-up
ending and the earlier ground-stage->2080 exits. It supersedes the tentative
interpretation that those earlier exits could simply be labelled "attacker
did not continue". The three unexplained exits in the1+2 recording still need
their conditional path/input history resolved. This control does not establish
a universal no-follow-up move ID for other tackle variants or characters.

All assertions passed for count/reset segmentation, the11 direct transitions,
duration85, no damage/acceptance/ground stage, attacker input, the late-defender
control and exclusion of the final incomplete entry. A failed entry break can
still be displayed as TACKLE FAILED; no separate ground-defense event should
be fabricated when the continuation is absent.

## Twelfth capture: single input and wrong-button trials

Evidence: `throw_buffer_boundary_20261105_132736.bin`, adjacent metadata,
`.buffer.events.json`, and `analyze_buffer_boundary.py` in the local evidence
directory. 10,249 continuous snapshots, no gaps/resets/errors; all12 tackle
entries select the normal branch. One ends directly at99 without continuation;
the other11 enter victim2163 (continuation1).

Eight2163 instances exit to2080 after one snapshot with no damage and byte+6E8
set. Seven have defender1 input; the remaining exit at frame12097 has no punch
input during its preceding tackle (the last raw punch edge is871 frames earlier)
and remains unresolved. Do not count it as a demonstrated manual break.

Five successful trials have exactly one press/release pair before the stage:

| Stage start | Press1 offset | Release offset |
| ---: | ---: | ---: |
| 13217 | -12 | -7 |
| 13704 | -35 | -32 |
| 14700 | -30 | -26 |
| 15187 | -9 | -4 |
| 15696 | -24 | -16 |

At16183, a fresh1 at-1 also succeeds. At12730, repeated1 inputs precede exit;
that trial is not a single-input boundary measurement. In particular, the
-35 press was released32 frames before evaluation: this is retained input,
not merely a button still held when the continuation starts.

Three trials using only wrong button2 fail and each deals35 damage. Inputs:
frame16670 at-16 and-4; frame17157 at0; frame17644 at-46, held through entry.
The early/held wrong-button failures demonstrate button specificity; the0
trial alone cannot separate wrong-button rejection from lateness.

Assertions passed for continuity, all11 continuation outcomes, all five
single released inputs, the-1 success, three wrong-button damage outcomes,
and the separate no-follow-up event. These are successful sample points,
**not established first/last eligible frames**: no adjacent correct-button
failure brackets either boundary. The unexplained no-input exit still requires
conditional-path investigation before ground-stage outcome integration.

## Ground-condition resolution: retained selection across attempts

Read-only disassembly plus the existing captures resolve the previously
unexplained exits. Evidence files: `tackle_dispatch_start.txt`,
`tackle_dispatch_prologue.txt`, `tackle_b3_disasm.txt`,
`check_ground_buffer.py`, and `ground_buffer_checks.json` in the evidence folder.

In the requirement evaluator, r14 is the current player and r13 the other
player. RVA1875A88 forms `other + 0x1500` and stores it in `[rsp+48]`.
Requirement1AF at RVA187B8B2 loads that pointer and compares its DWORD+0x64
against the requirement parameter. Thus **1AF:N tests other-player+0x1564==N**.
For a victim ground stage this field belongs to the attacker, although its
observed changes encode defender input:1,2,3 for1,2,1+2 respectively.

The alternate sequence `1AF:N,840D:1` matters: the current evaluator checks
the requirement ID's0x8000 bit at entry and returns true for that encoded
requirement. It does not require the simple83/84/85 break-check branch to run.
This explains why a ground exit can retain the idle tag and omit the usual
notification rise. Do not infer a manual fresh input from those signals.

Across **49 ground-stage instances in four captures**, every2080 exit and
entry+6E8 value matches whether other-player+0x1564 equals the metadata's
required parameter. This includes the1,2,and1+2 continuations and both victim
sides. All comparisons passed. B3 is an additional route: its handler calls
two context-dependent helpers and can also set+6E8. These recordings do not
contain all its inputs (including+0x3A50/+0x14F8), so the correlation is not
proof that no alternate route could trigger in a different trial.

### Previously unexplained no-input exits

- In `throw_buffer_boundary`, attacker+0x1564 changes0->1 at frame11226,
  when defender1 is pressed during the preceding tackle. The next recorded
  variant2091 retains that value; ground2163 at12097 exits with the same1,
  **871 frames after it was recorded**, without a new defender punch.
- In `throw_followup`, the value changes0->3 at68541. It remains3 through the
  exits at69977,70335,70693 (1436,1794,2152 frames later).
- The observed2079 entries reset this field to0; the intervening2091 entries
  in these sequences do not. Do not assume a new tackle always clears it, or
  generalize reset behavior to untested moves.

These results supersede the unresolved-exit classifications above. A stored
selection carried across attempts supplies the matching ground condition;
neither automatic defense nor an omitted continuation is needed to explain
these cases. The game performs the exit, but it is **not a new manual reaction**.

Production integration should read the stored selector, track its origin and
reset epoch separately from the current throw, and identify the ground stage
without depending on a throw tag. A current-attempt retained input can be
BUFFERED or HELD; an older/unobserved origin must have no fabricated reaction
time (e.g. CARRIED). Window duration remains unknown until its actual collection
interval is established. Conditional ground-stage support is not yet added to
the production tracker; this investigation changes the evidence/classification.

### Ground integration after the condition investigation

The production reader now reads optional `ground_buffer=0x1564` and examines
changed move pointers even with idle throw tags. The metadata decoder accepts
only the verified1AF:N,840D:1 single-frame conditional form for ground stages;
it does not treat arbitrary high-bit requirements as supported throw metadata.

The tracker keeps per-side selector provenance across attempts, resets it at
discontinuities/character changes/unavailable fields, and starts a separate
GROUND event on the conditional move. A fresh acceptance plus matching selector
is confirmed as BROKEN only on the declared target transition. Damage supplies
FAILED. A target exit with an unmatched selector remains UNCONFIRMED, because
the alternate B3 context has not been established. It is not labelled a manual
break. Buffered/held/current versus carried/unknown origins never fabricate
negative or zero reaction times; the ground reaction window remains unknown.

The new ground replay passes98 outcome/provenance checks (49 instances, both
orientations), including the four formerly unexplained retained-selector exits.
The no-follow-up control creates no ground cards. All256 prior throw/tackle
event checks still pass. Synthetic tests cover the metadata form, target exit
with mismatched selector, initially unknown buffer origin and gap handling.
