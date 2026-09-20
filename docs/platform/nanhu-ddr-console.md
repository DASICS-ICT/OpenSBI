# Nanhu DDR output console

The `bosc,nanhu-ddr-console` backend records OpenSBI and Linux SBI console output
in reserved DDR. It requires RV64 Nanhu with Zicbom and 64-byte cache blocks.
It provides no input and does not stop the CPU or interpret application text.

The `xiangshan/kmh_vp1902` platform includes this driver and selects DDR only
when `/chosen/stdout-path` points to its node. Otherwise the platform uses UART
with the clock and baud rate from the device tree. A malformed selected DDR node
is an error, not a UART fallback. The `kmh` and `kmh_s2c19p` platforms keep their
original console initialization; the generic platform does not enable this driver
by default. See [the VP1902 platform](kmh_vp1902.md) for board parameters.

Example (merge into the board's DT):

```dts
/ {
    chosen {
        stdout-path = &ddr_console;
        bootargs = "console=hvc0 earlycon=sbi";
    };
    reserved-memory {
        #address-cells = <2>;
        #size-cells = <2>;
        ranges;
        ddr_log: log@84000000 {
            reg = <0 0x84000000 0 0x100000>;
            no-map;
        };
    };
    ddr_console: ddr-console {
        compatible = "bosc,nanhu-ddr-console";
        memory-region = <&ddr_log>;
    };
};
```

Reserve physically backed DDR outside firmware, payload and other allocations.
Address and size must be multiples of 64, with at least 128 bytes. Disable unused
Linux UART nodes as well when sharing the physical serial port. Linux must enable
the SBI HVC and SBI early console drivers. Userspace stdout/stderr must be attached
to the console to appear in this log.

The first 64 bytes are eight little-endian uint64 fields:

| Offset | Field |
| --- | --- |
| 0 | Magic `0x555648534c4f4731` |
| 8 | Valid body length in bytes |
| 16 | Sum of unsigned body bytes modulo 2^64 |
| 24 | Overflow flag: new bytes were dropped after the buffer filled |
| 32 | Reserved, zero (version 1 used this as a test-specific done flag) |
| 40 | Cycle counter at the last publication |
| 48 | Time counter at the last publication |
| 56 | Format version, 2 |

The body starts at offset 64 and contains bytes in output order. Each output call
flushes touched body cache lines, fences, then publishes and flushes the header.
This includes an output tail without a newline. Calls are serialized by a driver
lock. The log starts afresh on firmware cold initialization; a full buffer drops
new bytes and reports overflow without blocking the workload.

On UVHS, wait for the workload, stop only the session's CPU while retaining DDR,
then dump the reserved region. Do not read DDR concurrently through the UVHS
backdoor. Reset does not flush processor caches; an interrupted output call can
leave an incomplete publication, which the decoder must reject if inconsistent.
Check workload completion from its output, separately from transport integrity.
Version 2 intentionally has no firmware test-completion flag.
