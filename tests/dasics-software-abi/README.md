# DASICS software ABI encoding checks

These tests use an existing RV64 GNU cross toolchain to preprocess, compile,
assemble and link the software interfaces. They never execute RISC-V code.
They do not test FDICALL execution, privilege checks, traps, context restoration
or the safety of an untrusted call.

Run from the OpenSBI repository after selecting a fresh persistent output
directory. The authoritative JSON remains outside this repository:

```sh
python3 -B tests/dasics-software-abi/run.py \
  --abi /absolute/path/to/LinkKmh-dasics/config/dasics-machine-abi.json \
  --out /absolute/path/to/LinkKmh-dasics/build/dasics/s01-software-abi/batch-name \
  --cross-compile /usr/bin/riscv64-linux-gnu- \
  --jobs 4
```

The output directory must not exist. The runner snapshots the JSON, headers,
generator and test sources into `inputs/` before launching the compiler. It
saves generated fixtures, preprocessed sources, objects, linker scripts and
final ELFs. Every tool invocation records its arguments, UTC start/end times,
actual exit code, stdout and stderr under `commands/`. `summary.json` reports
the aggregate result and the scope of the evidence. Failed batches remain
available for diagnosis and must not be overwritten.

The runner limits tool process concurrency to `--jobs` (1 through 4). All
assembler warnings are fatal. Run longer batches in the task's named tmux
session and preserve the runner's combined output and exit code outside tmux
scrollback.

`expected.py` contains the independent fixed A01 v2 constants and address map.
`elf_check.py` reads ELF64 little-endian sections, symbols and relocations and
decodes actual instruction words. Neither imports the production generator or
uses an encoder to generate expected words. Fixed instruction words and
intentional corruptions test the decoder itself. CSR source values are checked
by a small conservative constant/stack-spill analysis of the compiler output;
an unknown value or unsupported branch/call crossing fails a required proof.
This analysis is not a processor execution model.

Coverage includes C and preprocessed `.S` constant data, unsigned 64-bit masks,
all 52 addresses and 51 backing groups, C and assembly index selectors, six CSR
encodings, zero-valued non-x0 sources, discarded reads, immediate zero,
single evaluation and compiler memory barriers. C checks use `-O0`, `-O2`,
`-O3`, `-Os`, and `-O2 -flto`. J checks cover fixed words, signed boundaries and
every split immediate field, signed hexadecimal literals and leading-zero
decimal literals. JR checks cover all 1024 rd/rs1 pairs, x0,
rd=rs1, signed boundaries and odd immediates. Raw instructions remain 4 bytes,
including next to compressed instructions. Link checks cover same-section
forward/backward labels, cross-section and cross-file calls, and both linker
relaxation modes. An ordinary neighboring call really shrinks during relaxed
linking, changing the controlled call's PC; a fixed distant output section
also exercises a nonzero HI20 and negative LO12.

## Required gate for controlled symbol calls

`DASICS_CALL` is supported only together with the final ELF gate below. Generic
assembly/linking alone can accept unsupported shared images, data/TLS symbols
or odd function addresses. Such an ELF is not a supported call result.

Give each call sequence a unique symbol at its AUIPC instruction. Supply one
`--call-site` and one `--target` for every controlled call in the image:

```sh
python3 -B tests/dasics-software-abi/check_calls.py \
  --elf /absolute/path/to/final.elf \
  --call-site my_dasics_call --target my_target
```

The validator requires static ET_EXEC without dynamic loader/symbol sections,
defined executable call sites and function/notype targets at even addresses,
AUIPC t0 / ADDI t0,t0 / FDICALL.JR ra,t0,0, and the final address matching the
target symbol. It rejects surviving call-site relocations and records that the
specified link value is JR PC+4. It does not infer call sites from arbitrary
machine code: listing every supported `DASICS_CALL` is the caller's obligation.

Negative tests distinguish compiler/assembler errors, linker errors and
successful links rejected by the final gate. A tool's exit status or an
objdump `.word` display is never sufficient evidence of a correct encoding.
The gate does not establish runtime permission, trusted return handling,
callee register preservation or a safe C calling interface.
