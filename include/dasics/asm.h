/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef DASICS_ASM_H
#define DASICS_ASM_H

#ifndef __ASSEMBLER__
#error "dasics/asm.h requires preprocessed assembly"
#endif

#include <dasics/abi.h>

/*
 * These macros encode instructions; they do not establish permission or save
 * machine state. FDICALL requires an enabled, trusted host U/S source. CSR
 * access additionally requires its encoded privilege and applicable stateen
 * permission; a U-view name does not permit HU access to an S-level CSR.
 */

/*
 * Parse decimal or hexadecimal literals instead of accepting GAS expressions.
 * This prevents label differences from becoming stale when the linker changes
 * layout. Accumulation is bounded before it can overflow; leading zeroes are
 * decimal unless followed immediately by x/X. Signs are optional.
 */
.macro _DASICS_LITERAL out:req, value:req
	.set .Ldasics_state\@, 0
	.set .Ldasics_sign\@, 1
	.set .Ldasics_value\@, 0
	.set .Ldasics_bad\@, 0
	.irpc char,\value
	.if !.Ldasics_bad\@
		.set .Ldasics_skip\@, 0
		.if .Ldasics_state\@ == 0
			.ifc \char,-
				.set .Ldasics_sign\@, -1
				.set .Ldasics_state\@, 1
				.set .Ldasics_skip\@, 1
			.endif
			.ifc \char,+
				.set .Ldasics_state\@, 1
				.set .Ldasics_skip\@, 1
			.endif
		.endif
		.if .Ldasics_state\@ == 2
			.irp prefix,x,X
				.ifc \char,\prefix
					.set .Ldasics_state\@, 4
					.set .Ldasics_skip\@, 1
				.endif
			.endr
		.endif
		.if !.Ldasics_skip\@
			.set .Ldasics_digit\@, -1
			.irp digit,0,1,2,3,4,5,6,7,8,9
				.ifc \char,\digit
					.set .Ldasics_digit\@, \digit
				.endif
			.endr
			.set .Ldasics_letter\@, 10
			.irp digit,a,b,c,d,e,f,A,B,C,D,E,F
				.ifc \char,\digit
					.set .Ldasics_digit\@, .Ldasics_letter\@
				.endif
				.set .Ldasics_letter\@, .Ldasics_letter\@ + 1
				.if .Ldasics_letter\@ == 16
					.set .Ldasics_letter\@, 10
				.endif
			.endr
			.set .Ldasics_base\@, 10
			.if .Ldasics_state\@ >= 4
				.set .Ldasics_base\@, 16
			.endif
			.if (.Ldasics_digit\@ < 0) || (.Ldasics_digit\@ >= .Ldasics_base\@)
				.error "DASICS constant requires a signed decimal or hexadecimal literal"
				.set .Ldasics_bad\@, 1
			.else
				.set .Ldasics_value\@, .Ldasics_value\@ * .Ldasics_base\@ + .Ldasics_digit\@
				.if .Ldasics_value\@ > -DASICS_FDICALL_J_OFFSET_MIN
					.error "DASICS numeric literal is out of range"
					.set .Ldasics_bad\@, 1
				.endif
				.if (.Ldasics_state\@ <= 1) && (.Ldasics_digit\@ == 0)
					.set .Ldasics_state\@, 2
				.elseif .Ldasics_base\@ == 16
					.set .Ldasics_state\@, 5
				.else
					.set .Ldasics_state\@, 3
				.endif
			.endif
		.endif
	.endif
	.endr
	.if (.Ldasics_state\@ == 0) || (.Ldasics_state\@ == 1) || (.Ldasics_state\@ == 4)
		.error "DASICS constant requires a complete numeric literal"
	.endif
	.set \out, .Ldasics_sign\@ * .Ldasics_value\@
.endm

/* Raw .insn formats can accept floating registers; restrict this API to GPRs. */
.macro _DASICS_GPR reg:req
	.set .Ldasics_gpr\@, 0
	.irp name,x0,x1,x2,x3,x4,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15,x16,x17,x18,x19,x20,x21,x22,x23,x24,x25,x26,x27,x28,x29,x30,x31,zero,ra,sp,gp,tp,t0,t1,t2,s0,fp,s1,a0,a1,a2,a3,a4,a5,a6,a7,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11,t3,t4,t5,t6
		.ifc \reg,\name
			.set .Ldasics_gpr\@, 1
		.endif
	.endr
	.if !.Ldasics_gpr\@
		.error "DASICS register must be an integer GPR"
	.endif
.endm

/* Offset is relative to this instruction's PC, and the link is always x1. */
.macro DASICS_FDICALL_J_OFFSET offset:req
	_DASICS_LITERAL .Ldasics_offset\@, \offset
	.if (.Ldasics_offset\@ < DASICS_FDICALL_J_OFFSET_MIN) || (.Ldasics_offset\@ > DASICS_FDICALL_J_OFFSET_MAX)
		.error "DASICS FDICALL.J offset is out of range"
	.endif
	.if (.Ldasics_offset\@ & 1)
		.error "DASICS FDICALL.J offset must be even"
	.endif
	.set .Ldasics_word\@, DASICS_FDICALL_J_MATCH
	.set .Ldasics_word\@, .Ldasics_word\@ | (((.Ldasics_offset\@ >> DASICS_FDICALL_J_SLICE_0_OFFSET_LO) & ((1 << DASICS_FDICALL_J_SLICE_0_WIDTH) - 1)) << DASICS_FDICALL_J_SLICE_0_INST_LO)
	.set .Ldasics_word\@, .Ldasics_word\@ | (((.Ldasics_offset\@ >> DASICS_FDICALL_J_SLICE_1_OFFSET_LO) & ((1 << DASICS_FDICALL_J_SLICE_1_WIDTH) - 1)) << DASICS_FDICALL_J_SLICE_1_INST_LO)
	.set .Ldasics_word\@, .Ldasics_word\@ | (((.Ldasics_offset\@ >> DASICS_FDICALL_J_SLICE_2_OFFSET_LO) & ((1 << DASICS_FDICALL_J_SLICE_2_WIDTH) - 1)) << DASICS_FDICALL_J_SLICE_2_INST_LO)
	.set .Ldasics_word\@, .Ldasics_word\@ | (((.Ldasics_offset\@ >> DASICS_FDICALL_J_SLICE_3_OFFSET_LO) & ((1 << DASICS_FDICALL_J_SLICE_3_WIDTH) - 1)) << DASICS_FDICALL_J_SLICE_3_INST_LO)
	.set .Ldasics_word\@, .Ldasics_word\@ | (((.Ldasics_offset\@ >> DASICS_FDICALL_J_SLICE_4_OFFSET_LO) & ((1 << DASICS_FDICALL_J_SLICE_4_WIDTH) - 1)) << DASICS_FDICALL_J_SLICE_4_INST_LO)
	.insn DASICS_INSTRUCTION_LENGTH, .Ldasics_word\@
.endm

/* rd=x0 still updates ReturnPC; rd=rs1 uses the old rs1 in the architecture. */
.macro DASICS_FDICALL_JR rd:req, rs1:req, offset:req
	_DASICS_GPR \rd
	_DASICS_GPR \rs1
	_DASICS_LITERAL .Ldasics_offset\@, \offset
	.if (.Ldasics_offset\@ < DASICS_FDICALL_JR_OFFSET_MIN) || (.Ldasics_offset\@ > DASICS_FDICALL_JR_OFFSET_MAX)
		.error "DASICS FDICALL.JR offset is out of range"
	.endif
	.insn i DASICS_FDICALL_OPCODE, DASICS_FDICALL_JR_FUNCT3, \rd, \rs1, .Ldasics_offset\@
.endm

/*
 * Static, nonpreemptible, non-TLS symbols within PC-relative reach only. The
 * final ELF must pass the supplied verifier; a generic linker can also accept
 * unsupported dynamic images. ra and t0 are clobbered. The caller supplies
 * integer arguments in a0-a7, preserves live caller-saved state and its return
 * address, and keeps sp 16-byte aligned. No untrusted-callee preservation is
 * assumed or enforced. This is not a safe C call or a trusted runtime wrapper.
 * The link/ReturnPC belongs to JR's PC+4, not the AUIPC's PC+4.
 */
.macro DASICS_CALL symbol:req
	.option push
	.option norelax
	.option norvc
.Ldasics_call\@:
	auipc t0, %pcrel_hi(\symbol)
	addi t0, t0, %pcrel_lo(.Ldasics_call\@)
	DASICS_FDICALL_JR ra, t0, 0
	.option pop
.endm

.macro _DASICS_CSR_ADDRESS csr:req
	.if ((\csr) < 0) || ((\csr) > 0xfff)
		.error "DASICS CSR address must be a constant in 0..4095"
	.endif
.endm

.macro _DASICS_ZIMM value:req
	.if ((\value) < 0) || ((\value) > 31)
		.error "DASICS CSR immediate must be a constant in 0..31"
	.endif
.endm

/*
 * RW always writes; rd=x0 suppresses only its read. RS/RC always read and
 * suppress writing only when the encoded source is x0, regardless of its
 * runtime contents. RSI/RCI suppress writing when zimm=0. No fence is added.
 */
.macro DASICS_CSR_RW rd:req, csr:req, rs1:req
	_DASICS_CSR_ADDRESS \csr
	csrrw \rd, \csr, \rs1
.endm
.macro DASICS_CSR_RS rd:req, csr:req, rs1:req
	_DASICS_CSR_ADDRESS \csr
	csrrs \rd, \csr, \rs1
.endm
.macro DASICS_CSR_RC rd:req, csr:req, rs1:req
	_DASICS_CSR_ADDRESS \csr
	csrrc \rd, \csr, \rs1
.endm
.macro DASICS_CSR_RWI rd:req, csr:req, zimm:req
	_DASICS_CSR_ADDRESS \csr
	_DASICS_ZIMM \zimm
	csrrwi \rd, \csr, \zimm
.endm
.macro DASICS_CSR_RSI rd:req, csr:req, zimm:req
	_DASICS_CSR_ADDRESS \csr
	_DASICS_ZIMM \zimm
	csrrsi \rd, \csr, \zimm
.endm
.macro DASICS_CSR_RCI rd:req, csr:req, zimm:req
	_DASICS_CSR_ADDRESS \csr
	_DASICS_ZIMM \zimm
	csrrci \rd, \csr, \zimm
.endm

/* Indices are post-CPP numeric literals; selected CSR symbols are absolute. */
.macro _DASICS_INDEX out:req, index:req, entries:req, base:req
	_DASICS_LITERAL .Ldasics_index\@, \index
	.if (.Ldasics_index\@ < 0) || (.Ldasics_index\@ >= \entries)
		.error "DASICS entry index is out of range"
	.else
		.set \out, \base + 2 * .Ldasics_index\@
	.endif
.endm
.macro DASICS_LIB_BOUND_LO out:req, index:req
	_DASICS_INDEX \out, \index, DASICS_LIB_ENTRIES, DASICS_CSR_LIB_BOUND_LO_0
.endm
.macro DASICS_LIB_BOUND_HI out:req, index:req
	_DASICS_INDEX \out, \index, DASICS_LIB_ENTRIES, DASICS_CSR_LIB_BOUND_HI_0
.endm
.macro DASICS_JUMP_BOUND_LO out:req, index:req
	_DASICS_INDEX \out, \index, DASICS_JUMP_ENTRIES, DASICS_CSR_JUMP_BOUND_LO_0
.endm
.macro DASICS_JUMP_BOUND_HI out:req, index:req
	_DASICS_INDEX \out, \index, DASICS_JUMP_ENTRIES, DASICS_CSR_JUMP_BOUND_HI_0
.endm

.macro _DASICS_SLOT_MASK out:req, index:req, entries:req, stride:req, bit:req
	_DASICS_LITERAL .Ldasics_index\@, \index
	.if (.Ldasics_index\@ < 0) || (.Ldasics_index\@ >= \entries)
		.error "DASICS entry index is out of range"
	.else
		.set \out, 1 << (.Ldasics_index\@ * \stride + \bit)
	.endif
.endm
.macro DASICS_LIB_CFG_WRITE out:req, index:req
	_DASICS_SLOT_MASK \out, \index, DASICS_LIB_ENTRIES, DASICS_LIB_CFG_SLOT_BITS, DASICS_LIB_CFG_WRITE_BIT
.endm
.macro DASICS_LIB_CFG_READ out:req, index:req
	_DASICS_SLOT_MASK \out, \index, DASICS_LIB_ENTRIES, DASICS_LIB_CFG_SLOT_BITS, DASICS_LIB_CFG_READ_BIT
.endm
.macro DASICS_LIB_CFG_VALID out:req, index:req
	_DASICS_SLOT_MASK \out, \index, DASICS_LIB_ENTRIES, DASICS_LIB_CFG_SLOT_BITS, DASICS_LIB_CFG_VALID_BIT
.endm
.macro DASICS_JUMP_CFG_VALID out:req, index:req
	_DASICS_SLOT_MASK \out, \index, DASICS_JUMP_ENTRIES, DASICS_JUMP_CFG_SLOT_BITS, DASICS_JUMP_CFG_VALID_BIT
.endm

#endif
