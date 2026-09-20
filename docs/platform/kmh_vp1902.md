# LinkNan on UVHS V1 VP1902

Build with `PLATFORM=xiangshan/kmh_vp1902`. This platform targets the single-hart
LinkNan board wrapper and keeps the S2C19P platform separate.

The board device tree selects the console using `/chosen/stdout-path`:

- An `ns16550a` node selects the existing UART8250 driver. The UVHS device tree
  supplies the 10 MHz UART clock, 57600 baud, register shift 2 and access width 1.
- A `bosc,nanhu-ddr-console` node selects the output-only
  [DDR console](nanhu-ddr-console.md). Its memory-region must be reserved from
  the operating system and backed by DDR.

The platform reads `/cpus/timebase-frequency` for the timer frequency. For the
50/16.2601/10 MHz board profile, the RTC divider produces 1,626,010 Hz.

| Parameter | Value |
| --- | --- |
| Hart count | 1 |
| UART fallback address | `0x310b1000` |
| UART fallback clock / baud | 10 MHz / 57600 |
| ACLINT MSWI base | `0x38000000` |
| ACLINT MTIMER base | `0x38004000` |
| Timer fallback frequency | 1,626,010 Hz |
| Board firmware load address | `0x80000000` |
| Device-tree relocation offset for the next stage | `0x200000` |
| Payload / jump offset | `0x400000` |

Use the matching `nanhu-v5-1core-uvhs` or `nanhu-v5-1core-uvhs-ddr` device tree
from LinkNan. Both console drivers are built into this platform, so switching
the device tree does not require changing platform code. The existing
`xiangshan/kmh_s2c19p` platform retains its original UART and timer defaults.
