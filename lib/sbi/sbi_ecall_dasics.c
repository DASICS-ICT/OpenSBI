#include <sbi/sbi_domain.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_trap.h>
#include <sbi/sbi_version.h>
#include <sbi/sbi_dasics.h>
#include <sbi/sbi_scratch.h>
#include <sbi/riscv_asm.h>
#include <sbi/sbi_console.h>

static int sbi_ecall_dasics_handler(unsigned long extid, unsigned long funcid,
                const struct sbi_trap_regs *regs,
                unsigned long *out_val,
                struct sbi_trap_info *out_trap)
{
    if ((extid != 0xda51c5) || (funcid != 0x1))
        return SBI_ERR_INVALID_PARAM;
    
    csr_write(DSMBOUND_HI, regs->a1);
    csr_write(DSMBOUND_LO, regs->a0);
    csr_write(DSMCFG, regs->a2);
    return SBI_SUCCESS;
}

struct sbi_ecall_extension ecall_dasics = {
    .extid_start = SBI_EXT_DASICS,
    .extid_end = SBI_EXT_DASICS,
    .handle = sbi_ecall_dasics_handler,
};
 