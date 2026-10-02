"""Small independent ELF64 reader and instruction checks for software encoding tests.

This module does not import the ABI generator, assembly encoder, or public checker.
The symbolic CSR analysis checks compiler output only; it is not a CPU model.
"""

import struct
from pathlib import Path


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def signed(value, width):
    return value - (1 << width) if value & (1 << (width - 1)) else value


class ELF:
    def __init__(self, path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        require(self.data[:7] == b"\x7fELF\x02\x01\x01", "expected little-endian ELF64")
        h = struct.unpack_from("<HHIQQQIHHHHHH", self.data, 16)
        self.etype, machine = h[:2]
        require(machine == 243, "expected RISC-V ELF")
        self.entry, self.flags = h[3], h[6]
        shoff, shentsize, shnum, shstrndx = h[5], h[10], h[11], h[12]
        require(shentsize == 64, "unexpected ELF section size")
        self.sections = []
        for i in range(shnum):
            v = struct.unpack_from("<IIQQQQIIQQ", self.data, shoff + i * shentsize)
            self.sections.append(dict(zip(
                ("name_offset", "type", "flags", "addr", "offset", "size", "link", "info", "align", "entsize"), v)))
        names = self.section_data(self.sections[shstrndx])
        for section in self.sections:
            section["name"] = self.string(names, section["name_offset"])
        self.symbols = {}
        self.symtabs = {}
        for i, section in enumerate(self.sections):
            if section["type"] != 2:
                continue
            strings = self.section_data(self.sections[section["link"]])
            symbols = []
            for offset in range(section["offset"], section["offset"] + section["size"], 24):
                name, info, other, shndx, value, size = struct.unpack_from("<IBBHQQ", self.data, offset)
                sym = {"name": self.string(strings, name), "info": info, "other": other,
                       "shndx": shndx, "value": value, "size": size}
                symbols.append(sym)
                if sym["name"] and (shndx != 0 or sym["name"] not in self.symbols):
                    self.symbols[sym["name"]] = sym
            self.symtabs[i] = symbols

    @staticmethod
    def string(data, offset):
        return data[offset:data.index(b"\0", offset)].decode("ascii")

    def section_data(self, section):
        return self.data[section["offset"]:section["offset"] + section["size"]]

    def sym(self, name):
        require(name in self.symbols, "missing symbol " + name)
        return self.symbols[name]

    def at(self, address, size):
        for section in self.sections:
            if section["type"] != 8 and section["addr"] <= address and address + size <= section["addr"] + section["size"]:
                start = section["offset"] + address - section["addr"]
                return self.data[start:start + size]
        raise AssertionError("address not file-backed: %#x" % address)

    def symbol_data(self, name, size=None):
        sym = self.sym(name)
        section = self.sections[sym["shndx"]]
        offset = section["offset"] + sym["value"] - section["addr"]
        return self.data[offset:offset + (sym["size"] if size is None else size)]

    def word(self, name):
        data = self.symbol_data(name, 4)
        require(len(data) == 4, "missing instruction bytes")
        return int.from_bytes(data, "little")

    def relocations(self):
        result = []
        for section in self.sections:
            if section["type"] != 4:
                continue
            symbols = self.symtabs[section["link"]]
            for offset in range(section["offset"], section["offset"] + section["size"], 24):
                address, info, addend = struct.unpack_from("<QQq", self.data, offset)
                result.append({"offset": address, "type": info & 0xffffffff,
                               "symbol": symbols[info >> 32]["name"], "addend": addend,
                               "target_section": self.sections[section["info"]]["name"]})
        return result


def decode_j(word):
    require(word & 0x707f == 0x0b, "FDICALL.J opcode/funct3 mismatch")
    immediate = ((word >> 31) << 22 | ((word >> 7) & 1) << 21 |
                 ((word >> 15) & 63) << 15 | ((word >> 8) & 15) << 11 |
                 ((word >> 21) & 1023) << 1)
    return signed(immediate, 23)


def decode_jr(word):
    require(word & 0x707f == 0x100b, "FDICALL.JR opcode/funct3 mismatch")
    return (word >> 7 & 31, word >> 15 & 31, signed(word >> 20, 12))


def decode_csr(word):
    require(word & 0x7f == 0x73, "expected SYSTEM opcode")
    funct3 = word >> 12 & 7
    require(funct3 in (1, 2, 3, 5, 6, 7), "expected one of six CSR forms")
    return {"funct3": funct3, "csr": word >> 20, "rd": word >> 7 & 31,
            "source": word >> 15 & 31}


def words(data):
    require(len(data) % 4 == 0, "uncompressed function length must be multiple of four")
    result = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
    require(all(word & 3 == 3 for word in result), "unexpected compressed instruction")
    return result


def csr_symbolic(function_words):
    """Track constants and stack spills up to CSR instructions in RV64I fixtures.

    Unknown register values stay unknown. An unsupported instruction invalidates
    its destination instead of inventing a value. Tests therefore cannot claim
    a zero-valued source merely from its register number.
    """
    regs = [None] * 32
    regs[0], regs[2] = 0, 0x100000
    memory = {}
    observed = []
    for index, word in enumerate(function_words):
        op, rd, rs1, rs2 = word & 127, word >> 7 & 31, word >> 15 & 31, word >> 20 & 31
        funct3 = word >> 12 & 7
        immediate = signed(word >> 20, 12)
        if op in (0x63, 0x67, 0x6f):
            require(not any(later & 127 == 0x73 and later >> 12 & 7 in (1, 2, 3, 5, 6, 7)
                            for later in function_words[index + 1:]),
                    "CSR source proof cannot cross unsupported control flow")
        if op == 0x73:
            csr = decode_csr(word)
            csr.update(index=index, source_value=regs[rs1] if funct3 < 5 else rs1)
            observed.append(csr)
            if rd:
                regs[rd] = None
        elif op in (0x13, 0x1b) and funct3 == 0:
            value = None if regs[rs1] is None else (regs[rs1] + immediate) & ((1 << 64) - 1)
            if value is not None and op == 0x1b:
                value = signed(value & 0xffffffff, 32) & ((1 << 64) - 1)
            if rd:
                regs[rd] = value
        elif op == 0x37:
            if rd:
                regs[rd] = signed(word & 0xfffff000, 32) & ((1 << 64) - 1)
        elif op == 0x23:
            offset = signed((word >> 25) << 5 | (word >> 7 & 31), 12)
            address = None if regs[rs1] is None else regs[rs1] + offset
            if address is not None:
                memory[(address, funct3)] = regs[rs2]
            else:
                memory.clear()
        elif op == 0x03:
            address = None if regs[rs1] is None else regs[rs1] + immediate
            if rd:
                regs[rd] = memory.get((address, funct3)) if address is not None else None
        elif op in (0x63, 0x0f):
            pass
        elif rd:
            regs[rd] = None
        regs[0] = 0
    return observed


def verifier_selftest():
    """Fixed words and intentional corruptions keep the oracle independent."""
    expected = [(0x0000000b, 0), (0x0020000b, 2), (0xffff8f8b, -2),
                (0x7fff8f8b, 4194302), (0x8000000b, -4194304),
                (0x0000008b, 2097152), (0x0000010b, 2048), (0x0000800b, 32768)]
    for word, offset in expected:
        require(decode_j(word) == offset, "fixed J decoder oracle mismatch")
    require(decode_jr(0x8000100b) == (0, 0, -2048), "fixed JR oracle mismatch")
    require(decode_jr(0x7fff9f8b) == (31, 31, 2047), "fixed JR oracle mismatch")
    for bad in (0x0000006f, 0x0000100b, 0x00000009):
        try:
            decode_j(bad)
        except AssertionError:
            continue
        raise AssertionError("decoder accepted corrupt J opcode/funct3")
    require(decode_j(0x0020000b ^ (1 << 7)) != 2, "field mutation was not detected")
    require(decode_jr(0x7fff9f8b ^ (1 << 15)) != (31, 31, 2047), "JR mutation was not detected")
    csr = decode_csr(0x8b306073)
    require(csr["funct3"] == 6 and csr["source"] == 0 and csr["rd"] == 0,
            "CSR fixed oracle mismatch")
    require(decode_csr(0x8b306073 ^ (1 << 15))["source"] != 0,
            "CSR source mutation was not detected")
    for transfer in (0x00000463, 0x000000ef, 0x000080e7):
        try:
            csr_symbolic([0x00000513, transfer, 0x8b352073])
        except AssertionError as error:
            require("unsupported control flow" in str(error), "unexpected proof failure")
            continue
        raise AssertionError("CSR source proof crossed a branch or call")
    return {"fixed_j_words": len(expected), "fixed_jr_words": 2,
            "rejected_opcode_mutations": 3, "detected_field_mutations": 3,
            "rejected_control_flow_mutations": 3}
