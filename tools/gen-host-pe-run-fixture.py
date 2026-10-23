#!/usr/bin/env python3
"""Generate small PE32+ regression files for an AArch64/QEMU host-run smoke."""
import struct
import sys
from pathlib import Path

FILE_SIZE = 0x800
PE = 0x80
COFF = PE + 4
OPT = COFF + 20
TEXT_SECTION = OPT + 0xF0
IDATA_SECTION = TEXT_SECTION + 40
TEXT_RAW = 0x200
IDATA_RAW = 0x400
TEXT_RVA = 0x1000
IDATA_RVA = 0x2000
IAT_RVA = 0x2120


def u16(data, off, value):
    struct.pack_into("<H", data, off, value)


def u32(data, off, value):
    struct.pack_into("<I", data, off, value)


def u64(data, off, value):
    struct.pack_into("<Q", data, off, value)


def idata_off(rva):
    return IDATA_RAW + (rva - IDATA_RVA)


def build(kind):
    assert kind in ("exit0", "exit42", "exit42ecx", "exit42ecxclear", "loop")
    data = bytearray(FILE_SIZE)
    data[:2] = b"MZ"
    u32(data, 0x3C, PE)
    data[PE:PE + 4] = b"PE\0\0"
    u16(data, COFF, 0x8664)
    u16(data, COFF + 2, 2)
    u32(data, COFF + 4, 0x12345678)
    u16(data, COFF + 16, 0xF0)
    u16(data, COFF + 18, 0x22)
    u16(data, OPT, 0x20B)
    u32(data, OPT + 16, TEXT_RVA)
    u64(data, OPT + 24, 0x140000000)
    u32(data, OPT + 32, 0x1000)
    u32(data, OPT + 36, 0x200)
    u32(data, OPT + 56, 0x3000)
    u32(data, OPT + 60, 0x200)
    u16(data, OPT + 68, 3)
    u16(data, OPT + 70, 0x8160)
    u32(data, OPT + 108, 16)
    u32(data, OPT + 120, IDATA_RVA)
    u32(data, OPT + 124, 40)
    data[TEXT_SECTION:TEXT_SECTION + 5] = b".text"
    u32(data, TEXT_SECTION + 8, 0x200)
    u32(data, TEXT_SECTION + 12, TEXT_RVA)
    u32(data, TEXT_SECTION + 16, 0x200)
    u32(data, TEXT_SECTION + 20, TEXT_RAW)
    u32(data, TEXT_SECTION + 36, 0x60000020)
    data[IDATA_SECTION:IDATA_SECTION + 6] = b".idata"
    u32(data, IDATA_SECTION + 8, 0x1000)
    u32(data, IDATA_SECTION + 12, IDATA_RVA)
    u32(data, IDATA_SECTION + 16, 0x400)
    u32(data, IDATA_SECTION + 20, IDATA_RAW)
    u32(data, IDATA_SECTION + 36, 0xC0000040)

    if kind == "loop":
        code = bytes.fromhex("e9fbffffff")
    else:
        if kind == "exit0":
            argument = bytes.fromhex("31c9")
        elif kind == "exit42ecx":
            argument = bytes.fromhex("b92a000000")
        elif kind == "exit42ecxclear":
            argument = (bytes.fromhex("48b9") + b"\xff" * 8 +
                        bytes.fromhex("b92a000000"))
        else:
            argument = bytes.fromhex("48b9") + (42).to_bytes(8, "little")
        call_end_rva = TEXT_RVA + len(argument) + 6
        displacement = IAT_RVA - call_end_rva
        code = (argument + bytes.fromhex("ff15") +
                struct.pack("<i", displacement) + bytes.fromhex("cc"))
    assert 0 < len(code) <= 0x200
    data[TEXT_RAW:TEXT_RAW + len(code)] = code

    # One KERNEL32 import descriptor and one ExitProcess IAT slot.
    u32(data, idata_off(0x2000), 0x2100)
    u32(data, idata_off(0x2000) + 12, 0x2080)
    u32(data, idata_off(0x2000) + 16, IAT_RVA)
    data[idata_off(0x2080):idata_off(0x2080) + 13] = b"KERNEL32.dll\0"
    u64(data, idata_off(0x2100), 0x2200)
    u64(data, idata_off(IAT_RVA), 0x2200)
    data[idata_off(0x2202):idata_off(0x2202) + 12] = b"ExitProcess\0"
    assert len(data) == FILE_SIZE
    return data


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in (
        "exit0", "exit42", "exit42ecx", "exit42ecxclear", "loop"
    ):
        raise SystemExit(
            "usage: gen-host-pe-run-fixture.py "
            "{exit0|exit42|exit42ecx|exit42ecxclear|loop} OUT.exe"
        )
    Path(sys.argv[2]).write_bytes(build(sys.argv[1]))


if __name__ == "__main__":
    main()
