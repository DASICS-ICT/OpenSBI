#!/usr/bin/env python3
"""Required final-link gate for the supported static DASICS_CALL interface."""

import argparse
import json
import sys

from elf_check import ELF, decode_jr, require, signed


def check_call(elf, call_site, target):
    require(elf.etype == 2, "DASICS_CALL requires static ET_EXEC; ET_DYN is unsupported")
    require(not any(s["name"] in (".dynamic", ".interp", ".dynsym") for s in elf.sections),
            "DASICS_CALL dynamic linking is unsupported")
    call, dest = elf.sym(call_site), elf.sym(target)
    require(call["shndx"] not in (0, 0xfff1) and call["shndx"] < len(elf.sections),
            "DASICS_CALL site must be a defined section symbol")
    require(elf.sections[call["shndx"]]["flags"] & 4,
            "DASICS_CALL site must be in an executable section")
    require(call["value"] % 2 == 0, "DASICS_CALL site must be aligned to two bytes")
    require(dest["shndx"] not in (0, 0xfff1) and dest["shndx"] < len(elf.sections),
            "DASICS_CALL target must be a defined section symbol")
    require(dest["info"] & 15 in (0, 2), "DASICS_CALL target must not be TLS or data object")
    require(elf.sections[dest["shndx"]]["flags"] & 4,
            "DASICS_CALL target must be in an executable section")
    require(dest["value"] % 2 == 0, "DASICS_CALL target must be aligned to two bytes")
    pc = call["value"]
    a, b, c = [int.from_bytes(elf.at(pc + i, 4), "little") for i in (0, 4, 8)]
    require(a & 0xfff == 0x297, "DASICS_CALL must start with AUIPC t0")
    require(b & 0xfffff == 0x28293, "DASICS_CALL must continue with ADDI t0,t0")
    require(decode_jr(c) == (1, 5, 0), "DASICS_CALL must end with FDICALL.JR ra,t0,0")
    address = (pc + signed(a & 0xfffff000, 32) + signed(b >> 20, 12)) & ((1 << 64) - 1)
    require(address == dest["value"], "DASICS_CALL linked address differs from target symbol")
    for relocation in elf.relocations():
        require(not pc <= relocation["offset"] < pc + 12,
                "DASICS_CALL executable must not retain call-site relocations")
    return {"call_site": call_site, "target": target, "pc": pc, "target_address": address,
            "instruction_words": ["0x%08x" % word for word in (a, b, c)],
            "length_bytes": 12, "fdicall_pc": pc + 8, "specified_link_pc": pc + 12}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True)
    parser.add_argument("--call-site", action="append", required=True)
    parser.add_argument("--target", action="append", required=True)
    args = parser.parse_args()
    try:
        require(len(args.call_site) == len(args.target), "each call-site requires one target")
        elf = ELF(args.elf)
        result = [check_call(elf, site, target) for site, target in zip(args.call_site, args.target)]
    except (AssertionError, OSError, ValueError, KeyError) as error:
        print("DASICS_CALL validation rejected: " + str(error), file=sys.stderr)
        return 1
    print(json.dumps({"status": "PASS", "calls": result}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
