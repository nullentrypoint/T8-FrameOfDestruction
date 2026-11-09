"""Read the researched two-player identity container without attaching a debugger.

RVAs are build-specific candidates, not a verified active-match API.
Pass the actual ASLR module base for the specified process.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import struct
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pid", type=int)
    parser.add_argument("module_base", type=lambda text: int(text, 0))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--watch-seconds", type=float, default=0,
                        help="Sample for this duration; write changes/errors as JSONL instead of a single JSON snapshot")
    parser.add_argument("--interval", type=float, default=0.5)
    args = parser.parse_args()
    if not math.isfinite(args.watch_seconds) or not math.isfinite(args.interval) or args.watch_seconds < 0 or args.interval < 0.1:
        parser.error("Watch duration must be finite and nonnegative; interval must be finite and at least 0.1 seconds")
    if C.sizeof(C.c_void_p) != 8:
        raise RuntimeError("Requires x64 Python")
    k = C.WinDLL("kernel32", use_last_error=True)
    k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    k.OpenProcess.restype = W.HANDLE
    k.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
    k.ReadProcessMemory.restype = W.BOOL
    k.CloseHandle.argtypes = [W.HANDLE]
    k.CloseHandle.restype = W.BOOL
    k.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
    k.WaitForSingleObject.restype = W.DWORD
    handle = k.OpenProcess(0x100410, False, args.pid)  # Query, read, synchronize; no write/debug rights
    if not handle:
        raise C.WinError(C.get_last_error())

    def read(address, size):
        buffer = C.create_string_buffer(size)
        count = C.c_size_t()
        if not k.ReadProcessMemory(handle, address, buffer, size, C.byref(count)) or count.value != size:
            raise RuntimeError(f"Unreadable address {address:#x}, requested {size}, read {count.value}")
        return buffer.raw

    def qword(address):
        return struct.unpack("<Q", read(address, 8))[0]

    def string(address):
        header = read(address, 32)
        length, capacity = struct.unpack_from("<QQ", header, 16)
        if length > 64 or capacity < length or capacity > 4096:
            raise RuntimeError(f"Invalid string header at {address:#x}")
        pointer = struct.unpack_from("<Q", header)[0] if capacity >= 16 else address
        if capacity < 16 and capacity != 15:
            raise RuntimeError("Unexpected small-string layout")
        data = read(pointer, length + 1)
        if data[-1] != 0:
            raise RuntimeError("String terminator changed or layout mismatch")
        if read(address, 32) != header:
            raise RuntimeError("String header changed during read; retry snapshot")
        return data[:-1].decode("ascii")

    def snapshot():
        base = args.module_base
        # Verify the observed destructor before interpreting this build's container.
        if read(base, 2) != b"MZ":
            raise RuntimeError("Module base is not a PE image")
        if qword(base + 0x855C2C8) != base + 0x5BF31F0:
            raise RuntimeError("Identity vtable validation failed; unsupported build")
        root = qword(base + 0x9B8CB70)
        if not root:
            raise RuntimeError("Identity container not initialized")
        vector = read(root, 24)
        begin, end, capacity = struct.unpack("<QQQ", vector)
        if not begin <= end <= capacity or (end - begin) % 8 or capacity - begin > 8 * 64:
            raise RuntimeError("Unexpected identity vector layout/size")
        pointers = read(begin, end - begin) if end != begin else b""
        entries = []
        for index, (address,) in enumerate(struct.iter_unpack("<Q", pointers)):
            if qword(address) != base + 0x855C2C8:
                raise RuntimeError("Entry vtable mismatch; snapshot invalid")
            fields = {hex(offset): string(address + offset) for offset in (0x60, 0x80, 0xA0, 0xC0)}
            words = struct.unpack("<4I", read(address + 8, 16))
            entries.append({"index": index, "address": hex(address),
                            "unclassified_words_08_14": list(words), "strings": fields})
        if qword(base + 0x9B8CB70) != root or read(root, 24) != vector or (pointers and read(begin, len(pointers)) != pointers):
            raise RuntimeError("Container changed during read; retry snapshot")
        return {"pid": args.pid, "module_base": hex(base),
                  "captured_at": datetime.now(timezone.utc).isoformat(),
                  "root_rva": "0x9b8cb70", "root": hex(root), "entries": entries,
                  "active_match_verified": False}

    try:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        if not args.watch_seconds:
            result = snapshot()
            args.out.write_text(json.dumps(result, indent=2), encoding="utf-8")
            print(json.dumps(result, indent=2))
            return
        # Validate before starting; subsequent read failures can be transitions.
        initial = snapshot()
        started = time.monotonic()
        previous = None
        samples = changes = errors = 0
        reason = "duration_elapsed"
        # Exclusive creation protects earlier captures from accidental overwrite.
        with args.out.open("x", encoding="utf-8") as output:
            while True:
                try:
                    result = initial if samples == 0 else snapshot()
                    event = {"event": "snapshot", **result}
                except (RuntimeError, UnicodeDecodeError) as exc:
                    errors += 1
                    event = {"event": "read_error", "pid": args.pid,
                             "captured_at": datetime.now(timezone.utc).isoformat(), "error": str(exc)}
                samples += 1
                key = {k: v for k, v in event.items() if k != "captured_at"}
                if key != previous:
                    output.write(json.dumps(event) + "\n")
                    output.flush()
                    print(json.dumps(event), flush=True)
                    previous = key
                    changes += 1
                remaining = args.watch_seconds - (time.monotonic() - started)
                if remaining <= 0:
                    break
                status = k.WaitForSingleObject(handle, max(1, int(min(remaining, args.interval) * 1000)))
                if status == 0:
                    reason = "process_exited"
                    break
                if status != 258:  # WAIT_TIMEOUT
                    raise C.WinError(C.get_last_error())
            summary = {"event": "watch_finished", "pid": args.pid, "reason": reason,
                       "samples": samples, "changes": changes, "read_errors": errors,
                       "captured_at": datetime.now(timezone.utc).isoformat()}
            output.write(json.dumps(summary) + "\n")
            print(json.dumps(summary), flush=True)
    finally:
        k.CloseHandle(handle)


if __name__ == "__main__":
    main()
