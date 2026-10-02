#!/usr/bin/env python3
"""Compile and independently inspect the S01 software ABI without executing it."""

import argparse
import concurrent.futures
import datetime
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import threading
import traceback

from check_calls import check_call
from elf_check import ELF, csr_symbolic, decode_csr, decode_j, decode_jr, require, signed, verifier_selftest, words
from expected import constants, csr_map


class Runner:
    def __init__(self, args):
        self.out = Path(args.out).resolve()
        self.out.mkdir(parents=True, exist_ok=False)
        self.inputs = self.out / "inputs"
        self.inputs.mkdir()
        self.logs = self.out / "commands"
        self.logs.mkdir()
        source = Path(__file__).resolve().parents[2]
        shutil.copytree(source / "include/dasics", self.inputs / "include/dasics")
        shutil.copytree(Path(__file__).resolve().parent, self.inputs / "tests",
                        ignore=shutil.ignore_patterns("__pycache__"))
        shutil.copy2(source / "scripts/gen-dasics-abi.py", self.inputs / "gen-dasics-abi.py")
        shutil.copy2(args.abi, self.inputs / "abi.json")
        self.cross = args.cross_compile
        self.jobs = args.jobs
        self.semaphore = threading.Semaphore(args.jobs)
        self.lock = threading.Lock()
        self.command_count = 0
        self.commands = []
        self.results = {}
        self.failures = {}
        self.common = ["-march=rv64ima_zicsr", "-mabi=lp64", "-mcmodel=medany",
                       "-I" + str(self.inputs / "include"), "-ffreestanding", "-fno-pic", "-fno-pie",
                       "-fno-stack-protector", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
                       "-Wa,--fatal-warnings"]
        self.linker = self.inputs / "link.ld"
        self.linker.write_text("OUTPUT_ARCH(riscv)\nENTRY(_start)\nSECTIONS {\n"
            " . = 0x80000000; .text : { *(.text.start) *(.text) *(.text.*) }\n"
            " . = ALIGN(4096); .rodata : { *(.rodata*) }\n"
            " . = ALIGN(4096); .data : { *(.data*) *(.sdata*) }\n"
            " .bss : { *(.bss*) *(.sbss*) }\n /DISCARD/ : { *(.comment) }\n}\n")
        (self.out / "invocation.json").write_text(json.dumps(vars(args), indent=2) + "\n")

    def command(self, argv, label, reject=None):
        with self.lock:
            self.command_count += 1
            number = self.command_count
        start = datetime.datetime.now(datetime.timezone.utc).isoformat()
        with self.semaphore:
            result = subprocess.run([str(x) for x in argv], text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, cwd=self.out, env={**os.environ, "LC_ALL": "C"})
        end = datetime.datetime.now(datetime.timezone.utc).isoformat()
        name = "%04d-%s" % (number, re.sub(r"[^a-zA-Z0-9_.-]", "_", label))
        (self.logs / (name + ".stdout")).write_text(result.stdout)
        (self.logs / (name + ".stderr")).write_text(result.stderr)
        record = {"argv": [str(x) for x in argv], "shell_display": shlex.join([str(x) for x in argv]),
                  "cwd": str(self.out), "start_utc": start, "end_utc": end, "exit_code": result.returncode,
                  "expected_rejection": reject, "stdout": "commands/" + name + ".stdout",
                  "stderr": "commands/" + name + ".stderr"}
        (self.logs / (name + ".json")).write_text(json.dumps(record, indent=2) + "\n")
        with self.lock:
            self.commands.append(record)
        if reject is None:
            require(result.returncode == 0, "%s exited %d; see %s" % (label, result.returncode, name))
        else:
            require(result.returncode != 0, label + " unexpectedly succeeded")
            require(re.search(reject, result.stderr + result.stdout, re.I),
                    label + " failed for an unrelated reason; see " + name)
        return result

    def work(self, name):
        path = self.out / name
        path.mkdir()
        return path

    def assembly(self, directory, name, content, reject=None):
        source = directory / (name + ".S")
        source.write_text(content)
        preprocessed = directory / (name + ".s")
        self.command([self.cross + "gcc", *self.common, "-E", "-P", "-x", "assembler-with-cpp",
                      source, "-o", preprocessed], name + "-preprocess")
        obj = directory / (name + ".o")
        self.command([self.cross + "gcc", *self.common, "-c", "-x", "assembler", preprocessed, "-o", obj],
                     name + "-assemble", reject=reject)
        return obj

    def c(self, directory, name, content, optimization="-O2", lto=False, reject=None):
        source = directory / (name + ".c")
        source.write_text(content)
        preprocessed = directory / (name + ".i")
        flags = [*self.common, "-std=gnu11", "-Wall", "-Wextra", "-Werror", optimization]
        if lto:
            flags.append("-flto")
        self.command([self.cross + "gcc", *flags, "-E", source, "-o", preprocessed], name + "-preprocess")
        obj = directory / (name + ".o")
        self.command([self.cross + "gcc", *flags, "-c", preprocessed, "-o", obj], name + "-compile", reject=reject)
        return obj

    def link(self, directory, name, objects, relax=False, lto=False, script=None, reject=None, extra=()):
        output = directory / (name + ".elf")
        argv = [self.cross + "gcc", *self.common, "-nostdlib", "-static", "-no-pie"]
        if lto:
            argv.extend(["-flto", "-O2"])
        argv += ["-Wl,--build-id=none", "-Wl,--relax" if relax else "-Wl,--no-relax",
                 "-Wl,-T," + str(script or self.linker), *extra, *objects, "-o", output]
        self.command(argv, name + "-link", reject=reject)
        if reject is None:
            self.command([self.cross + "objdump", "-drw", "-M", "no-aliases", output], name + "-objdump")
            self.command([self.cross + "readelf", "-hSWsr", output], name + "-readelf")
        return output

    def save(self, directory, name, value):
        (directory / (name + ".json")).write_text(json.dumps(value, indent=2) + "\n")

    def test_constants(self):
        directory = self.work("constants")
        self.command([sys.executable, "-B", self.inputs / "gen-dasics-abi.py", "--abi", self.inputs / "abi.json",
                      "--output", self.inputs / "include/dasics/abi.h", "--check"], "generator-check")
        values = constants()
        c = '#include <dasics/abi.h>\n'
        for name, value in values.items():
            literal = "(-%dLL)" % -value if value < 0 else "0x%xULL" % value
            c += '_Static_assert(%s == %s, "%s value");\n' % (name, literal, name)
            if name.endswith(("_MASK", "_RESET", "_TVAL")) and name != "DASICS_FDICALL_MASK":
                c += '_Static_assert(_Generic((%s), unsigned long long: 1, default: 0), "%s type");\n' % (name, name)
        indexed = {}
        for group, base, count, stride in (("LIB", 0x890, 16, 4), ("JUMP", 0x8c0, 4, 16)):
            for index in range(count):
                for side, increment in (("LO", 0), ("HI", 1)):
                    indexed["DASICS_CSR_%s_BOUND_%s(%d)" % (group, side, index)] = base + 2 * index + increment
                fields = (("WRITE", 0), ("READ", 1), ("VALID", 3)) if group == "LIB" else (("VALID", 0),)
                for field, bit in fields:
                    indexed["DASICS_%s_CFG_%s(%d)" % (group, field, index)] = 1 << (stride * index + bit)
        for expression, value in indexed.items():
            c += '_Static_assert(%s == 0x%xULL, "indexed value");\n' % (expression, value)
        c += 'const unsigned long long abi_c[] = {\n' + ',\n'.join(values) + '\n};\n'
        c += 'const unsigned long long abi_indexed[] = {\n' + ',\n'.join(indexed) + '\n};\nvoid _start(void) {}\n'
        cobj = self.c(directory, "constants", c)
        assembly = '#include <dasics/asm.h>\n.section .rodata\n.balign 8\n.global abi_asm\nabi_asm:\n'
        assembly += ''.join('.quad %s\n' % name for name in values)
        assembly += '.global abi_asm_indexed\nabi_asm_indexed:\n'
        for expression in indexed:
            macro, index = expression[:-1].split('(')
            macro = macro.replace('DASICS_CSR_', 'DASICS_')
            assembly += '%s selected_index,%s\n.quad selected_index\n' % (macro, index)
        aobj = self.assembly(directory, "constants_asm", assembly)
        elf = ELF(self.link(directory, "constants", [cobj, aobj]))
        expected = b''.join((value & ((1 << 64) - 1)).to_bytes(8, "little") for value in values.values())
        require(elf.symbol_data("abi_c", len(expected)) == expected, "C constants differ from fixed ABI")
        require(elf.symbol_data("abi_asm", len(expected)) == expected, ".S constants differ from fixed ABI")
        indexed_bytes = b''.join(value.to_bytes(8, "little") for value in indexed.values())
        require(elf.symbol_data("abi_indexed", len(indexed_bytes)) == indexed_bytes, "indexed constants mismatch")
        require(elf.symbol_data("abi_asm_indexed", len(indexed_bytes)) == indexed_bytes, "assembly indexed constants mismatch")
        abi = json.loads((self.inputs / "abi.json").read_text())
        addresses = [int(item["address"], 16) for item in abi["csrs"]]
        require(set(addresses) == {entry[0] for entry in csr_map().values()} and len(addresses) == 52,
                "JSON address list differs from independent 52-address table")
        groups = {}
        for item in abi["csrs"]:
            groups.setdefault(item["backing"], []).append(item["name"])
        require(len(groups) == 51 and [set(g) for g in groups.values() if len(g) > 1] ==
                [{"FDISMainCfg", "FDIUMainCfg"}], "backing alias contract mismatch")
        require(0xbc3 not in addresses and 0x9e0 not in addresses, "legacy CSR address leaked")
        result = {"fixed_constants": len(values), "indexed_constants": len(indexed), "addresses": 52,
                  "backings": 51, "c_and_assembly_bytes_match": True, "constant_values": values}
        self.save(directory, "checks", result)
        return result

    def test_csr(self, optimization, lto=False):
        label = "csr-" + optimization.removeprefix("-") + ("-lto" if lto else "")
        directory = self.work(label)
        source = '#include <dasics/csr.h>\n#define KEEP __attribute__((noinline, noipa, used, externally_visible))\n'
        source += 'KEEP unsigned long csr_read(void) { return dasics_csr_read(DASICS_CSR_FREASON); }\n'
        cases = {"csr_read": (2, "zero", False)}
        helpers = (("write", 1, False), ("swap", 1, True), ("set", 2, False),
                   ("clear", 3, False), ("read_set", 2, True), ("read_clear", 3, True))
        for helper, funct3, returns in helpers:
            for value in (0, 17):
                name = "csr_%s_%d" % (helper, value)
                source += 'KEEP unsigned long %s(void) { %sdasics_csr_%s(DASICS_CSR_FREASON, %d); %s }\n' % (
                    name, "return " if returns else "", helper, value, "" if returns else "return 0;")
                cases[name] = (funct3, value, returns)
            for value in (0, 31):
                name = "csr_%s_imm_%d" % (helper, value)
                source += 'KEEP unsigned long %s(void) { %sdasics_csr_%s_imm(DASICS_CSR_FREASON, %d); %s }\n' % (
                    name, "return " if returns else "", helper, value, "" if returns else "return 0;")
                cases[name] = (funct3 + 4, value, returns)
            if returns:
                name = "csr_%s_ignored" % helper
                source += 'KEEP void %s(void) { (void)dasics_csr_%s(DASICS_CSR_FREASON, 0); }\n' % (name, helper)
                cases[name] = (funct3, 0, True)
        source += 'KEEP unsigned long source_once(volatile unsigned long *p) { unsigned long v = *p; *p = v + 1; return v; }\n'
        for helper, _, returns in helpers:
            source += 'KEEP unsigned long eval_%s(volatile unsigned long *p) { %sdasics_csr_%s(DASICS_CSR_FREASON, source_once(p)); %s }\n' % (
                helper, "return " if returns else "", helper, "" if returns else "return 0;")
        source += 'KEEP void csr_barrier(unsigned long *p) { *p=1; dasics_csr_set(DASICS_CSR_FREASON,0); *p=2; }\n'
        source += 'KEEP unsigned long csr_nested(void) { return dasics_csr_swap(DASICS_CSR_FREASON, dasics_csr_read(DASICS_CSR_FREASON)); }\n'
        address_cases = {"csr_address_zero": ("0", 0), "csr_address_max": ("4095", 4095),
                         "csr_address_lib15": ("DASICS_CSR_LIB_BOUND_LO(15)", 0x8ae),
                         "csr_address_jump3": ("DASICS_CSR_JUMP_BOUND_HI(3)", 0x8c7)}
        for name, (expression, _) in address_cases.items():
            source += 'KEEP unsigned long %s(void) { return dasics_csr_read(%s); }\n' % (name, expression)
        source += 'void _start(void) {}\n'
        obj = self.c(directory, label, source, optimization, lto)
        elf = ELF(self.link(directory, label, [obj], lto=lto))
        checks = {}
        for name, (funct3, value, returns) in cases.items():
            instructions = words(elf.symbol_data(name))
            csrs = csr_symbolic(instructions)
            require(len(csrs) == 1, name + " must contain exactly one CSR instruction")
            item = csrs[0]
            require(item["funct3"] == funct3 and item["csr"] == 0x8b3, name + " opcode/address mismatch")
            if value == "zero":
                require(item["source"] == 0 and item["rd"] != 0, "read must be pure CSRRS x0 source")
            else:
                require(item["source_value"] == value, name + " source value cannot be established independently")
                if funct3 in (2, 3):
                    require(item["source"] != 0, name + " must encode nonzero source register")
                if funct3 >= 5:
                    require(item["source"] == value, name + " zimm mismatch")
                require((item["rd"] != 0) == returns, name + " rd read/discard contract mismatch")
            checks[name] = item
        for name, (_, address) in address_cases.items():
            actual = csr_symbolic(words(elf.symbol_data(name)))
            require(len(actual) == 1 and actual[0]["csr"] == address and actual[0]["funct3"] == 2 and
                    actual[0]["source"] == 0 and actual[0]["rd"] != 0,
                    name + " immediate CSR address mismatch")
            checks[name] = actual[0]
        target = elf.sym("source_once")["value"]
        for helper, _, _ in helpers:
            name = "eval_" + helper
            data = words(elf.symbol_data(name))
            calls = 0
            pc = elf.sym(name)["value"]
            for index, word in enumerate(data):
                if word & 0x7f == 0x67 and word >> 7 & 31 == 1 and index:
                    previous = data[index - 1]
                    if previous & 0x7f == 0x17 and previous >> 7 & 31 == word >> 15 & 31:
                        address = pc + 4 * (index - 1) + signed(previous & 0xfffff000, 32) + signed(word >> 20, 12)
                        calls += address == target
            require(calls == 1, name + " must evaluate operand exactly once")
            checks[name] = {"source_once_calls": calls}
        nested = csr_symbolic(words(elf.symbol_data("csr_nested")))
        require([c["funct3"] for c in nested] == [2, 1], "nested helper order mismatch")
        barrier = words(elf.symbol_data("csr_barrier"))
        csr_index = next(i for i, word in enumerate(barrier) if word & 0x7f == 0x73)
        stores = [i for i, word in enumerate(barrier) if word & 0x7f == 0x23]
        require(any(i < csr_index for i in stores) and any(i > csr_index for i in stores),
                "memory compiler barrier did not preserve bracketing stores")
        checks["memory_barrier"] = {"csr_index": csr_index, "store_indices": stores,
                                    "processor_fence_claimed": False}
        self.save(directory, "checks", checks)
        return {"functions_checked": len(cases) + len(address_cases), "single_evaluation_helpers": len(helpers),
                "optimization": optimization, "lto": lto}

    def test_assembly(self):
        directory = self.work("assembly")
        source = '#include <dasics/asm.h>\n.section .text\n.option norvc\n.global _start\n_start:\n'
        j_offsets = sorted(set([0, 2, -2, -4194304, 4194302] +
                              [sign * (1 << bit) for bit in range(1, 22) for sign in (-1, 1)]))
        j_cases = {}
        for index, offset in enumerate(j_offsets):
            name = "j_%d" % index
            source += '.global %s\n%s: DASICS_FDICALL_J_OFFSET %d\n.size %s, .-%s\n' % (name, name, offset, name, name)
            j_cases[name] = offset
        source += '.global j_min_macro\nj_min_macro: DASICS_FDICALL_J_OFFSET DASICS_FDICALL_J_OFFSET_MIN\n'
        j_cases["j_min_macro"] = -4194304
        for name, literal, offset in (("j_hex_negative", "-0X2", -2), ("j_hex_positive", "+0x2", 2),
                                      ("j_leading_decimal", "000010", 10)):
            source += '.global %s\n%s: DASICS_FDICALL_J_OFFSET %s\n.size %s, .-%s\n' % (
                name, name, literal, name, name)
            j_cases[name] = offset
        jr_cases = {}
        for rd in range(32):
            for rs1 in range(32):
                offset = (-2048, -1, 0, 1, 2047)[(rd * 32 + rs1) % 5]
                name = "jr_%d_%d" % (rd, rs1)
                source += '.global %s\n%s: DASICS_FDICALL_JR x%d,x%d,%d\n.size %s, .-%s\n' % (
                    name, name, rd, rs1, offset, name, name)
                jr_cases[name] = (rd, rs1, offset)
        source += '.global jr_min_macro\njr_min_macro: DASICS_FDICALL_JR zero,zero,DASICS_FDICALL_JR_OFFSET_MIN\n'
        jr_cases["jr_min_macro"] = (0, 0, -2048)
        source += '.global jr_max_fixed\njr_max_fixed: DASICS_FDICALL_JR t6,t6,2047\n'
        jr_cases["jr_max_fixed"] = (31, 31, 2047)
        for name, literal, offset in (("jr_hex_negative", "-0X2", -2), ("jr_hex_positive", "+0x2", 2),
                                      ("jr_leading_decimal", "000010", 10)):
            source += '.global %s\n%s: DASICS_FDICALL_JR ra,t0,%s\n.size %s, .-%s\n' % (
                name, name, literal, name, name)
            jr_cases[name] = (1, 5, offset)
        csr_cases = {}
        for kind, funct3 in (("RW", 1), ("RS", 2), ("RC", 3), ("RWI", 5), ("RSI", 6), ("RCI", 7)):
            for rd in (0, 1, 31):
                for source_number in (0, 5, 31):
                    name = "a_csr_%s_%d_%d" % (kind, rd, source_number)
                    operand = str(source_number) if funct3 > 3 else "x%d" % source_number
                    source += '.global %s\n%s: DASICS_CSR_%s x%d,DASICS_CSR_FREASON,%s\n' % (
                        name, name, kind, rd, operand)
                    csr_cases[name] = (funct3, rd, source_number)
        source += '.option rvc\n.global compressed_before\ncompressed_before: c.nop\n'
        source += '.global relative_j\nrelative_j: DASICS_FDICALL_J_OFFSET 6\n'
        source += '.global compressed_after\ncompressed_after: c.nop\n.global relative_target\nrelative_target: c.nop\n'
        obj = self.assembly(directory, "raw", source)
        elf = ELF(self.link(directory, "raw", [obj]))
        checks = {}
        fixed = {0: 0x0000000b, 2: 0x0020000b, -2: 0xffff8f8b, 10: 0x00a0000b,
                 -4194304: 0x8000000b, 4194302: 0x7fff8f8b}
        for name, offset in j_cases.items():
            word = elf.word(name)
            require(decode_j(word) == offset, name + " J offset mismatch")
            if offset in fixed:
                require(word == fixed[offset], name + " fixed J word mismatch")
            if name != "j_min_macro":
                require(elf.sym(name)["size"] == 4, name + " must be 32 bits")
            checks[name] = {"word": "0x%08x" % word, "offset": offset, "pc": elf.sym(name)["value"]}
        for name, expected in jr_cases.items():
            word = elf.word(name)
            require(decode_jr(word) == expected, name + " JR decode mismatch")
            if name.startswith("jr_") and name not in ("jr_min_macro", "jr_max_fixed"):
                require(elf.sym(name)["size"] == 4, name + " must be 32 bits")
            checks[name] = {"word": "0x%08x" % word, "rd": expected[0], "rs1": expected[1], "offset": expected[2]}
        require(elf.word("jr_min_macro") == 0x8000100b and elf.word("jr_max_fixed") == 0x7fff9f8b,
                "JR fixed word mismatch")
        for name, expected in (("jr_hex_negative", 0xffe2908b), ("jr_hex_positive", 0x0022908b),
                               ("jr_leading_decimal", 0x00a2908b)):
            require(elf.word(name) == expected, name + " fixed literal word mismatch")
        for name, (funct3, rd, source_number) in csr_cases.items():
            actual = decode_csr(elf.word(name))
            require(actual == {"funct3": funct3, "rd": rd, "source": source_number, "csr": 0x8b3},
                    name + " assembly CSR encoding mismatch")
            checks[name] = actual
        pc = elf.sym("relative_j")["value"]
        require(pc % 4 == 2, "compressed neighbor fixture must put J at halfword address")
        require(elf.sym("compressed_after")["value"] == pc + 4, "J instruction width changed")
        require(elf.symbol_data("compressed_before", 2) == b'\x01\x00' and
                elf.symbol_data("compressed_after", 2) == b'\x01\x00', "compressed adjacency bytes changed")
        require(pc + decode_j(elf.word("relative_j")) == elf.sym("relative_target")["value"],
                "J displacement must be relative to the instruction's own PC")
        require(not ELF(obj).relocations(), "raw encoders must not create relocations")
        self.save(directory, "checks", checks)
        return {"j_cases": len(j_cases), "jr_register_pairs": 1024, "jr_cases": len(jr_cases),
                "csr_encoding_cases": len(csr_cases), "compressed_adjacency": True}

    def test_calls(self):
        directory = self.work("calls")
        source = '#include <dasics/asm.h>\n.section .text\n.option rvc\n.global _start\n_start: c.nop\n'
        source += '.global backward\n.type backward,@function\nbackward: c.nop\n'
        source += '.option relax\n.global ordinary_call\nordinary_call: call ordinary_target\n'
        source += '.global ordinary_after\nordinary_after: c.nop\n'
        pairs = [("call_forward", "forward"), ("call_backward", "backward"),
                 ("call_section", "other_section"), ("call_file", "other_file")]
        for site, target in pairs:
            source += '.global %s\n%s: DASICS_CALL %s\n.size %s, .-%s\nc.nop\n' % (site, site, target, site, site)
        source += '.global forward\n.type forward,@function\nforward: c.nop\n'
        source += '.global ordinary_target\n.hidden ordinary_target\n.type ordinary_target,@function\nordinary_target: c.nop\n'
        source += '.section .text.other,"ax",@progbits\n.global other_section\n.type other_section,@function\nother_section: c.nop\n'
        source += '.hidden other_file\n'
        obj = self.assembly(directory, "callers", source)
        target = self.assembly(directory, "callee", '.section .text.external,"ax",@progbits\n.option rvc\n'
            '.global other_file\n.hidden other_file\n.type other_file,@function\nother_file: c.nop\n')
        relocations = ELF(obj).relocations()
        call_relocations = []
        for site, _ in pairs:
            offset = ELF(obj).sym(site)["value"]
            selected = [r for r in relocations if r["target_section"] == ".text" and offset <= r["offset"] < offset + 12]
            require(len(selected) == 2 and {r["type"] for r in selected} == {23, 24},
                    "controlled calls must use only PCREL_HI20 and PCREL_LO12_I relocations")
            require({r["offset"] for r in selected} == {offset, offset + 4},
                    "relocations must belong to AUIPC/ADDI, never FDICALL")
            call_relocations.extend(selected)
        require(any(r["type"] == 51 for r in relocations), "ordinary neighbor must have an actual relaxation relocation")
        script = directory / "calls.ld"
        script.write_text('OUTPUT_ARCH(riscv)\nENTRY(_start)\nSECTIONS {\n'
                          ' . = 0x80000000; .text : { *(.text) *(.text.external) }\n'
                          ' . = 0x80002880; .text.distant : { *(.text.other) }\n}\n')
        results = {}
        layouts = {}
        for relax in (False, True):
            name = "calls-relax" if relax else "calls-no-relax"
            path = self.link(directory, name, [obj, target], relax=relax, script=script)
            elf = ELF(path)
            results[name] = [check_call(elf, site, symbol) for site, symbol in pairs]
            require(all(elf.sym(site)["size"] == 12 for site, _ in pairs), "controlled call length changed")
            distance = elf.sym("ordinary_after")["value"] - elf.sym("ordinary_call")["value"]
            require(distance == (4 if relax else 8), "ordinary neighbor did not exhibit the expected relaxation")
            section_call = next(item for item in results[name] if item["call_site"] == "call_section")
            hi_word, lo_word = [int(word, 16) for word in section_call["instruction_words"][:2]]
            require(hi_word & 0xfffff000 != 0 and signed(lo_word >> 20, 12) < 0,
                    "distant target must exercise a nonzero HI20 and negative LO12")
            require(elf.sym("other_section")["value"] == 0x80002880 and
                    elf.sym("other_section")["shndx"] != elf.sym("call_section")["shndx"],
                    "distant target must retain its distinct output section and fixed address")
            layouts[name] = {"ordinary_call_bytes": distance, "controlled_call_pc": elf.sym("call_section")["value"],
                             "distant_target": elf.sym("other_section")["value"],
                             "hi20_value": signed(hi_word & 0xfffff000, 32), "lo12_value": signed(lo_word >> 20, 12)}
            self.command([sys.executable, "-B", self.inputs / "tests/check_calls.py", "--elf", path,
                          *[item for site, symbol in pairs for item in ("--call-site", site, "--target", symbol)]],
                         name + "-independent-gate")
        require(layouts["calls-relax"]["controlled_call_pc"] != layouts["calls-no-relax"]["controlled_call_pc"],
                "relaxation must move a controlled call in the final layout")
        self.save(directory, "checks", {"relocations": relocations, "controlled_call_relocations": call_relocations,
                                        "linked": results, "layouts": layouts})
        return {"static_symbol_calls": len(pairs), "relaxation_modes": 2, "relocation_types": [23, 24],
                "ordinary_neighbor_shrinks": True, "nonzero_hi20_negative_lo12": True, "layouts": layouts}

    def test_negative_c(self):
        directory = self.work("negative-c")
        csr_diagnostic = r"DASICS CSR (address|immediate) must be an integer constant"
        cases = {
            "csr_negative": "dasics_csr_read(-1)", "csr_large": "dasics_csr_read(4096)",
            "csr_runtime": "dasics_csr_read(value)", "csr_float": "dasics_csr_read(1.5)",
            "csr_pointer": "dasics_csr_read((void *)0)",
            "zimm_negative": "dasics_csr_set_imm(0x8b3,-1)",
            "zimm_large": "dasics_csr_set_imm(0x8b3,32)",
            "zimm_runtime": "dasics_csr_set_imm(0x8b3,value)",
            "zimm_float": "dasics_csr_set_imm(0x8b3,1.5)",
            "zimm_pointer": "dasics_csr_set_imm(0x8b3,(void *)0)"}
        indexes = {}
        for group, count in (("LIB", 16), ("JUMP", 4)):
            helpers = ["DASICS_CSR_%s_BOUND_LO" % group, "DASICS_CSR_%s_BOUND_HI" % group,
                       "DASICS_%s_CFG_VALID" % group]
            if group == "LIB":
                helpers += ["DASICS_LIB_CFG_WRITE", "DASICS_LIB_CFG_READ"]
            for helper in helpers:
                for tag, value in (("negative", "-1"), ("large", str(count)), ("runtime", "value"),
                                   ("fractional", "0.5"), ("pointer", "(void *)0")):
                    indexes[helper.lower() + "_" + tag] = "%s(%s)" % (helper, value)
        for optimization in ("-O0", "-O2"):
            for name, expression in cases.items():
                content = '#include <dasics/csr.h>\nvoid bad(unsigned long value) { (void)value; %s; }\n' % expression
                diagnostic = csr_diagnostic
                if name in ("csr_runtime", "zimm_runtime"):
                    diagnostic += r"|expression in static assertion is not constant"
                self.c(directory, name + optimization, content, optimization, reject=diagnostic)
            for name, expression in indexes.items():
                content = '#include <dasics/abi.h>\nunsigned long bad(unsigned long value) { (void)value; return %s; }\n' % expression
                self.c(directory, name + optimization, content, optimization,
                       reject=r"dasics_index_must_be_constant_and_in_range|invalid operands to binary")
        return {"compiler_rejections": (len(cases) + len(indexes)) * 2, "optimization_modes": 2}

    def test_negative_assembly(self):
        directory = self.work("negative-assembly")
        cases = {
            "j_odd_pos": ("DASICS_FDICALL_J_OFFSET 1", "must be even"),
            "j_odd_neg": ("DASICS_FDICALL_J_OFFSET -1", "must be even"),
            "j_high": ("DASICS_FDICALL_J_OFFSET 4194304", "out of range"),
            "j_low": ("DASICS_FDICALL_J_OFFSET -4194306", "out of range"),
            "j_huge": ("DASICS_FDICALL_J_OFFSET 0x10000000000000000", "literal|out of range|too large|bignum"),
            "j_label": ("DASICS_FDICALL_J_OFFSET target", "literal"),
            "j_difference": ("DASICS_FDICALL_J_OFFSET target-.", "literal"),
            "j_expression": ("DASICS_FDICALL_J_OFFSET 2+2", "literal"),
            "j_parentheses": ("DASICS_FDICALL_J_OFFSET (2)", "literal"),
            "jr_high": ("DASICS_FDICALL_JR ra,t0,2048", "out of range"),
            "jr_low": ("DASICS_FDICALL_JR ra,t0,-2049", "out of range"),
            "jr_label": ("DASICS_FDICALL_JR ra,t0,target", "literal"),
            "jr_fp_rd": ("DASICS_FDICALL_JR f0,t0,0", "integer GPR"),
            "jr_fp_rs": ("DASICS_FDICALL_JR ra,fa0,0", "integer GPR"),
            "jr_x32": ("DASICS_FDICALL_JR x32,t0,0", "integer GPR"),
            "csr_low": ("DASICS_CSR_RW x0,-1,x1", "CSR.*range|CSR.*4095"),
            "csr_high": ("DASICS_CSR_RW x0,4096,x1", "CSR.*range|CSR.*4095"),
            "csr_reg": ("DASICS_CSR_RS x1,0x8b3,f0", "integer GPR|illegal operands|instruction csrrs requires absolute expression"),
            "csr_huge": ("DASICS_CSR_RW x0,0x100000000000008b3,x1", "bignum|too large|constant.*4095"),
            "csr_runtime": ("DASICS_CSR_RW x0,t0,x1", "non-constant|illegal operands"),
            "zimm_low": ("DASICS_CSR_RSI x1,0x8b3,-1", "immediate.*range|immediate.*31|zimm"),
            "zimm_high": ("DASICS_CSR_RCI x1,0x8b3,32", "immediate.*range|immediate.*31|zimm"),
            "zimm_huge": ("DASICS_CSR_RSI x1,0x8b3,0x10000000000000001", "bignum|too large|immediate.*31"),
            "lib_huge": ("DASICS_LIB_BOUND_LO invalid,0x10000000000000000", "literal.*range"),
            "lib_symbol": ("DASICS_LIB_BOUND_LO invalid,target", "literal"),
            "lib_index": ("DASICS_LIB_BOUND_LO invalid,16", "index.*range"),
            "lib_negative": ("DASICS_LIB_BOUND_HI invalid,-1", "index.*range"),
            "jump_index": ("DASICS_JUMP_BOUND_LO invalid,4", "index.*range"),
            "jump_negative": ("DASICS_JUMP_BOUND_HI invalid,-1", "index.*range")}
        for name, (body, diagnostic) in cases.items():
            content = '#include <dasics/asm.h>\n.text\n%s\ntarget: nop\n' % body
            self.assembly(directory, name, content, reject=diagnostic)
        return {"assembler_rejections": len(cases)}

    def test_negative_links(self):
        directory = self.work("negative-links")
        header = '#include <dasics/asm.h>\n.text\n.global _start\n_start:\n.global call\ncall: DASICS_CALL target\n'
        missing = self.assembly(directory, "undefined", header)
        self.link(directory, "undefined", [missing], reject="undefined reference.*target")
        far_script = directory / "far.ld"
        far_script.write_text('OUTPUT_ARCH(riscv)\nENTRY(_start)\nSECTIONS { . = 0x10000; .text : { *(.text) } '
                              '. = 0x90010000; .far : { *(.far) } }\n')
        far = self.assembly(directory, "far", '.section .far,"ax",@progbits\n.global target\n.type target,@function\ntarget: nop\n')
        self.link(directory, "far", [missing, far], script=far_script, reject="relocation truncated to fit.*R_RISCV_PCREL_HI20")
        for kind, section, symbol_type, padding, diagnostic in (
                ("object", '.section .text.data,"ax",@progbits', "object", "", "TLS or data object"),
                ("tls", '.section .tdata,"awT",@progbits', "tls_object", "", "TLS or data object"),
                ("odd", '.section .text.odd,"ax",@progbits', "function", ".byte 0\n", "aligned to two bytes")):
            content = header + section + '\n' + padding + '.global target\n.type target,@%s\ntarget: .word 0x13\n' % symbol_type
            obj = self.assembly(directory, kind, content)
            path = self.link(directory, kind, [obj])
            self.command([sys.executable, "-B", self.inputs / "tests/check_calls.py", "--elf", path,
                          "--call-site", "call", "--target", "target"], kind + "-gate", reject=diagnostic)
        for kind, content, diagnostic in (
                ("odd-site", header.replace('_start:\n', '_start:\n.byte 0\n') +
                 '.balign 2\n.global target\n.type target,@function\ntarget: nop\n',
                 "site must be aligned to two bytes"),
                ("data-site", header.replace('.text\n', '.data\n') +
                 '.text\n.global target\n.type target,@function\ntarget: nop\n',
                 "site must be in an executable section"),
                ("data-target", header + '.data\n.global target\ntarget: .quad 0\n',
                 "target must be in an executable section")):
            obj = self.assembly(directory, kind, content)
            path = self.link(directory, kind, [obj])
            self.command([sys.executable, "-B", self.inputs / "tests/check_calls.py", "--elf", path,
                          "--call-site", "call", "--target", "target"], kind + "-gate", reject=diagnostic)
        shared = self.assembly(directory, "shared", header + '.hidden target\n.type target,@function\ntarget: nop\n')
        path = directory / "shared.elf"
        self.command([self.cross + "ld", "-shared", "--no-relax", shared, "-o", path], "shared-link")
        self.command([sys.executable, "-B", self.inputs / "tests/check_calls.py", "--elf", path,
                      "--call-site", "call", "--target", "target"], "shared-gate", reject="ET_DYN is unsupported")
        return {"linker_rejections": 2, "post_link_validator_rejections": 7,
                "unsupported_forms": ["undefined", "PCREL overflow", "object", "TLS", "odd target",
                                      "odd site", "data site", "data target", "shared"]}

    def run(self):
        self.results["verifier_selftest"] = verifier_selftest()
        for tool in ("gcc", "as", "ld", "objdump", "readelf"):
            self.command([self.cross + tool, "--version"], tool + "-version")
        tests = [("constants", self.test_constants), ("assembly", self.test_assembly),
                 ("calls", self.test_calls), ("negative-c", self.test_negative_c),
                 ("negative-assembly", self.test_negative_assembly), ("negative-links", self.test_negative_links)]
        for optimization in ("-O0", "-O2", "-O3", "-Os"):
            tests.append(("csr" + optimization, lambda opt=optimization: self.test_csr(opt)))
        tests.append(("csr-O2-lto", lambda: self.test_csr("-O2", True)))
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.jobs) as executor:
            futures = {executor.submit(function): name for name, function in tests}
            for future in concurrent.futures.as_completed(futures):
                name = futures[future]
                try:
                    self.results[name] = future.result()
                    print("PASS " + name, flush=True)
                except Exception as error:
                    self.failures[name] = {"message": str(error), "traceback": traceback.format_exc()}
                    print("FAIL %s: %s" % (name, error), flush=True)
        summary = {"status": "FAIL" if self.failures else "PASS", "command_count": len(self.commands),
                   "max_parallel_tool_processes": self.jobs, "results": self.results, "failures": self.failures,
                   "scope": "software compile/link/encoding only; no processor execution or runtime safety validation"}
        self.save(self.out, "summary", summary)
        self.save(self.out, "commands", self.commands)
        print(json.dumps({"status": summary["status"], "summary": str(self.out / "summary.json")}), flush=True)
        return 1 if self.failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--abi", required=True, help="frozen authoritative machine JSON")
    parser.add_argument("--out", required=True, help="fresh persistent evidence directory")
    parser.add_argument("--cross-compile", default="/usr/bin/riscv64-linux-gnu-")
    parser.add_argument("--jobs", type=int, choices=range(1, 5), default=4)
    args = parser.parse_args()
    try:
        return Runner(args).run()
    except Exception:
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
