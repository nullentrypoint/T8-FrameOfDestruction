# Cheat Engine MCP Techniques

This reference belongs to the skill directory; locate project files from the
repository root. Discover exact MCP tool names and schemas through the available
tool catalog. In a Code Mode environment, use
`search({namespace: "cheatengine", ...})` and the exact returned tool path.
Do not guess method names or reuse an outdated PID.

## Inspecting a pointer chain

Typical tools include `open_process`, `evaluate_lua`, memory reads, and AOB scans.
For this `evaluate_lua` example, **replace the trail with the current INI values**.

```lua
local base = getAddress('Polaris-Win64-Shipping.exe')
assert(readSmallInteger(base) == 0x5A4D, 'Process/module is not readable')
local trail = {0x959a320, 0x10, 0x68, 0x8, 0x88} -- 3.03.01 example
local p = base
local out = {}
for i, offset in ipairs(trail) do
  local address = p + offset
  local value = readQword(address)
  out[#out + 1] = string.format('%d @%X = %s', i, address,
    value and string.format('%X', value) or 'unreadable')
  if not value or value == 0 then break end
  p = value
end
return table.concat(out, '\n')
```

Printing a readable chain does not prove its purpose. An outdated root may lead
to string data, including UTF-16 bytes that resemble QWORD values. A null root
may be normal during loading or in menus; correlate observations with game state.

## Finding roots and structure layouts

1. Read PE section boundaries from the live executable rather than reusing old
   `.data` sizes. The PE offset is at DOS header +0x3c; the section table follows
   the signature, COFF header, and optional header. Validate reads and bounds.
2. Read data sections in chunks, such as 0x1000 bytes. Decode little-endian QWORDs
   and filter using readable committed regions/plausible pointers rather than
   hardcoded heap-address prefixes from a previous session.
3. For player roots, try preserved inner-chain links and retain candidates with
   the selected character IDs, repeated blocks, and frame counters. For identity
   roots, look for a valid vector of pointers and valid strings in its objects.
4. If the stride is unknown, search for repeated recognizable field combinations
   in adjacent blocks, then verify candidate slots with independent fields.
5. For a side root, save candidates whose dereferenced pointer +4 contains a
   QWORD 0/1. This final +4 is a historical hypothesis to verify. After changing
   selected side, resolve each root again and compare values; check the reverse
   transition and a process restart. Do not modify memory to validate an address.

## Capturing without excessive CE memory use

- Create one named timer with a record/time limit, such as a few hundred fresh
  frames for a controlled scenario. Remove your previous timer before replacing
  it and stop capture if the process changes.
- Resolve a valid player base and select the current slot using the verified
  frame counter. Check every `readBytes` result's length and block bounds.
  A partial byte table is not a complete read.
- Where possible, retrieve unsaved frames from the rollback ring: a 16 ms timer
  often skips frames. Explicitly record gaps if slots have already been
  overwritten. Start a new segment when frame_count resets.
- Compare the selected block's counter before and after reading, discarding
  unstable records. Frame and camera reads are still not atomic; do not claim
  exact synchronization.
- Stream chunks to disk rather than retaining hundreds of full Lua byte tables.
  Their memory overhead greatly exceeds the binary data size. Use small chunks,
  such as 1024 bytes, with `string.char(table.unpack(...))`.
- Automatically disable/remove the timer and close the file on completion,
  read failure, or process change. Release only your own saved buffers, not the
  entire CE state. Do not poll background tool jobs in a loop.
- Use the current environment's approved temporary directory. Forward slashes
  in Windows Lua paths avoid nested JSON/backslash escaping problems.

### Historical 3.03.01 capture format

These capture files are not bundled with the repository. Their format was:

```text
u32 little-endian: record count
u32 little-endian: rollback block size
for each record:
    u32 little-endian: game frame
    byte[block size]: frame data containing both players
```

One spatial capture appended 256 bytes to **each record**: 16 projection doubles,
then 16 view doubles. This variant is not identified in the historical header;
specify it explicitly to the converter. For new formats, add version/flags and
offset/game-version metadata rather than inferring layout from the filename.
Do not feed binary captures directly to `dll/tests/replay.cpp`: it expects a
text field count/name list followed by `frame + P1 fields + P2 fields` in that order.

## Finding Steam initializers by signature

The following pattern was used for 3.03.01:

```text
40 53 48 83 EC 20 48 8B D9 FF 15 ?? ?? ?? ??
8B C8 48 8D 15 ?? ?? ?? ?? FF 15 ?? ?? ?? ?? 48 89 03
```

Filter AOB matches to the executable's code section. For a match at address `a`:

- Interface string: `a + 24 + signed_i32(a + 20)`.
- First import target: `QWORD(a + 15 + signed_i32(a + 11))`.
- Second import target: `QWORD(a + 30 + signed_i32(a + 26))`.

These formulas describe a specific instruction layout, not a general x64
decoder. If instructions change, disassemble and update the validator.
Verify the interface string (`SteamUser021`, `SteamNetworking006`, or
`SteamMatchMaking009`) and exports `SteamAPI_GetHSteamUser` /
`SteamInternal_FindOrCreateUserInterface`. Before game initialization, an import
slot may reference an executable delay-load thunk.
Search data sections for the context triple: initializer pointer, generation,
and interface pointer. Validate it against `dll/VoiceChat/SteamDiagnostics.cpp`.
