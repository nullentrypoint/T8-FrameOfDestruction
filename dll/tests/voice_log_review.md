# Two-client voice log review

Reviewed two externally supplied `steam_diagnostics.log` files after the sample
clock / muted-peer keepalive fixes. Raw logs and captured identities are not
included in the repository. Participants below are anonymous Client A / Client B.

| Mode | Client A connected samples | Client B connected samples |
| --- | --- | --- |
| Custom room | 184 / 185 peer samples | 183 / 184 peer samples |
| Fight Lounge | 159 / 171 peer samples | 157 / 166 peer samples |

Both clients showed successful same-room connections and increasing send/receive
PCM counters in both modes. Neither logged HELLO API-send failures or PCM-send
failures for the tested peer. These counters do not measure packet loss or
prove acoustic quality. The user separately confirmed clear speech in both
directions.

The saved hearing radius was 1500. Connected Fight Lounge gain reached 0.002
on one client and 0 on the other while moving; finite distance filtering and
attenuation were active. The new default is unlimited same-room hearing (`-1`),
with explicit finite settings still supported.

One client logged up to 74 rejected envelopes in the Fight Lounge; the other
logged zero. Current counters aggregate envelope/authentication rejection and
cannot establish its specific cause. Non-connected unrelated lounge members
also appear in the diagnostic roster and are not evidence that the tested peer
failed to connect.

Source/report audit found captured nicknames, platform/game IDs and private
room keys in research documentation; these have been replaced with placeholders.
Runtime code has no hardcoded captured participants. Numeric IDs in headless
fixtures are synthetic inputs, not copied from the supplied logs.
