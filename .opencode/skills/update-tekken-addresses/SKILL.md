---
name: update-tekken-addresses
description: Update memory_address.ini after a Tekken 8 patch: discover and validate frame, player, throw, camera, match voice, Fight Lounge and custom-room roster addresses using Cheat Engine MCP, verify captures with production readers/replay tests, and deploy the updated configuration.
---

# Updating Tekken 8 Addresses

Use this skill after a game update, when pointer chains stop working, when voice
identity reading reports `UnsupportedBuild`, or when asked to update
`memory_address.ini`. Work toward a validated configuration and identify checks
that require user actions or a second participant in an online session.

Paths prefixed with **repo:** are relative to the repository root. Other relative
paths are relative to this skill's directory.

## Read first

- **repo:** `AGENTS.md`, `config/memory_address.ini`.
- **repo:** `dll/TekkenBot/GameReader.h`, `dll/TekkenBot/GameSnapshot.h`,
  `dll/TekkenBot/MoveInfoEnums.h`.
- **repo:** `dll/VoiceChat/MemoryLayout.h`, `dll/VoiceChat/PlayerIdentityReader.cpp`,
  `dll/VoiceChat/SteamChannelProbe.cpp`, `dll/VoiceChat/SteamDiagnostics.cpp`.
- For room voice: **repo:** `dll/VoiceChat/LoungeLayout.h`,
  `dll/VoiceChat/LoungeReader.h`, `dll/VoiceChat/LoungeReader.cpp`,
  `dll/VoiceChat/CustomLobbyReader.h`, `dll/VoiceChat/LoungeVoice.h`.
- As needed: **repo:** `dll/tests/README.md`, `dll/tests/throw_timeline.md`,
  `dll/tests/throw_window_research.md`, `dll/tests/voice_3_03_01.md`.
- `references/ce-workflow.md`: CE discovery and capture techniques.
- `references/3.03.01-evidence.md`: evidence and pitfalls from a previous migration.
- `references/room-voice-addresses.md`: Fight Lounge/custom-room chains, lifetime
  guards, bounded roster layouts, production probes and controlled checks.

Historical RVAs, structure sizes, move IDs, and frame advantages are not evidence
for a new build. Treat the current INI as a starting hypothesis and the reader
code as the specification of how the mod interprets that configuration.

## 1. Preparation

1. Establish the game version, mode, characters, and selected P1/P2 side.
   Practice mode with two different characters is a useful starting point.
2. Inspect `git status` and existing changes. Preserve the original INI and
   record the executable's build identifier/hash. Preserve unrelated code changes.
3. Attach CE MCP to the current `Polaris-Win64-Shipping.exe`. Verify the PID,
   module base, and readable PE header. Reattach after a process restart;
   previously observed heap addresses are no longer valid.
4. Use memory reads for discovery. Have the user prepare controlled in-game
   scenarios. Avoid enabling research hooks when reading alone is sufficient.
5. Maintain an evidence table: section/key, old offset, candidate, type/width,
   P1/P2 base, observation, and validation status. Store captures outside the
   repository; keep a concise evidence report without real Steam IDs or large
   dumps in the project.
   Use local/remote placeholders for nicknames, Steam IDs, Tekken IDs, game-user
   IDs and custom-room keys. Scan source, tests, reports and skill references for
   captured identities before finishing. Synthetic fixtures must be clearly
   marked; keep raw diagnostic logs outside the repository.

## 2. Pointer chains and rollback storage

Work in this order: `player_data_pointer_offset`, frame stride, P2 offset,
then `frame_count`.

- Follow the reader's exact chain semantics: start at the executable base and
  read a QWORD at `pointer + offset` at every step, including the last one.
- Most ordinary fields use only the **first** offset on their INI line.
  Additional values are not automatic fallback addresses.
- `expected_module_address` has historically been unused by the active reader.
  Do not substitute it for the actual module base or store absolute heap
  addresses in the INI.
- If a root breaks, search the current PE data sections for pointers that pass
  the verified inner chain. Do not apply one discovered delta to every global
  or structure member.
- Confirm repeated blocks using character IDs, then check frame-counter
  sequences across all slots. The current reader assumes 32 slots; verify that
  assumption. A changed storage scheme may require reader changes.
- Validate block size and bounds for every configured field of both players,
  including pointers, bytes, XYZ vectors, and matrices where applicable.

## 3. Controlled field validation

Request one clear scenario, collect a bounded capture, and inspect it before
asking for the next action. Verify what actually happened: guarding may not
have been enabled, or the recorded input may represent a different move.

| Scenario | Fields to validate |
| --- | --- |
| Idle, movement, individual buttons | Character/move IDs, timer, input direction/buttons |
| Jab against continuous guard | Startup, recovery, states, block outcome, signed FA |
| The same jab on hit | Hit outcome, damage, recovery, signed FA |
| The same attack as Counter Hit | Outcome distinction and actual stun-field semantics |
| Stance transition, such as Jin f4~f | Actionable-frame publication, animation change, stance-adjusted FA |
| Ordinary throw without a break | Tag, current move pointer, float animation timer, damage |
| The same throw with automatic breaks | Fresh acceptance byte and transition to the metadata target |
| Counter Hit throw | Attacker counter byte compared with the normal-throw control |
| Tackle with a ground continuation and defender input | Ground selector for 1/2/1+2, reset and persistence |

Key checks:

- For ordinary contact, compare published FA with
  `(receiver.recovery - receiver.timer) - (attacker.recovery - attacker.timer)`.
  For a verified stance transition, use its published actionable endpoint.
- The FA field may be zero, preliminary, or stale. A single plausible value
  does not establish the final advantage.
- Compare observed semantics with current enum consumers. A generic stun flag
  is not a block/hit/counter classifier unless observations establish that.
- Read `ThrowPlayerData` using native widths: QWORD move pointer, float
  animation, byte acceptance/counter flags. Check remaining types in
  **repo:** `dll/TekkenBot/GameSnapshot.h`.
- Read throw metadata through the **defender's current move pointer**. It may
  reference the attacker's moveset. Verify the bounded cancel/requirement walk
  in **repo:** `dll/TekkenBot/ThrowMetadata.h`, terminators, buttons, and targets.
- A throw tag does not prove a successful break or the complete set of valid
  buttons. Metadata windows are in animation units, not necessarily game frames.
- The verified ground selector belongs to the attacker but encodes defender
  input. Constant zero values cannot validate its address; obtain positive
  changes and a control scenario.

## 4. Side selection, spatial data, and restart checks

1. Select P1 and then P2 through the menu. Verify `opponent_side`, character
   slots, and frame data while playing as P2.
2. Keep P2 selected, physically cross behind the opponent, then reset positions.
   `facing` must change and return while `opponent_side` stays unchanged.
3. Verify that contact XYZ updates on fresh hits/blocks and remains cached
   between contacts. Check attack-hitbox positions during active frames and whiffs.
4. Recover the FSceneViewState chain and validate both double 4x4 matrices and
   projection using **repo:** `dll/TekkenBot/SpatialPosition.h`. Capture matrices
   alongside player snapshots. Plausible NDC coordinates do not establish visual
   alignment with the impact point.
5. Repeat core checks after practice resets, character/stage changes, and a full
   process restart. The executable base may remain unchanged under ASLR; record
   the new PID and heap objects independently.

## 5. Voice-chat addresses

Update `VoiceIdentity`, `VoiceSteam`, `VoicePeerTrace`, and `VoiceProfileTrace`
in the same INI. The current loader requires all four sections, with no compiled
fallback RVAs. Keep the voice configuration complete.

Room support additionally loads `VoiceLounge`, `VoiceCustomLobby`, and
`SpatialCamera`. Their layouts use separate loaders and PE/vtable/class guards.
Do not treat a successful match identity read as validation of either room
roster. Update the PE timestamp and image size only after validating the
corresponding fixed-offset structure layout on that build.

- Identity: locate the container and verify its vector header, object vtable,
  destructor guard, four strings, and stable rereads through the production
  `PlayerIdentityReader`.
- Steam: locate initializer signatures, resolve RIP-relative interface names
  and import slots, and verify exports and owning modules. Find each context
  through its verified initializer pointer rather than a shared RVA delta.
- If several `SteamNetworking006` candidates exist, compare contexts and call
  sites. An uninitialized networking context in the menu is not proof of a
  wrong address.
- Retain instruction and RIP-target validation for trace fields. Structural
  changes require C++ updates rather than disabling signature checks.
  `steamclient_method_rva` is relative to `steamclient64.dll`; the other listed
  voice RVAs are relative to the executable.
- A valid local record and resolved imports do not establish remote identity,
  handshake, or audio operation. Verify those in an online session with a
  compatible second client.

### Fight Lounge and custom-room validation

Follow `references/room-voice-addresses.md` for the exact layouts and commands.

1. Fight Lounge: validate GWorld, game-mode class/active flags, subsystem/network
   object, opaque room key, local pawn, and sparse enemy map. Match candidate
   Steam IDs to user-confirmed participants, including a console-player control.
   Verify world-position doubles, sparse holes and entries beyond bit 31.
2. Custom room Player Match: find the manager root and follow
   its current-session shared pointer, then the bounded member map. Compare both
   Steam-ID strings and the node's user key against the participant record.
   The global cached room-ID pointer is not an active-session predicate.
3. Observe remote leave/rejoin (new allocations), local exit, a different room
   and a full process restart. Re-resolve every pointer from the new module base.
   Verify that a null current session suppresses membership even when old room
   strings remain readable.
4. Run the external production probes, not just Lua reads. Corrupt/unreadable/
   mutated snapshots must not publish partial rosters or retain stale audio.
5. Verify both endpoints with compatible builds: room agreement, nonce ACK,
   bidirectional audio, mute, removal and transitions into/out of a match.
   Check fighter-side HUD positions independently from the complete menu roster;
   host migration and spectator-viewer identity mapping need explicit scenarios.
6. Use `voice_debug on` on both clients (also Settings → System) for bounded
   `steam_diagnostics.log` snapshots. `ready=1` plus increasing receive counters
   validates transport, not audible quality. Failed attempts to unrelated room
   members do not imply failure of the connected test pair. Fight Lounge hearing
   is always unlimited within the same room (internal radius `-1`); the old
   distance command and saved values are no longer used. Avatar visibility
   range is a separate setting.

## 6. Changes, verification, and deployment

1. Update the INI with confirmed values, the tested version, and any remaining
   limitations. Remove stale alternatives for changed fields. Keep address-only
   voice changes in the INI and structural changes in the relevant reader or
   validator.
2. Convert fresh captures using offsets from the **updated INI**. Run production
   replay in both orientations and compare expected values/events. Output or an
   exit code of zero alone is not a result comparison.
3. Historical replays validate algorithms, not new memory addresses. Capture
   gaps preclude frame-perfect timing claims. Validate action-frame learning
   independently of potentially stale `actionable_frames.json` data, preserving
   the user's existing file.
4. For C++ changes, build `Release|x64` with MSVC v143 and the Windows SDK, then
   run the affected headless tests with assertions enabled. Consult the test
   documentation for commands and external prerequisites.
5. Locate the actual module-loading directory. Deploy the INI to
   `<DLL directory>/T8FrameOfDestruction/config/memory_address.ini`.
   The DLL build output is **repo:** `dll/x64/Release/dll.dll`; an ASI loader may
   load it as `plugins/T8FrameOfDestruction.asi`. Verify the real installation path.
6. Close the game before replacing its loaded library and preserve backups.
   When deploying a new DLL, copy the matching PDB and verify the installed file's
   hash. Restart the mod after editing the INI because it is loaded at startup.
7. Verify the resulting frame data, overlays, and any updated voice features in
   the game. Record numerical checks and visual/online checks separately.

## Report results

Summarize what changed, which scenarios actually passed, what remains unverified,
where files were installed (or that deployment was not performed), and the next
required action. Do not claim online audio or visual projection is verified
solely because the build and synthetic tests passed.
