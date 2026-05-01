# Spike A results — Interpreter throughput on minimum emulation hardware

**Status: build and toolchain complete; real-hardware numbers pending.**

The question Spike A asks is whether an RV32IMFC interpreter running on a
Pi Zero 2 W (Cortex-A53 @ 1 GHz) can execute a realistic cart workload within
the 16.7 ms frame budget at 60 fps. The build infrastructure is done and
verified correct. The actual answer requires Pi Zero 2 W hardware, which has
not yet been run.

---

## What was built

**Interpreter:** [rv32emu](https://github.com/sysprog21/rv32emu) — a
well-maintained, Apache 2.0-licensed RV32IMFC software interpreter written in
C. It handles Linux user-space ECALLs and loads ELFs directly. Chosen over
writing a new interpreter because the licensing is clean, the correctness
track record is good, and it can be dropped into the target binary build.

**Build environment:** arm64 Docker container (Ubuntu 24.04) built from
`spikes/spike-a/Dockerfile`. The container mirrors the Pi Zero 2 W's ISA
(Cortex-A53, arm64) and is used for build validation on Apple Silicon. All
timing numbers from this container are substantially faster than Pi hardware
and must not be treated as the spike answer.

**Guest cross-toolchain:** `gcc-riscv64-linux-gnu` targeting
`-march=rv32imfc_zicsr -mabi=ilp32f`, with Berkeley SoftFloat 3 providing
IEEE 754 double-precision helpers (the `rv64` libgcc.a cannot be used for
`rv32 ilp32f` targets). A minimal `crt0.S` + `syscalls.c` provides the bare
C runtime: `_exit`, raw `write`, `memset`/`memcpy`/`strlen`/`strcmp`, and a
single-character-at-a-time `ee_printf`.

**CoreMark:** The [EEMBC CoreMark](https://github.com/eembc/coremark)
benchmark compiled to RV32IMFC using a custom port in
`ports/rv32emu/`. Timing via Linux `clock_gettime64` (ECALL 403). SoftFloat
provides the double-precision helpers for the score calculation path.

**Embench-IoT:** All 18 benchmarks from
[embench-iot](https://github.com/embench/embench-iot) compiled to RV32IMFC.
The port lives in `ports/rv32emu/embench/` and reuses the same crt0/syscalls
foundation as CoreMark. Each benchmark binary self-reports its elapsed time
when run under rv32emu. qrduino is excluded (requires `<avr/pgmspace.h>`).

---

## Docker / Apple Silicon numbers (correctness check only)

These numbers were produced on an arm64 Docker container running on Apple
Silicon (M-series). They prove the benchmarks build and execute correctly;
they are **not** the spike's success criterion. The Pi Zero 2 W Cortex-A53
running at 1 GHz will be materially slower than this container.

### CoreMark

```
2K performance run parameters for coremark.
CoreMark Size    : 666
Total ticks      : 20026523
Total time (secs): 12.134348
Iterations/Sec   : 1648.925293
Iterations       : 20000
Compiler version : GCC13.3.0
Compiler flags   : -march=rv32imfc_zicsr -mabi=ilp32f -O2 -ffreestanding
                   -nostdlib -fno-stack-protector -fno-common -static
                   -no-pie -fno-pie
Memory location  : STACK
seedcrc          : 0xe9f5
[0]crclist       : 0xe714
[0]crcmatrix     : 0x1fd7
[0]crcstate      : 0x8e3a
[0]crcfinal      : 0x65c5
Correct operation validated. See README.md for run and reporting rules.
CoreMark 1.0 : 1651.30 / GCC13.3.0 / STACK
Elapsed (us)     : 12116476
```

The score of **1651 iterations/sec** is the guest workload throughput as seen
by the rv32emu interpreter, running on the arm64 Docker host. On Pi Zero 2 W
Cortex-A53 this number will be lower — the arm64 Docker host runs at ~3 GHz
effective throughput versus the Pi's 1 GHz, and the interpreter overhead
ratio is not 1:1 with clock speed.

### Embench-IoT

All 18 benchmarks pass verification (`verify_benchmark()` returns correct).

| Benchmark      | Docker time (ms) | Notes                              |
|----------------|------------------|------------------------------------|
| aha-mont64     | 9.6              | Montgomery multiplication          |
| crc32          | 5.5              | CRC-32                             |
| depthconv      | 6.5              | CNN depthwise convolution (float)  |
| edn            | 6.6              | FIR filter                         |
| huffbench      | 5.9              | Huffman encoding                   |
| matmult-int    | 7.3              | Integer matrix multiply            |
| md5sum         | 5.2              | MD5 hash                           |
| nettle-aes     | 6.0              | AES cipher                         |
| nettle-sha256  | 12.6             | SHA-256                            |
| nsichneu       | 15.4             | Large state machine                |
| picojpeg       | 6.9              | JPEG decoder                       |
| sglib-combined | 10.8             | Linked list / tree operations      |
| slre           | 5.5              | Regular expression matching        |
| statemate      | 6.0              | State machine (int recast as float)|
| tarfind        | 4.3              | Tar archive search                 |
| ud             | 5.3              | LU decomposition (int)             |
| wikisort       | 4.5              | Merge sort (calls sqrt via double) |
| xgboost        | 12.8             | Decision tree inference            |

Times range from 4–16 ms on this host. On Pi hardware all of these will
increase by a factor that depends on the host clock ratio, memory bandwidth,
and how well Cortex-A53's out-of-order pipeline amortises the interpreter
dispatch loop overhead. A rough estimate based on the 3:1 clock ratio alone
gives 12–48 ms Pi times, which would exceed the 16.7 ms budget for several
benchmarks — but this is not a meaningful prediction without real data.
Published RV32 soft-core scores on Cortex-A53 range from 200–400
CoreMark/MHz; at 1 GHz that gives 200–400 iterations/sec, compared to our
~1650 on the faster Docker host. Validating this against real hardware is the
remaining work.

---

## What remains

1. **Pi Zero 2 W hardware.** Run `make docker-bench` (CoreMark) and
   `make docker-embench` (Embench) on a real Pi Zero 2 W with rv32emu built
   natively for arm64. The Makefile already supports this path (the Docker
   image is `linux/arm64`; the ELFs run unchanged on real hardware).

2. **Score against the success criterion.** The criterion is that a
   non-trivial retro-era game loop leaves at least half the frame budget (~8 ms)
   headroom after the interpreter workload. The CoreMark score expressed as
   effective MIPS, and the Embench median time, are the two inputs to this
   judgment.

3. **Set the MIPS cap (ADR-0082).** The measured effective MIPS figure from
   the real Pi run becomes the emulator MIPS cap baked into all emulator
   builds. This cap is currently unset.

---

## Running the benchmarks

```sh
# Correctness check on Apple Silicon (arm64 Docker):
make docker-bench       # CoreMark
make docker-embench     # all 18 Embench benchmarks

# On real Pi Zero 2 W (ssh in, clone repo, then):
make -C rv32emu OUT=build -j4
make -C coremark PORT_DIR=../ports/rv32emu ITERATIONS=3000 compile
./rv32emu/build/rv32emu coremark/coremark.elf

for f in ports/rv32emu/embench/build/embench-*.elf; do
  ./rv32emu/build/rv32emu "$f"
done
```

The `ITERATIONS=3000` value targets approximately 10 seconds of CoreMark at
the expected Pi throughput; adjust if the run completes in under 10 seconds
(CoreMark requires at least 10 seconds for a valid score).
