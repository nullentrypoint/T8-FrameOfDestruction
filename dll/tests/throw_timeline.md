# Throw break timeline

The F1 setting `Show Throw Break Timeline` is persisted independently of floating
frame-advantage hints. P1/P2 identify the defender in snapshot orientation.
All labels are English.

F1 settings include `Throw Card Style`: `Detailed` (620x136), `Compact`
(460x106 with timeline), and `Minimal` (360x68, result/window/input only).
Dimensions are scaled with the viewport. `Throw Card Duration` is 1–15 seconds
after the first result (default 5), including `Throw Card Fade-out` (0–3 seconds,
default 1, capped to duration). Zero fade hides the card immediately at expiry.
Later animation updates do not restart the timer. A new throw starts a new card.
All three settings persist in the user INI's `FrameAdvantageHints` section.
Pending cards remain visible; buttons show `--` until `BROKEN` or `FAILED`.
Unconfirmed outcomes never reveal the combination.

Verified parameter-3 tackles use a `TACKLE` card with two disjoint spans.
`BREAK WINDOW 6f/2f` reports their separate observed durations; the gap is not
breakable. A later window remains `?` until sampled, including after an early
break that prevents reaching it. Failure/button reveal and expiry wait until
the final window closes. `TOTAL` uses the last deadline, including the gap,
not just the sum of the two window durations. Ground follow-ups are separate
actions and cannot turn a failed tackle entry into a successful break.
Ground follow-ups with the verified `1AF:N,840D:1` predicate use a separate
`GROUND` card even when their raw tag is idle. The other player's configured
`ground_buffer` field selects1/2/1+2. A matching selector, fresh acceptance byte
and transition to the declared target are all required for `BROKEN`; damage
confirms `FAILED`. Other exit routes (including unverified `B3` contexts) remain
`UNCONFIRMED`. Buttons are revealed only after the confirmed result.

Ground input provenance is `BUFFERED` when a matching selector change was
observed with defender input during the current tackle, `HELD` if that input
is still down at evaluation, or `CARRIED` for an older/unknown origin. Unknown
origins include capture starts, gaps and unobserved selector updates. These
labels do not show a numeric reaction time. `BREAK ACCEPTED +0f` refers to the
ground-stage evaluation, not a zero-frame player reaction. Ground startup,
window duration and total remain `?`; no reaction bar is inferred from1–1
cancel metadata. The updated `ground_buffer = 0x1564` INI entry is required.

- `BREAK [1]`, `[2]`, `[1+2]`, or `[1 OR 2]` comes from bounded move-cancel
  metadata reads, selecting the Counter Hit branch from the attacker, and is
  revealed only once the break succeeds or the window closes unsuccessfully.
- `STARTUP` is the attack startup immediately before connection. Chain links
  show `?` rather than reusing the original grab's startup.
- `BREAK WINDOW` converts the metadata's animation-domain end to game frames
  using the first observed animation delta. An actual timer crossing replaces
  this estimate when available. This assumes the opening rate remains constant;
  early breaks do not independently measure the entire window. An immediate
  auto-break without two animation samples shows `?`.
- Yellow `INPUT +Nf` marks the first new punch edge, including wrong/late input.
  Offsets are zero-based from the first connected snapshot. `INPUT HELD` is
  used for verified parameter-5 chain requirements with 1 already held.
- The triangle and `BREAK ACCEPTED +Nf` mark a fresh acceptance-byte rise in
  the tracked window. `BROKEN` is displayed immediately on acceptance.
- `ANIMATION +Nf` reports the later transition to the declared break target.
  Before that, `TOTAL` shows startup plus window when both are available.
- `FAILED` means the supported window closed without acceptance, or the
  connected move ended with damage. Other endings are `UNCONFIRMED`.

The bar shows startup followed by the window, with a solid deadline when known.
Unknown windows use dashes. Late input can extend the axis beyond the deadline.
The delayed chain-break animation does not stretch the window visualization.

`ThrowPlayerData` in `config/memory_address.ini` is required. Deploy the updated
INI to `<DLL directory>/T8FrameOfDestruction/config/memory_address.ini` alongside
the new DLL. Fields use native widths and are read from the selected rollback
snapshot. Cancel/requirement pointers are read with checked `ReadProcessMemory`,
limited to 128 cancels and 64 requirements; results are cached per current move
pointer/character and cleared on frame reset. Unknown requirements are skipped.
Missing configuration leaves throw fields unavailable instead of guessing.

Consecutive chain links are tracked by move transitions even with identical
tags. Dropped frames/practice resets invalidate timing. The tag is only an
entry hint; it does not supply buttons, window length, or proof of success.
See `throw_window_research.md` for evidence and the limits of the tested cases.

## Verification

In an x64 developer prompt, from `dll/` (assertions must remain enabled):

```bat
cl /nologo /std:c++20 /EHsc /MD /I. tests\throw_timeline.cpp TekkenBot\MoveInfoEnums.cpp /Fo%TEMP%\ /Fe%TEMP%\t8_throw_timeline.exe
%TEMP%\t8_throw_timeline.exe
python tests\replay_throw_captures.py --exe "%TEMP%\t8_throw_timeline.exe" --out "%TEMP%\throw_replay" PATH\throw_ground_20261104_181832.bin
```

The synthetic tests cover bounded/failed metadata reads, alternate buttons,
stale acceptance, separate animation timing, discontinuities and both player
orientations. The replay converter requires local `T8TW0002` captures, adjacent
`.bin.moves.txt` and reference `.events.json`; chains additionally require
`.extra.moves.txt` and `.chain.events.json`. These evidence files are not bundled.
The oldest `T8TW0001` recording lacks required fields and is not supported.

`replay_ground_captures.py` uses the same executable and arguments for the
ground-continuation captures and the no-follow-up control. It checks target/
damage outcomes in both orientations, known buffered/held/carried examples,
unknown reaction/window values, and absence of fabricated ground events.

Replay checks compare buttons, startup, window duration, first input, acceptance,
outcome, held input and break-animation transition in both orientations.
Rendering and live metadata reads still require in-game verification with the
new DLL and INI (`F1` toggle, `F11` unload).
