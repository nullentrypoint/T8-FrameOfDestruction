"""Read-only Windows x64 process search; no debugger, injection or memory writes."""
import argparse
import ctypes as C
from ctypes import wintypes as W
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import time


class MemoryInfo(C.Structure):
    _fields_ = [("BaseAddress", C.c_void_p), ("AllocationBase", C.c_void_p),
                ("AllocationProtect", W.DWORD), ("PartitionId", W.WORD),
                ("RegionSize", C.c_size_t), ("State", W.DWORD),
                ("Protect", W.DWORD), ("Type", W.DWORD)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pid", type=int)
    parser.add_argument("steam_id", type=int)
    parser.add_argument("--name")
    parser.add_argument("--tekken-id")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--seconds", type=float, default=120)
    parser.add_argument("--start-address", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--end-address", type=lambda value: int(value, 0), default=0x7FFFFFFF0000)
    parser.add_argument("--recheck", type=Path, help="Re-read candidates from an earlier scan instead of searching")
    args = parser.parse_args()
    if C.sizeof(C.c_void_p) != 8 or C.sizeof(MemoryInfo) != 48:
        raise RuntimeError("Requires 64-bit Python on Windows")
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    if not 0 <= args.start_address < args.end_address <= 0x7FFFFFFF0000:
        parser.error("Invalid address range")
    needles = {"steam-u64": struct.pack("<Q", args.steam_id),
               "steam-ascii": str(args.steam_id).encode(),
               "steam-utf16": str(args.steam_id).encode("utf-16le")}
    for label, text in [("nickname", args.name), ("tekken-id", args.tekken_id)]:
        if text:
            needles[label + "-utf8"] = text.encode()
            needles[label + "-utf16"] = text.encode("utf-16le")
    kernel = C.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    kernel.OpenProcess.restype = W.HANDLE
    kernel.CloseHandle.argtypes = [W.HANDLE]
    kernel.CloseHandle.restype = W.BOOL
    kernel.VirtualQueryEx.argtypes = [W.HANDLE, C.c_void_p, C.POINTER(MemoryInfo), C.c_size_t]
    kernel.VirtualQueryEx.restype = C.c_size_t
    kernel.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
    kernel.ReadProcessMemory.restype = W.BOOL
    handle = kernel.OpenProcess(0x410, False, args.pid)  # QUERY_INFORMATION | VM_READ only
    if not handle:
        raise C.WinError(C.get_last_error())
    started = time.monotonic()
    result = {"pid": args.pid, "steam_id": str(args.steam_id),
              "mode": "recheck" if args.recheck else "scan",
              "range_start": hex(args.start_address), "range_end": hex(args.end_address),
              "captured_at": datetime.now(timezone.utc).isoformat(),
              "patterns": {k: v.hex() for k, v in needles.items()}, "hits": [],
              "bytes_read": 0, "bytes_skipped_on_read_error": 0, "complete": False}
    buffer = C.create_string_buffer(1024 * 1024)

    def read(address, size):
        got = C.c_size_t()
        kernel.ReadProcessMemory(handle, address, buffer, size, C.byref(got))
        return C.string_at(buffer, got.value)

    try:
        if args.recheck:
            previous = json.loads(args.recheck.read_text(encoding="utf-8"))
            if previous["pid"] != args.pid or previous["steam_id"] != str(args.steam_id):
                raise ValueError("Recheck requires the same PID and SteamID; addresses cannot carry across restarts")
            for hit in previous["hits"]:
                expected = bytes.fromhex(previous["patterns"][hit["pattern"]])
                data = read(int(hit["address"], 16), len(expected))
                result["bytes_read"] += len(data)
                result["hits"].append({**hit, "still_matches": data == expected, "readable_bytes": len(data)})
            result["matching_candidates"] = sum(hit["still_matches"] for hit in result["hits"])
            result["complete"] = True
        else:
            address = args.start_address
            overlap = max(map(len, needles.values())) - 1
            tail = b""
            tail_end = 0
            seen = set()
            while address < args.end_address:
                if time.monotonic() - started > args.seconds:
                    result["stop_reason"] = "time_budget"
                    break
                info = MemoryInfo()
                if not kernel.VirtualQueryEx(handle, address, C.byref(info), C.sizeof(info)):
                    result["query_error"] = C.get_last_error()
                    result["stop_reason"] = "query_failed"
                    break
                base = info.BaseAddress or 0
                end = min(base + info.RegionSize, args.end_address)
                if end <= address:
                    raise RuntimeError("Non-advancing memory region")
                # Committed readable pages only; never touch guard/no-access pages.
                readable = info.State == 0x1000 and not info.Protect & 0x101 and info.Protect & 0xFF in (2, 4, 8, 32, 64, 128)
                if readable:
                    cursor = max(base, address)
                    while cursor < end:
                        if time.monotonic() - started > args.seconds:
                            break
                        wanted = min(len(buffer), end - cursor)
                        data = read(cursor, wanted)
                        result["bytes_read"] += len(data)
                        if tail_end != cursor:
                            tail = b""
                        combined = tail + data
                        origin = cursor - len(tail)
                        for label, needle in needles.items():
                            pos = combined.find(needle)
                            while pos >= 0:
                                location = origin + pos
                                key = (label, location)
                                if key not in seen:
                                    seen.add(key)
                                    hit = {"pattern": label, "address": hex(location),
                                           "allocation": hex(info.AllocationBase or 0),
                                           "region": hex(base), "type": hex(info.Type)}
                                    result["hits"].append(hit)
                                    print(json.dumps(hit), flush=True)
                                pos = combined.find(needle, pos + 1)
                        tail = combined[-overlap:]
                        tail_end = cursor + len(data)
                        if len(data) == wanted:
                            cursor += wanted
                        else:
                            # Continue at the next page after a failed/partial read.
                            next_page = min(end, ((cursor + len(data)) // 4096 + 1) * 4096)
                            result["bytes_skipped_on_read_error"] += next_page - cursor - len(data)
                            cursor = next_page
                    if cursor < end:
                        result["stop_reason"] = "time_budget"
                        break
                address = end
            else:
                result["complete"] = True
            # Windows reports ERROR_INVALID_PARAMETER at the top of user space.
            if result.get("query_error") == 87 and address >= 0x7FFFFFFEFFFF:
                result["complete"] = True
                result["stop_reason"] = "end_of_address_space"
        result["seconds"] = round(time.monotonic() - started, 3)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps({k: v for k, v in result.items() if k not in ("hits", "patterns")}, indent=2))
        print(f"Hits: {len(result['hits'])}; saved: {args.out}")
    finally:
        kernel.CloseHandle(handle)


if __name__ == "__main__":
    main()
