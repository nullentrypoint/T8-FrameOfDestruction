# Tekken 8 3.03.02 — partial live checks

Practice, Jin (P1) vs Kazuya (P2). Executable SHA-256:
`B1B39950FB7C8CFFA60FD8F51D4BD087D561BB78258E530106F884BB69A6456E`.
PE timestamp `0x6af749a0`, image size `0xa79b000`.

Using the 3.03.01 INI, the player chain resolved in practice. All 32 slots
contained character IDs 6 and 8 at `+0x168`, with P2 at `+0xe220` and stride
`0x1c440`. Frame counters at `+0x1824` spanned 2247–2278 and later 3857–3888.

| Scenario | Live observation |
| --- | --- |
| Jin jab on block | Startup 10, outcome 1, stun 256, damage delta 0. At frame 8793: `(18-10)-(27-20) = +1`, matching published and displayed FA. |
| Jab on hit | Outcome 12, stun 256, damage delta 5. At frame 13185: `(25-10)-(27-20) = +8`, matching published and displayed FA. |
| Jab in counter-hit trial | Outcome 10, stun 256, damage delta 6. Published and displayed FA +8. |
| Jin f+4~f on block | Move 1891 → 1897. At frame 21090 both timers were 2; defender recovery 25 minus attacker actionable endpoint 26 gives displayed −1. Raw FA was 0. |
| Jin 1+3, no break | Moves 2102/2103, damage delta 35, defender acceptance byte 0. Defender move metadata listed buttons 1 or 2, target 2104, normal window 1–15 and counter window 1–10 in animation units. |
| Same throw, automatic break | Defender acceptance byte 1 during 2103, then move 2104; damage unchanged. Attacker counter byte 0. |
| Same throw, counter hit and automatic break | At frame 31644 attacker counter byte 1, defender acceptance byte 1, animation 1.0. Transition to 2104 with no additional damage. This does not measure the last accepted input. |

After menu selection of P2, the user controlled Jin on the right. At frame 3034
all 32 slots held Kazuya (8) first and Jin (6) second. `opponent_side` read
0 and raw `facing` read 0 before any physical crossover. After the crossover
(frame 6433), raw `facing` was 1; `opponent_side` and both character IDs were unchanged.
After a practice position reset, Jin was back on the right (frame 974): raw
`facing` returned to 0, `opponent_side` remained 0 and the slots still read 8/6.

With Jin in the second slot, the blocked jab (1596) contacted on frame 17923
(timer 10, receiver outcome 1). At 17933 the published FA and recovery
calculation both gave +1: `(18-10)-(27-20)`. The receiver's `+0x3620` XYZ
changed on contact and still held that value at frame 17973. Jin's `+0x29d0`
XYZ moved during the jab and matched the receiver's contact XYZ at 17924.

During a whiff starting at 21036, the attack hitbox moved across timers
9–11, but receiver outcome stayed 0 and the previous contact XYZ was unchanged
across 46 frames. At 23273 both configured camera matrices contained finite
doubles; the cached contact projected to NDC (-0.0870, 0.3025), clip W 346.729.
This was not a visual alignment check.

After a full restart (PID 28928 → 17368), the module base was unchanged and
the rollback buffer moved. Re-resolving from the module yielded frames
7090–7121 without gaps, IDs 8/6 in all 32 slots, side 0 and facing 0.
Both camera matrices were finite; projection [0,5,11] = [1.56969,2.79055,1]
and view[15] = 1.

With Dragunov in slot 2 against Kazuya, the tackle (2088/2089 attacker/defender)
transitioned to 2172/2173 and defender damage increased by 35. The defender
button field and both ground selectors stayed 0: a no-input control only.
On frames 3907 and 4522, defender input 0x400 coincided with the attacker's
`+0x15e4` changing from 0 to 2. On frame 4831, input 0x600 coincided with
0 to 3. The defender's selector remained 0. The attacker retained the value
after release and reset it on the next tackle entry (4511, 4825, 5171).
On frames 8155 and 8483, defender input 0x200 (button 1) coincided with
the attacker's selector changing from 0 to 1; the defender's stayed 0.

Fresh replay, Dragunov attacking from P2: move 1622 contacted on frame 5518
(timer 10, Kazuya outcome 1). The headless C++ replay emitted exactly one
event, `5520 +1`, in both normal and swapped orientations. At 5520 the
published value was 1 and `(18-2)-(27-12) = +1`, matching the in-game reference.
The next jab hit on frame 10141 (outcome 12, damage 0 → 5). Both replay
orientations retained the original block event and added exactly `10142 +8`.
At 10151 the published FA was 8 and `(25-10)-(27-20) = +8`.
With Counter Hit enabled, the next contact at 12400 gave outcome 10 and
damage 5 → 11. Both replay orientations added `12401 +8`; at that frame
`(25-1)-(27-11) = +8` even though the raw FA field was still 0.

Fight Lounge: the external production probe, using a temporary INI with only
the lounge PE timestamp changed to `0x6af749a0`, returned `ok` twice. The
second snapshot contained four platform-0 remote entries, all Steam candidates.
These reads do not establish peer identity, transport, or audio.
In a different game process (PID 3336), the same probe returned `not-in-lounge`.
This is an outside-lounge control, not a same-process exit/stale-roster test.

Player Match, single-member room in PID 3336: using a separate temporary INI
with only the custom-room PE timestamp changed, the production probe returned
`status=0 count=1 current=1` (exit 0). No remote membership change tested yet.
After local exit in the same PID: `status=1 count=0 current=0` (probe exit 4,
expected for non-Ok status). No stale participant was published.

Online match: the old identity destructor guard returned `unsupported-build`.
Both live objects had vtable RVA `0x85a3ca8`, whose first slot had changed from
RVA `0x5c287c0` to `0x5c28600`. With only this correction the production
PlayerIdentityReader twice returned `ok` with two records (roles 1 and 3);
both passed string rereads and had matching platform-ID copies.

Steam profiles: the runtime log reported `Unsupported Steam initializer layout`.
Read the three initializer pointers from their configured context objects.
User, networking and matchmaking now point to `0x5c7dd40`, `0x697c050` and
`0x697c020`. Each matched the instruction pattern and its expected interface
string; both import targets matched the named exports in the loaded steam_api64.dll.
Profile display after reloading the mod still needs an in-game check.

Visual alignment, replay coverage beyond these three jabs and Steam voice transport
are not established by these captures. Both repository room timestamp guards
now match the tested build; image size and RVAs are unchanged.
