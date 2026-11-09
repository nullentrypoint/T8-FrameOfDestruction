"""Offline x64 PE context-reference inspection. Requires capstone; never opens a process."""
import argparse
import bisect
from pathlib import Path
import re
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_64


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("rva", type=lambda value: int(value, 0))
    parser.add_argument("--function", action="store_true", help="Disassemble the function containing this RVA")
    parser.add_argument("--calls", action="store_true", help="Find direct CALL/JMP references instead of MOV/LEA")
    args = parser.parse_args()
    data = args.exe.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Expected PE image")
    if struct.unpack_from("<H", data, pe + 4)[0] != 0x8664:
        raise ValueError("Expected x64 image")
    optional = pe + 24
    table = optional + struct.unpack_from("<H", data, pe + 20)[0]
    sections = []
    for i in range(struct.unpack_from("<H", data, pe + 6)[0]):
        offset = table + i * 40
        _, _, va, size, raw = struct.unpack_from("<8sIIII", data, offset)
        flags = struct.unpack_from("<I", data, offset + 36)[0]
        sections.append((va, size, raw, flags))

    def file_offset(rva):
        for va, size, raw, _ in sections:
            if va <= rva < va + size:
                return raw + rva - va
        raise ValueError(f"RVA has no file backing: {rva:#x}")

    exception_rva, exception_size = struct.unpack_from("<II", data, optional + 112 + 3 * 8)
    unwind = file_offset(exception_rva)
    functions = [struct.unpack_from("<III", data, unwind + i)
                 for i in range(0, exception_size, 12)]
    functions.sort()
    starts = [f[0] for f in functions]
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    if args.function:
        index = bisect.bisect_right(starts, args.rva) - 1
        if index < 0 or args.rva >= functions[index][1]:
            raise ValueError("RVA is not covered by the exception-function table")
        start, end, _ = functions[index]
        offset = file_offset(start)
        print(f"FUNCTION RVA {start:#x}..{end:#x}")
        for ins in decoder.disasm(data[offset:offset + end - start], start):
            print(f"  {ins.address:08X}: {ins.mnemonic:8} {ins.op_str}")
        return
    # RIP-relative MOV/LEA candidates; full function decoding below rejects
    # byte patterns that occur inside another instruction.
    pattern = rb"[\x48\x4c][\x8b\x8d\x89][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]"
    if args.calls:
        pattern = rb"[\xe8\xe9]"
    displacement_offset = 1 if args.calls else 3
    instruction_size = displacement_offset + 4
    found = set()
    for va, size, raw, flags in sections:
        if not flags & 0x20000000:
            continue
        for match in re.finditer(pattern, data[raw:raw + size]):
            index = match.start()
            if index + instruction_size > size:
                continue
            displacement = struct.unpack_from("<i", data, raw + index + displacement_offset)[0]
            instruction_rva = va + index
            if instruction_rva + instruction_size + displacement != args.rva:
                continue
            candidate = bisect.bisect_right(starts, instruction_rva) - 1
            if candidate < 0:
                continue
            start, end, _ = functions[candidate]
            if not start <= instruction_rva < end:
                continue
            code = data[file_offset(start):file_offset(start) + end - start]
            instructions = list(decoder.disasm(code, start))
            positions = [i for i, ins in enumerate(instructions) if ins.address == instruction_rva]
            if not positions or instruction_rva in found:
                continue
            found.add(instruction_rva)
            print(f"\nFUNCTION RVA {start:#x}..{end:#x}; reference {instruction_rva:#x}")
            position = positions[0]
            for ins in instructions[max(0, position - 8):position + 26]:
                mark = ">" if ins.address == instruction_rva else " "
                print(f"{mark} {ins.address:08X}: {ins.mnemonic:8} {ins.op_str}")
    print(f"\nValidated references: {len(found)}")


if __name__ == "__main__":
    main()
