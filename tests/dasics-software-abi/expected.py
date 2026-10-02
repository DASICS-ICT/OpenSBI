"""Fixed A01 v2 expectations, deliberately independent of production generation."""


def constants():
    result = {}

    def add(name, value):
        result["DASICS_" + name] = value

    for name, value in {"ABI_VERSION": 2, "XLEN": 64, "CSR_ADDRESS_COUNT": 52,
                        "CSR_BACKING_COUNT": 51}.items():
        add(name, value)
    masks = {"MAIN_S": 0x7ff, "MAIN_U": 0x7c2, "BOUND": 0xfffffffffffffff8,
             "LIB_CFG": 0xbbbbbbbbbbbbbbbb, "JUMP_CFG": 0x0001000100010001,
             "PC": 0xffffffffffffffff, "REASON": 7}
    for kind, mask in masks.items():
        add(kind + "_READ_MASK", mask)
        add(kind + "_WRITE_MASK", mask)
        add(kind + "_RESET", 0)
    for name, (address, kind, privilege) in csr_map().items():
        add("CSR_" + name, address)
        add("CSR_" + name + "_MIN_PRIVILEGE", privilege)
        add("CSR_" + name + "_READ_MASK", masks[kind])
        add("CSR_" + name + "_WRITE_MASK", masks[kind])
        add("CSR_" + name + "_RESET", 0)
    fields = ("S_ENABLE", "U_ENABLE", "S_CLOSE_ECALL", "S_CLOSE_WRITE", "S_CLOSE_READ",
              "S_CLOSE_JUMP", "U_CLOSE_ECALL", "U_CLOSE_WRITE", "U_CLOSE_READ",
              "U_CLOSE_JUMP", "U_CLOSE_USER_INTERRUPT")
    for bit, field in enumerate(fields):
        add("MAIN_CFG_" + field + "_BIT", bit)
        add("MAIN_CFG_" + field + "_MASK", 1 << bit)
    add("MAIN_CFG_U_HIDDEN_MASK", 0x03d)
    for name, value in {"LIB_ENTRIES": 16, "LIB_CFG_SLOT_BITS": 4,
                        "LIB_CFG_SLOT_RESERVED_MASK": 4, "JUMP_ENTRIES": 4,
                        "JUMP_CFG_SLOT_BITS": 16, "JUMP_CFG_VALID_BIT": 0,
                        "JUMP_CFG_VALID_SLOT_MASK": 1, "JUMP_CFG_SLOT_RESERVED_MASK": 0xfffe}.items():
        add(name, value)
    for field, bit in (("WRITE", 0), ("READ", 1), ("VALID", 3)):
        add("LIB_CFG_" + field + "_BIT", bit)
        add("LIB_CFG_" + field + "_SLOT_MASK", 1 << bit)
        for index in range(16):
            add("LIB_CFG_%s_%d_BIT" % (field, index), 4 * index + bit)
            add("LIB_CFG_%s_%d_MASK" % (field, index), 1 << (4 * index + bit))
    for index in range(4):
        add("JUMP_CFG_VALID_%d_BIT" % index, 16 * index)
        add("JUMP_CFG_VALID_%d_MASK" % index, 1 << (16 * index))
    for name, value in {"FREASON_BITS": 3, "FREASON_SOFTWARE_MIN": 0,
                        "FREASON_SOFTWARE_MAX": 7, "FREASON_CLEAR": 0,
                        "FREASON_ECALL": 1, "FREASON_LOAD": 2,
                        "FREASON_STORE_AMO": 3, "FREASON_JUMP": 4}.items():
        add(name, value)
    for value in range(8):
        add("FREASON_VALUE_%d" % value, value)
    for value in range(5, 8):
        add("FREASON_RESERVED_%d" % value, value)
    for name, value in {"HU_CAUSE": 24, "HS_CAUSE": 25, "INTERNAL_HU_BIT": 24,
                        "INTERNAL_HS_BIT": 25, "VECTOR_WIDTH": 26,
                        "MEDELEG_MASK": 0x3000000, "HEDELEG_MASK": 0,
                        "MEDELEG_BITS_RESET": 0, "ECALL_TVAL": 0}.items():
        add("EXCEPTION_" + name, value)
    for name, value in {"INSTRUCTION_LENGTH": 4, "FDICALL_MASK": 0x707f,
                        "FDICALL_OPCODE": 0x0b, "FDICALL_J_MATCH": 0x0b,
                        "FDICALL_J_FUNCT3": 0, "FDICALL_J_MIN_BITS": 22,
                        "FDICALL_J_LOW_ZERO_BITS": 1, "FDICALL_J_LINK_REGISTER": 1,
                        "FDICALL_J_OFFSET_MIN": -4194304, "FDICALL_J_OFFSET_MAX": 4194302,
                        "FDICALL_JR_MATCH": 0x100b, "FDICALL_JR_FUNCT3": 1,
                        "FDICALL_JR_IMMEDIATE_BITS": 12, "FDICALL_JR_OFFSET_MIN": -2048,
                        "FDICALL_JR_OFFSET_MAX": 2047}.items():
        add(name, value)
    for index, (hi, lo, offset_lo, width) in enumerate(((31, 31, 22, 1), (7, 7, 21, 1),
            (20, 15, 15, 6), (11, 8, 11, 4), (30, 21, 1, 10))):
        for field, value in zip(("INST_HI", "INST_LO", "OFFSET_LO", "WIDTH"), (hi, lo, offset_lo, width)):
            add("FDICALL_J_SLICE_%d_%s" % (index, field), value)
    for field, value in (("INST_HI", 31), ("INST_LO", 20), ("OFFSET_LO", 0), ("WIDTH", 12)):
        add("FDICALL_JR_SLICE_0_" + field, value)
    return result


def csr_map():
    result = {"S_MAIN_CFG": (0xbc4, "MAIN_S", 3), "U_MAIN_CFG": (0x9e1, "MAIN_U", 1),
              "S_MAIN_BOUND_LO": (0xbc5, "BOUND", 3), "S_MAIN_BOUND_HI": (0xbc6, "BOUND", 3),
              "U_MAIN_BOUND_LO": (0x9e2, "BOUND", 1), "U_MAIN_BOUND_HI": (0x9e3, "BOUND", 1),
              "LIB_CFG": (0x880, "LIB_CFG", 0), "MAIN_CALL_ENTRY": (0x8b0, "PC", 0),
              "RETURN_PC": (0x8b1, "PC", 0), "ACTIVE_ZONE_RETURN_PC": (0x8b2, "PC", 0),
              "FREASON": (0x8b3, "REASON", 0), "JUMP_CFG": (0x8c8, "JUMP_CFG", 0)}
    for group, base, count in (("LIB", 0x890, 16), ("JUMP", 0x8c0, 4)):
        for index in range(count):
            for side, increment in (("LO", 0), ("HI", 1)):
                result["%s_BOUND_%s_%d" % (group, side, index)] = (base + 2 * index + increment, "BOUND", 0)
    return result
