# Lounge proximity voice research

## Live session, 2026-11-13

Read-only CE research in Europe 2 - 0001. Configuration targets 3.03.01;
the displayed game version has not been independently checked this session.
PID 4164, executable base `0x7FF6BDD50000`, PE timestamp `0x6ADB1AD5`,
SizeOfImage `0xA79B000`. These are session evidence, not persistent pointers.
No game-memory writes or hooks were used. Screenshots are outside the repository
under the session's approved temporary directory. No player IDs are recorded here.

## Observations

- Existing identity container at RVA `0x9BDDB18` contains one role-1 record
  despite remote lounge avatars being present. Its vtable/destructor guard matches.
- World candidate at RVA `0x9BBA200` leads to the following coherent UE objects.
- `World + 0x30` -> persistent level; `Level + 0x98` is a candidate actor
  TArray (pointer, int32 count, int32 capacity). Observed count 217, capacity 510.
- Local chain: `World + 0x1B8` -> GameInstance; `+0x38` -> LocalPlayers data;
  first pointer -> LocalPlayer; `+0x30` -> PlayerController;
  `+0x340` -> `BP_Player_Onl_C` (superclass `LobbyPlayer`, then `Character`).
- Actor `+0x1A0` -> component; component `+0x128/+0x130/+0x138` contains
  candidate position doubles. After user-confirmed movement, local values changed
  from `(2813.197, 970.531, 918.701)` to `(3579.301, 1265.036, 918.501)`.
  Root-relative versus world-space semantics still require explicit validation.
- Two similarly moving objects were identified by class as
  `PolarisSoundPositionActor` and `PlayerCameraManager`, not remote avatars.
- Seven actors have class `BP_AvatarEnemy_Onl_C`, superclass `LobbyEnemy`.
  The closest actor's first display name matches the nearby player visible in
  the screenshot. Position `(2953.699, 774.814, 922.500)` gives approximately
  794.80 units from the local candidate after movement; about 241.03 before.
  Units have not been calibrated to physical distance.

## Name and reflection discovery

- Candidate FName block-pointer table RVA: `0x99BC390`, found through the first
  block's `None` / `ByteProperty` entries and a pointer search in the executable.
- Entry address: `blocks[id >> 16] + 2 * (id & 0xFFFF)`.
  Header is uint16; length is `header >> 6`, wide flag is `header & 1`.
  Read the exact byte length; null-terminated reads can include following entries.
- UObject class pointer `+0x10`, name index `+0x18`;
  UStruct superclass `+0x40`, ChildProperties `+0x50`.
- Observed FField next `+0x20`, name `+0x28`; FProperty element size `+0x3C`,
  member offset `+0x4C`. Property type names were also inspected.
  These layouts are build-specific candidates, not a production reader yet.

## Reflected LobbyEnemy fields

| Member | Offset | Reflected type / observation |
| --- | --- | --- |
| `_platform012` | `0x2A0` | IntProperty; values 0, 1, 2 observed |
| `SpawnCosmosId` | `0x2A8` | Int64Property |
| `UserId` | `0x2D8` | NameProperty; decoded names have 18 characters |
| `IsJoinLobby` | `0x2E0` | BoolProperty; false for six present avatars, true for one; not a membership filter |
| `_displayNames2` | `0x380` | ArrayProperty; observed three FString-shaped elements, stride 16 |
| `_cosmosId3` | `0x390` | Int64Property; nonzero and equal to SpawnCosmosId for all seven |

Display element 0 matches the visible nearby player's name. Element 1 contains
17 decimal characters with a SteamID-like prefix for both platform-0 avatars;
it has length 1 for the other five. This suggests a Steam identity mapping, but
does not verify ownership, Steam availability, handshake, or audio transport.
The nearby player has platform value 1, so is not one of those Steam candidates.
Element 2 has observed FString length 15 including terminator; meaning unverified.

### User-selected Steam participant

The user confirmed the initially nearest player is on console and identified
another player as a Steam participant. A subsequent read found that exact display
name on actor index 68, class `BP_AvatarEnemy_Onl_C`, platform value 0, with a
17-digit SteamID-shaped second display element. No raw ID is recorded here.
At that read the remote position was `(1123.327, 583.009, 900.001)` and the
local position `(1398.842, 672.288, 909.946)`, distance approximately 289.790
game units. This correlates a user-confirmed Steam participant with platform 0;
it still does not validate the ID through Steam or establish mod availability.

After the user moved away, the same actor, root component and SteamID-shaped
string were found again. Remote coordinates were unchanged; local coordinates
became `(1755.123, 1071.216, 909.946)`. Distance increased from 289.790 to
798.505 units (798.443 horizontally). This validates identity continuity across
this local movement, not across respawn or reconnection.

Reflection identifies component `+0x128` as `RelativeLocation` (24 bytes).
`AttachParent` at `+0xB0` was null for both the local CapsuleComponent and the
remote SceneComponent, consistent with directly comparable world positions in
this observed state. Attached components still require transform handling.

### Membership research leads

The level contains `BP_LobbyGameMode_Onl_C`, derived from `LobbyGameMode`.
Reflected fields include `_isUseNetwork` at `+0x3EE`, `_isBeginPlayFinished` at
`+0x3EF`, and `_enemyManager` at `+0x488`. The latter points to an object of
class `LobbyEnemyManager`, whose reflected `_enemies` field is at `+0x28`
with element size 80. Subsequent reflection identifies it as MapProperty with
Int64Property keys and ObjectProperty values. In a later snapshot, the candidate
sparse-array header had 10 slots, capacity 24, inline allocation bits `0x3FF`,
and zero free slots. Reading 24-byte entries (key at +0, object at +8) yielded
10 `BP_AvatarEnemy_Onl_C` objects, all present in the current level actor array.
Every key equalled its object's `_cosmosId3`; the selected Steam participant
was present. This is a promising direct participant index. Sparse allocation
bits/free slots, concurrent mutation, and removal semantics still need handling
and validation; do not assume all indices below the array length are occupied.
The blueprint's `SpawnEnemies` array at `+0x530` is empty despite live remote
avatars; it is not currently a usable participant list.

`0_LobbyRootOnl_C` exposes `LoadingSubLevelID` at `+0x2A8`, `sublevels` at
`+0x308`, and `Lobby Game Mode` at `+0x3D0`. These are research leads only;
none has been established as a server, lounge, or zone identifier.

### User-confirmed zone transition

After the user moved to another zone within the same lounge:

- World, persistent level, local pawn, game mode and enemy manager pointers
  remained unchanged. Local coordinates became `(3621.553, 10250.889, 76.501)`;
  the local root still had no attachment parent.
- The selected Steam participant's actor remained present and its identity
  string was unchanged. Its position was `(1311.024, 643.286, 910.001)`, giving
  distance 9916.617 units. The participant had also moved since the prior sample.
- Actor array count was 139; eight actors matched the remote avatar class.
  The manager map still had 10 sparse slots, with inline allocation bits `0x2FB`
  and two free slots. Skipping unset bits (slots 2 and 8) yielded exactly eight
  valid remote avatars, all in the actor array with matching Cosmos keys.
  This validates the candidate inline allocation bitmap for this small map.
  Departure timing was not captured, so the two removals cannot be attributed
  specifically to the local zone transition.
- `LoadingSubLevelID` read 9, but no pre-transition value was recorded;
  neither its change nor its meaning as the current zone is established.
- Local blueprint `BP_Lobby_AreaComponent` is at `+0x788`; the remote one
  remains at `+0x3A8`. Both point to `BP_Lobby_AreaComponent_C`.
  `_lobbyPlayerStatus` points to `LobbyPlayerStatus`. No useful reflected
  data fields were exposed in the inspected classes.

The observed participant list spans zones; proximity filtering must not assume
that presence in the map means the participant is nearby. Lounge changes and
process restart remain untested. No voice packets have been sent.

### User-confirmed region/lounge change: Europe 1

After switching from Europe 2 to Europe 1, world, persistent level, local pawn,
game mode and enemy manager all changed. GameInstance remained unchanged.
The new actor array contained 1016 objects, including 58 remote avatars.
Neither the old selected participant's actor pointer nor its Steam identity
was present among the new remote avatars. Local coordinates were approximately
`(-145.003, -1695.000, 900.001)`.

The new manager map had 60 sparse slots, capacity 62, two free slots, and an
allocation bitmap with 60 bits in use and capacity 128. Reading the four inline
uint32 bitmap words at map `+0x10` and skipping free slots yielded 58 valid
avatars. All 58 were in the level actor array and all 58 map keys matched their
avatar's `_cosmosId3`. Platform values: 13 at 0, 44 at 1, one at 2.
This validates crossing the first 32-bit bitmap word; dynamically allocated
bitmaps beyond inline capacity remain untested.

World reflection confirms `PersistentLevel +0x30`, `AuthorityGameMode +0x150`,
and `OwningGameInstance +0x1B8`. The AuthorityGameMode pointer matched the game
mode discovered by enumerating actors. This supplies a direct candidate chain:
module + `0x9BBA200` -> World; World + `0x150` -> LobbyGameMode;
game mode + `0x488` -> LobbyEnemyManager; map is inline at manager + `0x28`.
Class and container validation are still required before traversing the chain.

`LoadingSubLevelID` again read 9, so it does not distinguish these two lounges.
No stable server/region/lounge key has been identified. World replacement is
an observed local lifecycle signal, not a globally comparable session ID or
proof that every possible lounge change recreates the world.

### Return to Europe 2

The user returned to Europe 2. The direct World -> AuthorityGameMode ->
LobbyEnemyManager chain resolved successfully with class checks. The world
address differed from both the Europe 1 world and the original Europe 2 world.
The new actor array contained 267 objects and nine remote avatars. The manager
map had nine occupied slots, capacity 24; all nine values were valid remote
avatar objects in the level and all nine keys matched `_cosmosId3`. Six had
platform value 0.

The selected participant was rediscovered by the same Steam identity string,
but its avatar pointer differed from the original. The original avatar pointer
was absent from the actor array. Local spawn coordinates were approximately
`(-145.003, -1695.000, 900.001)`, as on the Europe 1 entry: spawn position is
not a lounge identifier.

This validates re-resolving participants after a world change and associating
the returning participant by identity rather than reusing its old object pointer.
It does not establish that the same address can never be reused by the allocator.
Production lifecycle handling must also cover unavailable/loading snapshots and
membership changes without a world replacement.

### Full process restart

After the user restarted the game and entered a lounge, CE was reattached to
PID 8064. The executable base remained `0x7FF6BDD50000`, PE timestamp
`0x6ADB1AD5`, SizeOfImage `0xA79B000`. Heap objects changed. No old heap
addresses were used to traverse the new process. The name decoder was rebuilt
from module-relative block-table RVA `0x99BC390`, validating `None` and `Actor`.
This is a process-restart check, not a test with a relocated executable base.

The direct world/game-mode/manager chain passed class checks. The first snapshot
contained 110 level actors and ten remote avatars. The map had ten occupied
slots, capacity 24, no free slots; all ten values were remote avatars in the
actor array, and all ten keys matched `_cosmosId3`. Platform distribution was
seven at 0, two at 1, one at 2. The local pawn was `BP_Player_Onl_C` with position
approximately `(-0.003, -1400.000, 900.001)` and a null attachment parent.

A subsequent read contained eleven occupied entries (membership changed between
samples). All eleven display arrays passed bounded FString header/terminator
checks for three elements; all eleven root components had finite coordinate
triples and null attachment parents. All seven platform-0 entries had a
SteamID-shaped string in display element 1. The previously selected participant's
identity was present again. Reflection independently confirmed
`_platform012 +0x2A0`, `UserId +0x2D8`, `_displayNames2 +0x380`, `_cosmosId3 +0x390`.
The world root remained unchanged during that second read. These external reads
are not atomic snapshots; production code must detect container mutation.

## Current research milestone

Local avatar lookup, remote enumeration, candidate positions, per-avatar identity
association, sparse occupied-slot filtering, zone persistence, lounge replacement,
returning identity with a new object, and process restart have observational
coverage. This is enough to start a read-only production-reader prototype.

Remaining research: globally comparable lounge/region and zone identity; attached
root/world transforms; loading/teardown and concurrent container mutations;
heap-backed allocation bitmaps; Steam identity verification and reachability with
a compatible second client. No handshake, audio exchange, or console support has
been demonstrated. Runtime configuration and deployment remain unchanged.

## Session identity and world-position follow-up

Read-only reflection and disassembly in PID 8064 found:

- `LobbyGameMode.GetLobbyIndex` native wrapper RVA `0x2BB6460`, calling the
  getter at RVA `0x2DFC7B0`. The getter reads the global pointer at RVA
  `0x9948200`, checks byte `+0x1B1` and game-mode byte `+0x3EE`, follows
  `+0x2A0`, and returns int32 at `+0x150` (otherwise -1).
- The global object is `LobbyGameInstanceSubsystem`. Reflection confirms
  `_isUseNetwork +0x1B1` and `_networkObject +0x2A0`; the latter is a
  `LobbyNetworkObject`. Current lobby index reads 0.
- A bounded FString-shaped field at network object `+0x140` contains
  `EASTERN_EUROPE_5_0_00001_1`. This is a room-key candidate, not yet validated
  across controlled region/room transitions. Its suffix semantics are unknown.
- Subsystem `_lobbyNetworkSelect +0x2F8` points to `LobbyNetworkSelect`, with
  reflected methods including `GetUserLobbyInfo`, `GetDefaultLobbyArea`,
  `SearchAreaLobbies`. No methods were executed.

`LobbyAreaComponent.GetArea` wrapper RVA `0x2B99550` calls RVA `0x2DBADC0`.
Disassembly shows it follows component owner `+0x90`, actor root `+0x1A0`, and
reads the root's world translation doubles at `+0x260/+0x268/+0x270`. It then
enumerates candidate actors and selects a spatial area, reading the selected
area actor's byte at `+0x298`. Thus the examined getter does not simply return
a cached area byte in the avatar component. Area selection geometry is not yet
decoded. For the local avatar, observed world translation and RelativeLocation
matched at `(784.250, 1861.710, 913.758)` with no attachment parent.

### Room-key comparison: Europe 2 -> Europe 1

After the user confirmed entering Europe 1, the bounded string at
`[[module + 0x9948200] + 0x2A0] + 0x140` changed from
`EASTERN_EUROPE_5_0_00001_1` to `WESTERN_EUROPE_4_0_00001_1`.
The FString header was reread and remained unchanged during the observation.
The world changed, while the subsystem and network-object addresses remained
the same. `_isUseNetwork` remained 1 and `GetLobbyIndex`'s backing int32 remained
0. Thus the string distinguishes these observed regions while the numeric index
does not. Returning to the previous lounge still needs a string restoration check.
Do not cache room membership solely by the persistent network-object pointer.

### Room-key restoration: Europe 1 -> Europe 2

On the user's return to Europe 2, the same field returned exactly
`EASTERN_EUROPE_5_0_00001_1`, matching the pre-transition baseline and differing
from the Europe 1 string. Two full bounded string reads agreed. The world
changed again; subsystem and network-object pointers remained unchanged,
`_isUseNetwork` remained 1, and the numeric lobby index remained 0. The current
participant map had 14 slots and no free slots.

The room string now has controlled region-change and return coverage. Treat it
as an opaque room key rather than interpreting its segments. Cross-client
agreement, same-region room differentiation, restart stability of this newly
discovered field, and its clearing/timing during loading remain unverified.

### Exit to main menu: stale room state

After the user confirmed returning to the main menu, the world changed and its
AuthorityGameMode class became `MainTmp_SimpleKamuiGameMode_C`. The
`LobbyGameInstanceSubsystem` and `LobbyNetworkObject` remained allocated at
their previous addresses. `_isUseNetwork` still read 1, the lobby index still
read 0, and the room FString still contained `EASTERN_EUROPE_5_0_00001_1`.
Subsystem `_levelState` read 0; its active-lounge value/enum has not been checked.

The room string and `_isUseNetwork` therefore cannot establish active lounge
membership. Before following lounge-specific offsets, validate the current
world and its game-mode class. Leaving the supported lounge game mode must
invalidate the active voice session and discard participant/audio queues even
when the room key is unchanged. Validate further roots/containers only after
the mode check; do not reuse a cached game mode or enemy manager in the menu.
Unreadable/loading or inconsistent snapshots must suspend eligibility pending
a fresh valid lounge snapshot. The steady menu state was checked here, not
the timing of every intermediate loading frame.

## Diagnostic implementation

`VoiceChat/LoungeReader.{h,cpp}` implements the researched chain through a
read callback, independently of CE. `LoungeLayout.h` loads a separate optional
`[VoiceLounge]` configuration with the three module RVAs and PE timestamp/image
size guards. The console command `lounge_info` reads one snapshot and logs room,
position and distance-sorted participants. This does not create voice connections.

The reader validates the current game-mode class before consulting persistent
room state; failures return empty snapshots. It supports the verified inline
sparse-map bitmap, validates Cosmos key/object correspondence, decodes UTF-16
names, and reports syntactically validated Steam candidates only for platform 0.
It uses ComponentToWorld translation. Structural/identity observations are reread
before publication; positions are best-effort independent samples.

Verification performed so far:

- Release/x64 DLL build with MSVC v143 succeeded. Existing console conversion
  and default-library conflict warnings remain.
- Standalone synthetic `lounge_reader.cpp` tests passed with assertions enabled.
- External `lounge_probe.cpp`, using this C++ reader against PID 8064 in the
  main menu, returned `not-in-lounge` despite the previously observed stale room.
- Live C++ lounge enumeration passed in the subsequent session described below.
  The in-game console command still requires verification. Built artifacts and
  INI have not been deployed to the game.

### Live C++ reader validation, PID 19420

The next user-confirmed lounge session used a new process, PID 19420. The
external `t8_lounge_probe.exe`, built from the production reader and supplied
the repository INI, returned `ok`, room `EASTERN_EUROPE_5_0_00001_1`, and
20 distance-sorted participants. Ten had platform 0 and a syntactically valid
Steam candidate; the other ten had no Steam candidate. Distances in this sample
ranged from about 4098.9 to 12630.9 game units.

CE was independently reattached to PID 19420. A fresh read confirmed the same
room key, 20 occupied map slots with no free slots, ten platform-0 participants,
and matching map/Cosmos keys for all 20 objects. Thus the C++ result agrees with
the separately read map counts and room state. The newly discovered room chain
also survived this additional process restart. Distance values were not compared
to a simultaneous CE sample or a visually calibrated scale. No DLL deployment,
in-game console execution, handshake or audio test was performed.

## Participant UI and head-marker implementation

The voice menu now renders a compact scrolling participant table with separate
game/Steam names, ping, mute, receive volume and fresh signal level. The guarded
voice worker publishes the lounge snapshot and bounded/cached Steam profiles.
The existing single-opponent transport still supplies match telemetry; lounge
profiles are explicitly disconnected until a multiparty transport is implemented.

Head markers reuse the match avatar/status renderer. `LoungeScene.h` projects
Unreal world coordinates directly through the configured FSceneViewState matrices
and rejects off-screen/behind-camera points and world changes. Live camera-chain
reads in PID 19420 had valid perspective matrix guards; the external C++ probe
read 32 participants and projected one head anchor into the current viewport.
This does not verify final icon placement or wall occlusion in the rendered game.

The subsystem `_displayName +0x1E8` was also inspected: its text matched the
Steam persona, not the different nickname visible over the local avatar. It is
therefore not used as the local game nickname; the local table row says `You`.
Remote game names continue to come from their validated display-name arrays.

Checks passed: Release/x64 DLL build; menu layout with 64 rows at multiple
resolutions; participant-state/expiry and projection tests; lounge reader tests;
Steam lifecycle/shutdown/identity/protocol smoke tests. The in-game HUD height
uses a provisional world-Z offset of 110 and still needs visual calibration.
The rebuilt DLL/PDB and INI have not been installed into the running game.

### UI build deployment

After the user closed the game, the Release/x64 DLL was installed as
`Polaris/Binaries/Win64/plugins/T8FrameOfDestruction.asi`, with its matching
`T8FrameOfDestruction.pdb` and the updated
`plugins/T8FrameOfDestruction/config/memory_address.ini`, under the Steam
installation of TEKKEN 8. Existing files were backed up with the suffix
`.backup-lounge-ui-<timestamp>`. SHA-256 comparisons against all three source
files passed. Existing diagnostics and channel-probe opt-in markers were present.
In-game UI/head-marker validation remains pending; this deployment does not add
the unimplemented multiparty lounge voice transport.

### UI refinement after first in-game review

The participant list now omits console/non-Steam peers and hides the room key.
Rows are grouped self / connected / disconnected, with case-insensitive Unicode
game-nickname ordering inside peer groups and identity tie-breaks. A left click
on a menu avatar queues the existing Steam-overlay profile request.

World markers now use ImGui's background draw list, below menu and console
windows. Visible markers are sorted far-to-near using camera-space Euclidean
distance; markers beyond 2000 Unreal world units from the camera are omitted.
Release/x64 build, participant filtering/sorting/range tests, and the menu
resolution/scrolling tests passed. This refinement is built but not deployed
yet: the game was still running (PID 16200) at verification time.

The user subsequently closed the game. A persistent console setting was added:
`lounge_avatar_distance [0..100000|reset]` (default 2000, zero hides markers).
It applies immediately and saves `[VoiceChat] lounge_avatar_distance` in user
settings. Parsing/range tests and Release/x64 build passed. This build, including
the UI refinements above, was installed as the ASI with its matching PDB; SHA-256
comparisons passed. Previous binaries are backed up with suffix
`.backup-avatar-range-20261113-185031`. Memory INI and user preferences were not
overwritten by this deployment. In-game verification of the refinements and
console command remains pending.

### Multiparty transport implementation

Implemented room-bound `T8LV` envelopes on Steam Messages channel 21561, separate
from the existing match channel. The worker supplies authenticated sender IDs,
fresh BCrypt nonces and Steam-ID preferences to the callback-driven `LoungeVoice`
engine. Per-peer sequence windows, bounded jitter queues and saturated mixing
support simultaneous speakers. Hearing radius defaults to 1500 world units and
is independently persisted by `lounge_voice_distance [0..10000|reset]`.

The reader now exposes the validated game-mode pointer for fast current-world /
mode / active-flag checks between full roster reads. Audio routing is exclusive;
membership/room/world/control changes clear queued lounge playback. Presentation
uses actual handshake state, Steam ping/quality, signal, microphone state and
counters. PCM remains uncompressed mono 16 kHz, 20 ms per packet.

Release/x64 build, deterministic three-client protocol/mixing/control tests,
reader/lifecycle validation and Steam diagnostics lifecycle/protocol smoke checks
passed. No live second-client voice exchange has been performed; real Steam
delivery and audible quality remain unverified.

Installed the Release ASI and matching PDB with the game closed; SHA-256
comparisons passed. Previous files use backup suffix
`.backup-lounge-voice-20261113-200419`. Existing runtime INI/preferences were
preserved.

`LobbyPlayer._nearEnemyCount` (`+0x704`) and `_farEnemyCount` (`+0x708`) both
read zero in this state despite seven remote avatars; do not use as membership.
Remote blueprint exposes `BP_Lobby_AreaComponent` at `+0x3A8`, but its area
semantics and the lounge/server identity remain unresolved.

## Next checks

1. Reverse local movement to verify coordinate direction and repeatability;
   compare component transform/world location and parent attachment.
2. Approach a platform-0 avatar and confirm its visible identity matches memory.
3. Observe arrival/departure, zone changes, and lounge changes; distinguish active
   avatars from stale objects and validate a stable lounge/zone identifier.
4. Repeat discovery after process restart; validate name pool and actor layouts.
5. Only then design the production reader and test transport with a second modded
   Steam client. No runtime INI update or deployment has been performed.
