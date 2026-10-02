#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Derive the S01 C/assembler constants from an explicit A01 machine JSON."""

import argparse
import json
from pathlib import Path
import re
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def integer(value, name, low=0, high=(1 << 64) - 1):
    require(type(value) is int and low <= value <= high, f"invalid {name}")
    return value


def hex_integer(value, name, bits=64):
    require(isinstance(value, str) and re.fullmatch(r"0x[0-9a-fA-F]+", value),
            f"invalid hexadecimal {name}")
    return integer(int(value, 16), name, high=(1 << bits) - 1)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def upper_name(name):
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name).upper()


def csr_names():
    names = {
        "FDISMainCfg": "S_MAIN_CFG", "FDIUMainCfg": "U_MAIN_CFG",
        "FDISMainBoundLo": "S_MAIN_BOUND_LO", "FDISMainBoundHi": "S_MAIN_BOUND_HI",
        "FDIUMainBoundLo": "U_MAIN_BOUND_LO", "FDIUMainBoundHi": "U_MAIN_BOUND_HI",
        "FDILibCfg": "LIB_CFG", "FDIMainCallEntry": "MAIN_CALL_ENTRY",
        "FDIReturnPC": "RETURN_PC", "FDIActiveZoneReturnPC": "ACTIVE_ZONE_RETURN_PC",
        "FDIFReason": "FREASON", "FDIJumpCfg": "JUMP_CFG",
    }
    for bank, count in (("Lib", 16), ("Jump", 4)):
        for index in range(count):
            for edge in ("Lo", "Hi"):
                names[f"FDI{bank}Bound{edge}{index}"] = f"{bank.upper()}_BOUND_{edge.upper()}_{index}"
    return names


def validate(abi):
    # This generator supports the frozen v2 shape, not arbitrary future ISAs.
    require((abi["schema_version"], abi["abi_version"], abi["abi_id"], abi["status"])
            == (1, 2, "kunminghu-dasics-machine-v2", "frozen-specification"),
            "expected frozen A01 machine ABI v2")
    require(abi["xlen"] == 64, "S01 requires XLEN=64")
    names = csr_names()
    csrs = abi["csrs"]
    require(len(csrs) == abi["csr_address_count"] == len(names), "CSR address count mismatch")
    require({csr["name"] for csr in csrs} == names.keys(), "unsupported CSR names")
    require(len({csr["name"] for csr in csrs}) == len(csrs), "duplicate CSR name")
    addresses = [hex_integer(csr["address"], csr["name"], 12) for csr in csrs]
    require(len(set(addresses)) == len(addresses), "duplicate CSR address")
    backings = {csr["backing"] for csr in csrs}
    require(len(backings) == abi["csr_backing_count"] == len(csrs) - 1,
            "CSR backing count mismatch")
    for csr, address in zip(csrs, addresses):
        require(set(csr) == {"name", "address", "backing", "type", "min_privilege"},
                f"unsupported CSR shape: {csr['name']}")
        expected_backing = "FDIMainCfg" if csr["name"] in ("FDISMainCfg", "FDIUMainCfg") else csr["name"]
        require(csr["backing"] == expected_backing, "only S/U MainCfg may share backing")
        require(csr["min_privilege"] == (address >> 8) & 3, "CSR privilege encoding mismatch")
        require((address >> 10) != 3, "read-only CSR unsupported")
        require(csr["type"] in abi["csr_types"], "unknown CSR type")
    require(set(abi["csr_types"]) == {"main_s", "main_u", "bound", "lib_cfg", "jump_cfg", "pc", "reason"},
            "unsupported CSR types")
    for name, info in abi["csr_types"].items():
        require(info["width"] == abi["xlen"], f"invalid width: {name}")
        require(info["reserved"] == "read_zero_write_ignored", f"unsupported reserved behavior: {name}")
        read_mask = hex_integer(info["read_mask"], f"{name}.read_mask")
        write_mask = hex_integer(info["write_mask"], f"{name}.write_mask")
        reset = hex_integer(info["reset"], f"{name}.reset")
        require(reset & ~read_mask == 0 and read_mask == write_mask, f"unsupported masks: {name}")
    by_name = {csr["name"]: csr for csr in csrs}
    for bank, count in (("Lib", 16), ("Jump", 4)):
        info = abi[f"{bank.lower()}_cfg"]
        require(info["entries"] == count, f"unsupported {bank} entry count")
        require(info["entries"] * info["slot_bits"] == abi["xlen"], f"invalid {bank} slots")
        base = int(by_name[f"FDI{bank}BoundLo0"]["address"], 16)
        for index in range(count):
            for edge, delta in (("Lo", 0), ("Hi", 1)):
                csr = by_name[f"FDI{bank}Bound{edge}{index}"]
                require(csr["type"] == "bound" and int(csr["address"], 16) == base + 2 * index + delta,
                        f"noncontiguous {bank} bound addresses")
        fields = [value for key, value in info.items() if key.endswith("_bit")]
        require(set(fields).isdisjoint(info["reserved_bits"])
                and sorted(fields + info["reserved_bits"]) == list(range(info["slot_bits"])),
                f"invalid {bank} field coverage")
    main = abi["main_cfg_fields"]
    require({item["name"] for item in main} == {
        "sEnable", "uEnable", "sCloseEcall", "sCloseWrite", "sCloseRead", "sCloseJump",
        "uCloseEcall", "uCloseWrite", "uCloseRead", "uCloseJump", "uCloseUserInterrupt"},
        "unsupported MainCfg fields")
    require(len({item["bit"] for item in main}) == len(main), "duplicate MainCfg bit")
    for item in main:
        integer(item["bit"], item["name"], high=63)
    reason = abi["freason"]
    require(reason["software_values"] == list(range(1 << reason["width"])), "incomplete FReason values")
    require(set(reason["hardware_values"]) == {"ecall", "load", "store_amo", "jump"},
            "unsupported FReason hardware meanings")
    require(len(set(reason["hardware_values"].values())) == len(reason["hardware_values"]),
            "duplicate FReason hardware meaning")
    for value in reason["hardware_values"].values():
        require(value in reason["software_values"], "hardware FReason outside software range")
    insns = abi["instructions"]
    require(insns["length_bytes"] == 4, "unsupported instruction length")
    require(insns["j"]["immediate_slices"] == [[31, 31], [7, 7], [20, 15], [11, 8], [30, 21]],
            "unsupported FDICALL.J slice layout")
    require(insns["jr"]["immediate_slices"] == [[31, 20]] and insns["jr"]["immediate_bits"] == 12,
            "unsupported FDICALL.JR slice layout")
    for key in ("j", "jr"):
        insn = insns[key]
        mask = hex_integer(insn["mask"], f"{key}.mask", 32)
        match = hex_integer(insn["match"], f"{key}.match", 32)
        require(match & ~mask == 0 and ((match >> 12) & 7) == insn["funct3"], "instruction mask/match mismatch")
    require(insns["j"]["mask"] == insns["jr"]["mask"], "instruction masks differ")
    j = insns["j"]
    require(sum(hi - lo + 1 for hi, lo in j["immediate_slices"]) == j["min_bits"], "J width mismatch")
    sign_bit = j["min_bits"] + j["low_zero_bits"] - 1
    require(j["offset_min"] == -(1 << sign_bit)
            and j["offset_max"] == (1 << sign_bit) - (1 << j["low_zero_bits"]), "J offset range mismatch")
    for key in ("hu_cause", "hs_cause", "internal_hu_bit", "internal_hs_bit", "vector_width"):
        integer(abi["exceptions"][key], f"exceptions.{key}", high=63)
    for key in ("medeleg_mask", "hedeleg_mask", "medeleg_bits_reset", "ecall_tval"):
        hex_integer(abi["exceptions"][key], f"exceptions.{key}")


def generate(abi):
    validate(abi)
    lines = [
        "/* SPDX-License-Identifier: BSD-2-Clause */",
        "/*",
        " * Generated from the A01 config/dasics-machine-abi.json by",
        " * scripts/gen-dasics-abi.py. Do not edit: the JSON owns this ABI.",
        " * Regeneration requires an explicit --abi path; --check detects drift.",
        " * Constants do not imply implemented hardware or runtime capability.",
        " */",
        "#ifndef DASICS_ABI_H", "#define DASICS_ABI_H", "",
        "#ifdef __ASSEMBLER__", "#define DASICS_U64(value) value", "#else",
        "#define DASICS_U64(value) value##ULL", "#endif", "",
    ]

    emitted_names = set()

    def define(name, value):
        identifier = name.split("(", 1)[0]
        require(identifier not in emitted_names, f"duplicate generated macro: DASICS_{identifier}")
        emitted_names.add(identifier)
        lines.append(f"#define DASICS_{name} {value}")

    def u64(name, value):
        define(name, f"DASICS_U64(0x{value:016x})")

    def section(comment):
        lines.extend(["", f"/* {comment} */"])

    define("ABI_VERSION", abi["abi_version"])
    define("XLEN", abi["xlen"])
    define("CSR_ADDRESS_COUNT", abi["csr_address_count"])
    define("CSR_BACKING_COUNT", abi["csr_backing_count"])
    section("Type masks preserve full-width bound and special-PC semantics.")
    for name, info in abi["csr_types"].items():
        for field in ("read_mask", "write_mask", "reset"):
            u64(f"{name.upper()}_{field.upper()}", int(info[field], 16))
    section("Only S/U MainCfg share state; the U view still requires at least S privilege.")
    names = csr_names()
    for csr in abi["csrs"]:
        name = "CSR_" + names[csr["name"]]
        define(name, f"0x{int(csr['address'], 16):03x}")
        define(name + "_MIN_PRIVILEGE", csr["min_privilege"])
        for field in ("read_mask", "write_mask", "reset"):
            define(name + "_" + field.upper(), f"DASICS_{csr['type'].upper()}_{field.upper()}")
    section("MainCfg bits are in their backing positions, including the U view.")
    for item in abi["main_cfg_fields"]:
        name = "MAIN_CFG_" + upper_name(item["name"])
        define(name + "_BIT", item["bit"])
        u64(name + "_MASK", 1 << item["bit"])
    u64("MAIN_CFG_U_HIDDEN_MASK", int(abi["csr_types"]["main_s"]["write_mask"], 16)
        & ~int(abi["csr_types"]["main_u"]["write_mask"], 16))
    section("Explicit slot constants are usable in both C and preprocessed assembly.")
    for bank in ("lib", "jump"):
        cfg = abi[bank + "_cfg"]
        define(bank.upper() + "_ENTRIES", cfg["entries"])
        define(bank.upper() + "_CFG_SLOT_BITS", cfg["slot_bits"])
        for key, bit in cfg.items():
            if not key.endswith("_bit"):
                continue
            name = bank.upper() + "_CFG_" + key[:-4].upper()
            define(name + "_BIT", bit)
            u64(name + "_SLOT_MASK", 1 << bit)
            for index in range(cfg["entries"]):
                position = index * cfg["slot_bits"] + bit
                define(name + f"_{index}_BIT", position)
                u64(name + f"_{index}_MASK", 1 << position)
        u64(bank.upper() + "_CFG_SLOT_RESERVED_MASK", sum(1 << bit for bit in cfg["reserved_bits"]))
    section("Every low-three-bit FReason value is software-storable; reserved values have no hardware meaning.")
    reason = abi["freason"]
    define("FREASON_BITS", reason["width"])
    define("FREASON_SOFTWARE_MIN", min(reason["software_values"]))
    define("FREASON_SOFTWARE_MAX", max(reason["software_values"]))
    for value in reason["software_values"]:
        define(f"FREASON_VALUE_{value}", value)
    for meaning, value in reason["hardware_values"].items():
        define("FREASON_" + meaning.upper(), value)
    for value in reason["software_values"]:
        if value not in reason["hardware_values"].values():
            define("FREASON_" + ("CLEAR" if value == 0 else f"RESERVED_{value}"), value)
    section("Cause numbers and delegation masks do not perform trap setup.")
    ex = abi["exceptions"]
    for key in ("hu_cause", "hs_cause", "internal_hu_bit", "internal_hs_bit", "vector_width"):
        define("EXCEPTION_" + key.upper(), ex[key])
    for key in ("medeleg_mask", "hedeleg_mask", "medeleg_bits_reset", "ecall_tval"):
        u64("EXCEPTION_" + key.upper(), int(ex[key], 16))
    section("Custom instruction fields; J slices are not the standard JAL immediate.")
    insns = abi["instructions"]
    define("INSTRUCTION_LENGTH", insns["length_bytes"])
    define("FDICALL_MASK", insns["j"]["mask"])
    define("FDICALL_OPCODE", f"0x{int(insns['j']['match'], 16) & 0x7f:02x}")
    for key in ("j", "jr"):
        insn = insns[key]
        name = "FDICALL_" + key.upper()
        define(name + "_MATCH", insn["match"])
        define(name + "_FUNCT3", insn["funct3"])
        if key == "j":
            for field in ("min_bits", "low_zero_bits", "link_register", "offset_min", "offset_max"):
                value = insn[field]
                define(name + "_" + field.upper(), value)
            bits_left = insn["min_bits"] + insn["low_zero_bits"]
        else:
            bits_left = insn["immediate_bits"]
            define(name + "_IMMEDIATE_BITS", bits_left)
            define(name + "_OFFSET_MIN", -(1 << (bits_left - 1)))
            define(name + "_OFFSET_MAX", (1 << (bits_left - 1)) - 1)
        for index, (hi, lo) in enumerate(insn["immediate_slices"]):
            width = hi - lo + 1
            bits_left -= width
            for suffix, value in (("INST_HI", hi), ("INST_LO", lo), ("OFFSET_LO", bits_left), ("WIDTH", width)):
                define(name + f"_SLICE_{index}_" + suffix, value)
    section("GNU C index helpers require integer constant expressions; invalid indices cannot alias another CSR.")
    lines.extend([
        "#ifndef __ASSEMBLER__",
        "#define DASICS_INDEX_CHECK(index, count) \\",
        "\t(0 * sizeof(struct { int dasics_index_must_be_constant_and_in_range : \\",
        "\t\t(__builtin_constant_p(index) && ((index) | 0) >= 0 && (index) < (count)) ? 1 : -1; }))",
    ])
    for bank in ("LIB", "JUMP"):
        for edge in ("LO", "HI"):
            define(f"CSR_{bank}_BOUND_{edge}(index)", "\\")
            lines.append(f"\t(DASICS_CSR_{bank}_BOUND_{edge}_0 + 2 * (index) + DASICS_INDEX_CHECK(index, DASICS_{bank}_ENTRIES))")
        for field in ("WRITE", "READ", "VALID") if bank == "LIB" else ("VALID",):
            define(f"{bank}_CFG_{field}(index)", "\\")
            lines.append(f"\t((DASICS_U64(1) << (DASICS_{bank}_CFG_SLOT_BITS * (index) + DASICS_{bank}_CFG_{field}_BIT)) + DASICS_INDEX_CHECK(index, DASICS_{bank}_ENTRIES))")
    lines.extend(["#endif", "", "#endif /* DASICS_ABI_H */", ""])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--abi", required=True, type=Path, help="authoritative A01 machine JSON")
    parser.add_argument("--output", required=True, type=Path, help="generated abi.h path")
    parser.add_argument("--check", action="store_true", help="compare without writing")
    args = parser.parse_args()
    try:
        abi = json.loads(args.abi.read_text(), object_pairs_hook=unique_object)
        expected = generate(abi)
        if args.check:
            require(args.output.read_text() == expected,
                    f"generated header differs from machine JSON: {args.output}")
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(expected)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"DASICS ABI generation failed: {error}", file=sys.stderr)
        return 1
    print(f"DASICS ABI {'checked' if args.check else 'generated'}: {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
