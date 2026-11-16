# Room voice addresses: Fight Lounge and Player Match

Historical 3.03.01 observations, not fallback values for another patch.
Consult the current production readers and INI before discovery. All pointers
below are QWORDs; RVAs are executable-relative. No participant identities or
private room IDs belong in this reference.

## INI sections and build guards

Both room layouts were validated with PE timestamp `0x6ADB1AD5` and
SizeOfImage `0xA79B000`. Check the new PE and fixed layouts before changing these
guards. An old layout passing a newly copied fingerprint is not validation.

| Section / key | Historical RVA |
| --- | --- |
| VoiceLounge / world_rva | `0x9BBA200` |
| VoiceLounge / name_blocks_rva | `0x99BC390` |
| VoiceLounge / subsystem_rva | `0x9948200` |
| VoiceCustomLobby / root_rva | `0x9BDCF40` |
| VoiceCustomLobby / owner_vtable_rva | `0x859BFB0` |
| VoiceCustomLobby / session_vtable_rva | `0x859BFA8` |
| VoiceCustomLobby / id_vtable_rva | `0x859BF08` |
| VoiceCustomLobby / member_vtable_rva | `0x859B720` |

Each section also requires `pe_timestamp` and `image_size`.
`LoungeLayout.h` loads these independently of the four core match-voice sections.

## Fight Lounge

- `[module + world_rva]` → World; `[World + 0x150]` → game mode.
  Require class `BP_LobbyGameMode_Onl_C` and bytes `+0x3EE/+0x3EF` both 1.
- `[module + subsystem_rva]` → `LobbyGameInstanceSubsystem`.
  Byte `+0x1B1` must be 1; `[subsystem + 0x2A0]` →
  `LobbyNetworkObject`; bounded FString at network `+0x140` is the opaque key.
  This key and subsystem flags can persist after leaving; never use them alone.
- Local actor: `[World + 0x1B8]` → GameInstance; `[instance + 0x38]` →
  LocalPlayers data; `[data]` → LocalPlayer; `[local + 0x30]` → controller;
  `[controller + 0x340]` → `BP_Player_Onl_C`. Validate the array header.
- `[mode + 0x488]` → `LobbyEnemyManager`; sparse map inline at manager `+0x28`.
  Slots are 24 bytes: uint64 Cosmos key at `+0`, actor pointer at `+8`.
  Only occupied allocation bits are traversed. Current reader supports four
  inline uint32 allocation words / 128 slots; heap-backed bitmaps require new
  research and reader support, not relaxed bounds.
- Remote actor class `BP_AvatarEnemy_Onl_C`; int32 platform at `+0x2A0`,
  FName user ID at `+0x2D8`, display FString array at `+0x380` (three entries,
  stride 16), uint64 Cosmos ID at `+0x390`, matching the sparse-map key.
  Display entry 0 is game nickname; entry 1 is the platform ID. Platform 0
  supplies canonical Steam candidates; console fields must not become peers.
- `[actor + 0x1A0]` → root component; doubles XYZ at `+0x260` are world
  translation. `+0x128` is RelativeLocation and is not an interchangeable field.
- FName decoding: block pointer at `module + name_blocks_rva + (id >> 16)*8`;
  entry at block + `(id & 0xFFFF)*2`; uint16 header, length `header >> 6`,
  wide flag `header & 1`. Read exact lengths, not unbounded C strings.
- Camera is `[SpatialCamera]`, using the existing FSceneViewState chain and
  double view/projection matrices. Verify projection separately from voice.
  World markers display confirmed voice peers only, with camera-distance culling.

## Custom room / Player Match / Lobby

```text
[module + root_rva] -> parent
[parent + 0x640]    -> owner (owner vtable guard)
[owner + 0x730]     -> holder (null outside the session)
[holder + 0]       -> session (session vtable guard)
[session + 0x08]   -> session-ID object (ID vtable guard)
ID object + 0x08   -> MSVC string, validated PLM-prefixed room key
```

The owner shared-pointer control block is at `owner + 0x738`.
On validated local exit, both shared-pointer fields became null, while a
separate global cached ID at historical RVA `0x9BE0498` retained the old key.
Do not put that cache into the current-session chain.

Session member map:

- Sentinel pointer at `session + 0x180`, uint64 count at `+0x188`.
- Node `+0/+8`: next/previous; node `+0x10`: MSVC game-user-ID string;
  node `+0x30`: participant-record pointer, validated by member vtable.
- Record strings: game-user ID `+0x08`, nickname `+0x28`, Tekken ID `+0x50`,
  platform-ID copies `+0x70/+0x90`. Require key/record agreement, unique user/Steam
  IDs, canonical matching Steam copies, bounded linked-list traversal ending at
  the sentinel, and stable rereads. Current production count bound is 64.
- MSVC string: inline storage or pointer at `+0`, uint64 length/capacity at
  `+16/+24`; capacity 15 means inline. Validate lengths/capacity/terminator.
  These are not UE FStrings.
- The native platform encoding differs from lounge actors. Production normalizes
  only matching canonical Steam-ID copies to a Steam endpoint.

The existing match identity vector may contain only self while this room has
several participants. Room membership is not equivalent to the verified
local/opponent pair used for fighter HUD placement.

## Validation and tools

Read **repo:** `dll/tests/lounge_voice_research.md`,
`dll/tests/custom_lobby_voice_research.md`, `dll/tests/README.md`.
Historical coverage: lounge zone/room change and restart; custom remote leave,
rejoin with new allocations, local exit with cached old ID, different room and
full process restart. Production C++ probes matched CE roster observations.
Later two-client logs showed connected audio in both modes, and the user
confirmed intelligible two-way speech. Host migration and spectator-viewer
fighter mapping still require dedicated live verification.

From `dll/` in an x64 developer cmd, use a temporary `T8_TEST_OUT` directory:

```bat
cl /nologo /std:c++20 /EHsc /MD /DNOMINMAX /I. tests\lounge_reader.cpp VoiceChat\LoungeReader.cpp /Fo"%T8_TEST_OUT%\\" /Fe"%T8_TEST_OUT%\lounge_reader.exe"
"%T8_TEST_OUT%\lounge_reader.exe"
cl /nologo /std:c++20 /EHsc /MD /I. tests\custom_lobby_probe.cpp /Fo"%T8_TEST_OUT%\\" /Fe"%T8_TEST_OUT%\custom_probe.exe"
"%T8_TEST_OUT%\custom_probe.exe" CURRENT_PID ..\config\memory_address.ini
cl /nologo /std:c++20 /EHsc /MD /DNOMINMAX /I. tests\custom_lobby_reader.cpp /Fo"%T8_TEST_OUT%\\" /Fe"%T8_TEST_OUT%\custom_reader.exe"
"%T8_TEST_OUT%\custom_reader.exe"
```

Also run `tests/lounge_voice.cpp`, participant/layout tests and Steam lifecycle
smoke checks for transport/presentation changes. These validate algorithms and
synthetic failure cases, not new addresses. Keep raw probe output/logs external.
Deploy all required room INI sections alongside the matching DLL/PDB; the INI
is loaded once at startup.
