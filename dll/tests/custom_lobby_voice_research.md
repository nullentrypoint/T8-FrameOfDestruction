# Custom lobby "Player Match" voice research

## 2026-11-15: initial read-only CE capture

Game PID 23004, module base `0x7FF66D5C0000`, PE timestamp `0x6ADB1AD5`,
SizeOfImage `0xA79B000`. Heap addresses below are observations from this process,
not configuration values or restart-stable chains.

User confirmed two current room participants: self and a remote Steam player.
The existing identity vector at module RVA `0x9BDDB18` contains only self.
GWorld reports `Boot`, game-mode class `BP_PolarisGameMode_C`, so Fight Lounge
classification cannot identify this mode.

| Name | Game user ID | Tekken ID | Steam ID |
| --- | --- | --- | --- |
| Local player | `<local-user-id>` | `<local-tekken-id>` | `<local-steam-id>` |
| Remote player | `<remote-user-id>` | `<remote-tekken-id>` | `<remote-steam-id>` |

Both peer records contain matching copies of their Steam ID. A serialized
server response also associates the remote nickname, Tekken ID, `pUserId`,
`onlineId`, and platform value 3. This platform encoding differs from the
Fight Lounge actor's platform field.

## Candidate session and member map

Session object `0x18B7EF21A00`, vtable module RVA `0x859BFA8`:

- `+0x08`: session-ID object `0x18B73082090`.
- Session-ID object vtable RVA `0x859BF08`; MSVC string at object `+0x08`.
- Session ID observed: `PLM<room-A>` (redacted).
- `+0x178`: probable unordered-map load-factor field.
- `+0x180`: member-map sentinel pointer `0x18B7A328300`.
- `+0x188`: member count, 2.
- `+0x190..0x1A0`: bucket-vector begin/end/capacity.
- `+0x1A8`, `+0x1B0`: bucket mask/count, 7 / 8.
- Two additional maps at `+0x1B8` and `+0x1F8` also have size 2;
  their meanings are not established.

Member-map nodes:

- `+0x00`: next; `+0x08`: previous; iteration terminates at sentinel.
- `+0x10`: MSVC string game-user-ID key.
- `+0x30`: participant record pointer.
- Nodes observed: `0x18B7A3273C0` (self), `0x18B6A1B79C0` (remote).

Participant record layout, observed on both nodes:

- `+0x08`: game user ID, MSVC string.
- `+0x28`: game nickname, MSVC string.
- `+0x50`: Tekken ID, MSVC string.
- `+0x70`: Steam-ID string.
- `+0x90`: duplicate Steam-ID string.
- Self record `0x18B875C76C0`; remote record `0x18B6D59D9C0`.

The member map yields both names and IDs in the table, independently matching
the serialized response's two-entry `userList`. That response reports group 1,
orders 0/1, memberSize 2, and inFighting=false. Serialized response buffers and
UI copies must not be treated as authoritative current membership.

## Candidate owner and static session identifier

Observed ownership:

```text
0x188056A0010 + 0x730 -> 0x18AB18F5C10
0x18AB18F5C10        -> 0x18B7EF21A00 (session)
```

At owner `+0x738` is the associated shared_ptr control-block pointer
`0x18AB18F5C00`. Owner vtable RVA is `0x859BFB0`. A restart-stable root
for this owner has NOT been established.

Separately:

```text
module + 0x9BE0498 -> 0x18B73082F90
session-ID string at object + 0x08 -> same PLM session ID
```

This global is a different session-ID object from the member map's ID.
Its lifecycle and meaning remain unverified; it may retain a cached room ID.
Nearby module RVA `0x9BE0480` points to `0x188723F6580`, with vtable RVA
`0x85AC6F8`; role is not yet established.

Additional native infrastructure found through RTTI:
`PlatformServerDiarkis`, `DiarkisMatchingDriver`, `DiarkisModuleMatch`,
`DiarkisInterface`, `DiarkisRoom`, and `SessionService`. These are research
leads, not validated reader chains.

## Required next observations

### Remote departure validated (same process)

After the user confirmed the remote player had left, rereading the owner chain yielded
the same holder/session and session ID. The map count at session `+0x188`
changed from 2 to 1. Traversing the live list yielded only the self node
`0x18B7A3273C0`, with the local nickname and Steam ID; the remote node was
no longer present. The global session ID remained unchanged, as expected while
self remains in the same room. This validates removal for the candidate map,
but does not yet validate local room exit or a restart-stable root.

### Remote rejoin validated (same process)

After the user confirmed rejoin, the same owner/holder/session chain returned
count 2 again. Self retained its original node/record. The remote entry was
newly allocated: node `0x1874EA6F900`, record `0x18B902FF700`. Its key,
remote nickname, Tekken ID, and both Steam-ID copies
matched the initial capture. The session ID was unchanged.
Thus live list traversal correctly follows removal/reallocation; caching
participant addresses would be incorrect.

### Local exit validated; static root discovered

After local exit to the main menu, owner `+0x730` and `+0x738` both became
null. The old session allocation was no longer valid (vtable replaced, old
count location now 81). Global RVA `0x9BE0498` still returned the old PLM room ID:
it is explicitly unsuitable as an active-room predicate. World changed to
`MainMenu_PL`, mode `MainTmp_SimpleKamuiGameMode_C`.

Read-only constructor analysis found the owner wrapper at parent `+0x640`.
The wrapper constructor at RVA `0x5B71700` allocates the owner and calls
RVA `0x5B71750`; its caller at RVA `0x5B40473` uses parent `+0x640`.
The parent has a static pointer at module RVA `0x9BDCF40`.

Candidate complete chain, directly read and validated in the main menu:

```text
module + 0x9BDCF40 -> parent       (0x187480A60A0 in initial process)
parent + 0x640    -> owner        (0x188056A0010; vtable RVA 0x859BFB0)
owner + 0x730     -> holder       (null after leaving; formerly 0x18AB18F5C10)
holder + 0       -> session
session + 0x180  -> member-map sentinel
session + 0x188  -> member count
```

This chain still requires an in-room read through the static root and validation
after a full process restart. None of these offsets are deployed yet.

### Full restart and new room validated

After the user restarted the game and entered a room with the remote player, CE attached
to new PID **23060** (previous PID 23004). Module base happened to remain
`0x7FF66D5C0000`; timestamp and SizeOfImage were unchanged. The read used only
the module name and candidate offsets, with no previous heap addresses.

```text
module + 0x9BDCF40 -> 0x179BBF260A0
parent + 0x640    -> 0x17A79650010  (vtable RVA 0x859BFB0)
owner + 0x730     -> 0x17AD8EF2F50
holder + 0       -> 0x17E09388A80  (vtable RVA 0x859BFA8)
session room     = PLM<room-B> (redacted, different from room A)
member count     = 2
```

Self node/record: `0x17DD8FE7640` / `0x17E0885C100`.
Remote node/record: `0x17D6FB14500` / `0x17E000D88C0`.
Both names, game-user IDs, Tekken IDs and duplicate Steam-ID strings matched the
initial capture exactly. The chain therefore passed one full process restart
and a different room ID with entirely different heap allocations.

The earlier outstanding static-root/restart checks are now complete. Additional
host-change, fighting and spectator-state behavior is still untested. Research
only: no custom-lobby reader/transport has been installed.

1. Remote leaves: validated, member-map size and nodes dropped from 2 to 1.
2. Remote rejoins: validated, new remote node/record and correct identity, count 2.
3. Local leaves room: validated owner shared_ptr clears while global ID persists.
4. New room / changed host / match and spectator transitions.
5. Static owner chain validated in-room after full process restart.

## Transport implementation

Added a configured `CustomLobbyReader`, normalized room snapshots and room-wide
multi-peer voice using the existing guarded worker and channel 21561.
`custom:PLM...` room envelopes distinguish custom rooms from Fight Lounge rooms.
Custom-room voice requires local membership and ignores the proximity settings.
The current-session pointer is checked each worker tick; membership is reread
every 200 ms. Leaving/changing a room resets audio capture/playback.

The HUD renders only connected Steam participants horizontally from the top
right to the left, with the shared speaking/mute badges. Self is included once
there is a confirmed peer connection. The full roster remains in the menu.

The production C++ reader was also run externally against PID 23060:
`status=0 count=2 current=1`, with local/remote nicknames and their expected Steam IDs.
Release/x64 DLL build, custom reader tests, three-client transport/mixer tests,
participant/HUD layout tests, menu resolution tests, Fight Lounge reader tests
and Steam lifecycle/protocol smoke checks passed. Live voice delivery and visual
HUD verification are still pending. Deployment requires game closure and the
updated `[VoiceCustomLobby]` INI section.

No game-memory writes, injected calls, breakpoints, or runtime-reader changes
were used in this capture. No custom-lobby voice support is enabled yet.
