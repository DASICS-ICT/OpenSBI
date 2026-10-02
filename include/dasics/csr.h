/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef DASICS_CSR_H
#define DASICS_CSR_H

#include <dasics/abi.h>

#ifdef __ASSEMBLER__
#error "Use dasics/asm.h for preprocessed assembly"
#else

#if !defined(__GNUC__) || !defined(__riscv) || __riscv_xlen != 64
#error "DASICS CSR helpers require GNU C on RV64"
#endif

/*
 * These helpers only encode accesses. The caller must satisfy the CSR's
 * minimum privilege, host-mode, stateen and source-trust requirements. In
 * particular, the U main view requires at least S privilege.
 *
 * Every helper is volatile and a compiler memory barrier, but emits no
 * processor fence. Values are evaluated once and converted to unsigned long.
 * Returning helpers return the previous XLEN-wide CSR value.
 */
_Static_assert(sizeof(unsigned long) == 8,
	       "DASICS CSR helpers require a 64-bit unsigned long");

#define __DASICS_CSR_CHECK(csr) \
	_Static_assert(__builtin_classify_type(csr) == 1 && \
		       __builtin_constant_p(csr) && \
		       (csr) >= 0 && (csr) <= 4095, \
		       "DASICS CSR address must be an integer constant in [0, 4095]")

#define __DASICS_CSR_CHECK_IMM(value) \
	_Static_assert(__builtin_classify_type(value) == 1 && \
		       __builtin_constant_p(value) && \
		       (value) >= 0 && (value) <= 31, \
		       "DASICS CSR immediate must be an integer constant in [0, 31]")

/* CSRRS with source register x0 is a pure read. */
#define dasics_csr_read(csr) \
	({ \
		__DASICS_CSR_CHECK(csr); \
		unsigned long __dasics_csr_old; \
		__asm__ __volatile__("csrrs %0, %1, x0" \
			: "=r" (__dasics_csr_old) : "i" (csr) : "memory"); \
		__dasics_csr_old; \
	})

/* CSRRW always writes; rd=x0 suppresses only the CSR read. */
#define dasics_csr_write(csr, value) \
	do { \
		__DASICS_CSR_CHECK(csr); \
		unsigned long __dasics_csr_value = (unsigned long)(value); \
		__asm__ __volatile__("csrrw x0, %0, %1" \
			: : "i" (csr), "r" (__dasics_csr_value) : "memory"); \
	} while (0)

#define dasics_csr_swap(csr, value) \
	({ \
		__DASICS_CSR_CHECK(csr); \
		unsigned long __dasics_csr_value = (unsigned long)(value); \
		unsigned long __dasics_csr_old; \
		__asm__ __volatile__("csrrw %0, %1, %2" \
			: "=r" (__dasics_csr_old) \
			: "i" (csr), "r" (__dasics_csr_value) : "memory"); \
		__dasics_csr_old; \
	})

/*
 * A writable early-clobber output cannot be x0. Populate it before the CSR
 * instruction so RS/RC retain a non-x0 source even when value is constant zero.
 * Early-clobber prevents the copy from destroying any still-live input. The
 * returning form safely uses the same register for rd and rs1: the instruction
 * consumes the old source before replacing it with the previous CSR value.
 */
#define __DASICS_CSR_UPDATE(op, rd, csr, value) \
	({ \
		__DASICS_CSR_CHECK(csr); \
		unsigned long __dasics_csr_value = (unsigned long)(value); \
		unsigned long __dasics_csr_temp; \
		__asm__ __volatile__("mv %0, %2\n\t" op " " rd ", %1, %0" \
			: "=&r" (__dasics_csr_temp) \
			: "i" (csr), "r" (__dasics_csr_value) : "memory"); \
		__dasics_csr_temp; \
	})

/* Register set/clear always encode a write, including for a zero mask. */
#define dasics_csr_set(csr, value) \
	((void)__DASICS_CSR_UPDATE("csrrs", "x0", csr, value))
#define dasics_csr_clear(csr, value) \
	((void)__DASICS_CSR_UPDATE("csrrc", "x0", csr, value))
#define dasics_csr_read_set(csr, value) \
	__DASICS_CSR_UPDATE("csrrs", "%0", csr, value)
#define dasics_csr_read_clear(csr, value) \
	__DASICS_CSR_UPDATE("csrrc", "%0", csr, value)

#define __DASICS_CSR_IMM_WRITE(op, csr, value) \
	do { \
		__DASICS_CSR_CHECK(csr); \
		__DASICS_CSR_CHECK_IMM(value); \
		__asm__ __volatile__(op " x0, %0, %1" \
			: : "i" (csr), "i" (value) : "memory"); \
	} while (0)

#define __DASICS_CSR_IMM_READ(op, csr, value) \
	({ \
		__DASICS_CSR_CHECK(csr); \
		__DASICS_CSR_CHECK_IMM(value); \
		unsigned long __dasics_csr_old; \
		__asm__ __volatile__(op " %0, %1, %2" \
			: "=r" (__dasics_csr_old) \
			: "i" (csr), "i" (value) : "memory"); \
		__dasics_csr_old; \
	})

/* RWI always writes, including for immediate zero. */
#define dasics_csr_write_imm(csr, value) \
	__DASICS_CSR_IMM_WRITE("csrrwi", csr, value)
#define dasics_csr_swap_imm(csr, value) \
	__DASICS_CSR_IMM_READ("csrrwi", csr, value)

/* RSI/RCI with immediate zero suppress the write, unlike register helpers. */
#define dasics_csr_set_imm(csr, value) \
	__DASICS_CSR_IMM_WRITE("csrrsi", csr, value)
#define dasics_csr_clear_imm(csr, value) \
	__DASICS_CSR_IMM_WRITE("csrrci", csr, value)
#define dasics_csr_read_set_imm(csr, value) \
	__DASICS_CSR_IMM_READ("csrrsi", csr, value)
#define dasics_csr_read_clear_imm(csr, value) \
	__DASICS_CSR_IMM_READ("csrrci", csr, value)

#endif /* !__ASSEMBLER__ */
#endif /* DASICS_CSR_H */
