# Spike D results — cross-platform determinism

**Status: PASS.  `make all` builds the arm64 and amd64 Docker images,
hashes the cart ELFs in each, runs the three carts under rv32emu in
each, and exits 0.  Both gates are green:**

- **ELF byte identity.** The three Spike D cart ELFs
  (`lua_cart_det_doom_tick.elf`, `lua_cart_det_entity_update.elf`,
  `whetstone.elf`) hash to the same SHA-256 on both Docker images.
- **Per-frame digest stream identity.** `digests.arm64.txt` and
  `digests.amd64.txt` are byte-for-byte equal: SHA-256
  `f9fadde46c8b25b532570073f10513d8075f2c949a89cfc564cd4eda46e7c71f`,
  92 lines covering 30 frames × 3 workloads with their `=== <name> ===`
  headers.

The question Spike D asks (per `early-validation-spikes.md` §D and
`spikes/spike-d/PLAN.md`) is whether the cart workload, given the same
inputs, produces bit-identical per-frame output on two materially
different host platforms — `linux/amd64` and `linux/arm64` — without
heroic effort.  ADR-0007 names this as the load-bearing property: save
states, rewind, replay, and netplay all rely on it.

The answer is **yes**, with the spike-D-shaped caveats below.

This is a **correctness spike**.  No timing harness; the only output
the spike measures is the per-frame `DIGEST <frame> <hex64>` stream
each cart emits.

---

## What was built

### `cart_runtime/` — frame state, digest, RNG

A small set of source files, linked into every Spike D cart:

| File | Role |
|------|------|
| `frame_state.h` | The packed POD struct that defines the digest's input — layout is the wire format. |
| `digest.{c,h}` | FNV-1a-64 over the byte image of `frame_state_t`, with NaN canonicalization on every float field; emits `DIGEST <frame> <hex64>` on stdout. |
| `pcg32.{c,h}` | Reference PCG32 RNG (Melissa O'Neill, 2014); state lives inside `frame_state` so RNG progress is part of the digest. Default seed `0xfc320001`, default stream `0x14057b7ef767814f`. |
| `nan_canon.h` | `canonicalize_nanf()` collapses any IEEE-754 single-precision NaN to `0x7fc00000` before serialization. |

**Layout of `frame_state_t`** (packed):

```c
struct frame_state {
    uint32_t frame;            // monotonic frame counter
    uint64_t rng_state;        // PCG32 state at frame end
    uint64_t rng_inc;          // PCG32 stream
    float    accum_sin;        // running sum of sinf() calls
    float    accum_cos;        // running sum of cosf() calls
    float    accum_sqrt;       // running sum of sqrtf() calls
    float    accum_misc;       // catch-all f32 accumulator
    struct {
        float x, y, vx, vy;
        uint32_t state;
    } mobs[64];
} __attribute__((packed));
```

The struct is zero-initialised at cart entry.  Every float field passes
through `canonicalize_nanf()` immediately before the FNV-1a hash, so two
hosts that happen to mint different NaN payloads cannot create a
false-positive divergence in the digest.

### `lib/musl-libm/` — vendored musl 1.2.5 transcendentals

The Dockerfile downloads `musl-1.2.5.tar.gz` (pinned by SHA-256
`a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4`) and
extracts the source tree.  `lib/musl-libm/Makefile` compiles a curated
subset into `libm-rv32.a`:

- Float-precision transcendentals: `sinf`, `cosf`, `tanf`, `sincosf`,
  `atanf`, `atan2f`, `expf`, `exp2f`, `logf`, `log2f`, `powf`.
- Float-precision rounding: `floorf`, `ceilf`, `fabsf`.
- Internal kernels: `__rem_pio2f`, `__rem_pio2_large`, `__sindf`,
  `__cosdf`, `__tandf`.
- Error helpers: `__math_invalidf`, `__math_xflowf`, `__math_uflowf`,
  `__math_oflowf`, `__math_divzerof`.
- Data tables: `exp2f_data`, `logf_data`, `log2f_data`, `powf_data`.

`sqrtf` is **not** taken from musl.  Spike B's runtime keeps using the
inline `fsqrt.s` instruction (serviced by SoftFloat on the host), which
is already deterministic.  Excluding `sqrtf` from `libm-rv32.a` also
makes PLAN.md's first fallback step ("drop transcendentals, keep sqrt")
trivial: stop linking the archive and `sqrtf` survives unchanged.

The build flag set is identical to spike-b's (`-march=rv32imfc_zicsr
-mabi=ilp32f -O2 -ffreestanding -nostdlib`) plus the deterministic-FP
preamble per ADR-0007: `-fno-fast-math -ffp-contract=off
-frounding-math`.

### `lib/musl-libm/include/{libm.h,features.h}` — header shims

musl's own `src/internal/libm.h` pulls in the system `<endian.h>` and
`arch/*/fp_arch.h`, neither of which exists in a freestanding RV32
cross-build.  The shim is a self-contained drop-in defining only what
the float-precision kernels actually use:

- Bit-cast helpers (`asuint`, `asfloat`, `asuint64`, `asdouble`,
  `GET_FLOAT_WORD`, etc.).
- Branch hints (`predict_true` / `predict_false`).
- `eval_as_float` / `eval_as_double` (no excess-precision dance).
- `fp_barrierf` / `fp_force_evalf`.
- Hidden declarations of `__rem_pio2f`, `__sindf`, `__cosdf`, `__tandf`,
  and the `__math_*f` error helpers.

The `features.h` shim exists only to define `hidden` (the visibility
attribute musl's `*_data.h` headers use).  Both shims are 30-line files
with no external dependencies.

`spikes/spike-d/ports/rv32emu/include/math.h` is a superset of spike-b's
`math.h` adding `isnan` / `isinf` / `isfinite` / `signbit` (as
`__builtin_*` macros), `float_t` / `double_t` typedefs, and
`scalbnf` / `scalbn` declarations needed by the vendored kernels.

### Spike-D-shaped edits to spike-b's runtime

`spikes/spike-b/ports/rv32emu/lua_runtime.c` already provided the right
overall surface for a cross-platform RV32IMFC freestanding cart.  Two
minimal edits made it serve both spikes:

1. **`#ifndef HAVE_LIBM` gating** of the float-precision math stubs
   (`sinf`, `cosf`, `tanf`, `atanf`, `atan2f`, `expf`, `logf`, `log2f`,
   `powf`, `floorf`, `ceilf`, `fabsf`).  Default-off → spike-b unchanged.
   Spike-d's port compiles the same source file with `-DHAVE_LIBM`, so
   musl's strong symbols win at link time.  The double-precision stubs
   (`sin`, `cos`, …) remain unconditional in both builds because
   spike-d's musl subset is float-only.
2. **`scalbn(double,int)` / `scalbnf(float,int)`** as thin aliases of
   `ldexp` / `ldexpf`.  Used only by `__rem_pio2_large` for very large
   arguments — never reached by the spike workloads — but referenced
   at link time.

`spikes/spike-b/Dockerfile`'s runtime stage now also copies
`/spike-b/ports/rv32emu/{lua_runtime.c, lua_init_libs.c, softfloat-glue.c,
crt0.S, include/}` and `/spike-b/lua/` into the runtime image, so spike-d
(which `FROM`s the spike-b runtime image) can re-invoke the cart build
with `-DHAVE_LIBM`.

### Three cart ELFs

| ELF | Source | What it stresses |
|-----|--------|------------------|
| `lua_cart_det_doom_tick.elf`     | `workloads/det_doom_tick.lua`     | Lua VM + RV32IMFC guest + `sinf`/`cosf`/`atan2f`/`sqrtf` + PCG32-driven control flow |
| `lua_cart_det_entity_update.elf` | `workloads/det_entity_update.lua` | Pure FP arithmetic through Lua, dense `sinf`/`cosf` |
| `whetstone.elf`                  | `ports/rv32emu/whetstone.c`       | Dense f32 transcendentals with no Lua layer; isolates libm behaviour from VM behaviour |

Each Lua cart is `crt0.S` + `cart_runtime` + `lua_det_cart.c` (the
embed-and-run driver) + `lua_det_bindings.c` (the `console` global) +
Lua VM + spike-b's `lua_runtime.c` (with `-DHAVE_LIBM`) + SoftFloat
glue + `libm-rv32.a` + spike-a's SoftFloat archive.  Whetstone is the
same minus Lua.

### Where the FP determinism actually comes from

It is worth being explicit about the FP layering, because the cross-host
bit-identity result depends on it and the design only mentioned SoftFloat
in passing.  Three FP surfaces show up in a Spike D run, and **every one
of them goes through Berkeley SoftFloat** — none touches the host FPU:

| Surface | Path | Where SoftFloat sits |
|---------|------|----------------------|
| Guest f32 instructions in cart code (`fadd.s`, `fmul.s`, `fdiv.s`, `fsqrt.s`, conversions, comparisons) | rv32emu's interpreter dispatches each one to `f32_*` in `src/rv32_template.c` | Spike A's SoftFloat archive linked into rv32emu |
| Guest f64 intermediates inside musl's float-precision kernels (musl's `sinf` etc. compute in `double` internally) | RV32IMFC has F but no D, so the cart's compiled code emits libgcc soft-double calls (`__adddf3`, `__muldf3`, etc.); spike-b's `softfloat-glue.c` forwards them to `f64_*` | Spike A's SoftFloat archive linked into the cart |
| `sqrtf` | inline `fsqrt.s` from spike-b's `lua_runtime.c` → rv32emu interpreter → `f32_sqrt` | Same SoftFloat archive, accessed via rv32emu's dispatch |

So the spike already uses Berkeley SoftFloat for *all* guest FP, by
construction: rv32emu was built that way in Spike A, and spike-b's
softfloat-glue plus the `-mabi=ilp32f` ABI route every emulated double
through it as well.  The fact that the digest streams agree across hosts
is empirical evidence that nothing host-FP-dependent in rv32emu's own
host-side machinery (decoder loop, MIPS cap math, host `printf`) leaks
into the cart's stdout — which is the residual host-dependence vector
PLAN.md flagged as risk #1.

### `console` Lua global

Each Lua workload sees a single `console` table.  The bindings are
intentionally narrow — no I/O, no time, no other side channels:

```
console.rng()           → uint32 (PCG32 next)
console.unit_float()    → f32 in [0,1) (24-bit / 2^24)
console.add_sin(f)      → fs.accum_sin += f
console.add_cos(f)      → fs.accum_cos += f
console.add_sqrt(f)     → fs.accum_sqrt += f
console.add_misc(f)     → fs.accum_misc += f
console.set_mob(i, x, y, vx, vy, state)
console.commit_frame()  → emit DIGEST + bump frame counter
```

### Build, run, diff

```sh
# from spikes/spike-d/:
make docker-build       # builds arm64 and amd64 chains (spike-a → b → d)
make elf-bytes          # hashes cart ELFs in each image; PASS/FAIL
make run                # writes digests/digests.{arm64,amd64}.txt
make diff               # diff -u + sha256sum; PASS/FAIL
make all                # all of the above
```

`make all` exits 0 on PASS and prints both `ELF-BYTES PASS` and
`DIGEST PASS`.

---

## What the first build found

Three issues surfaced before the carts emitted matching digests.  None
invalidated the architectural plan.

**1. Cross-host `<features.h>` divergence under musl headers.**  musl's
`*_data.h` files include `<features.h>` to pick up the `hidden` macro.
With nothing in the spike-d include path satisfying this, the cross
toolchain fell through to the host's `/usr/include/features.h`, which
chains into `bits/wordsize.h` (an architecture-specific glibc header
absent in the cross sysroot).  Resolved by shipping a 13-line
`features.h` shim in `lib/musl-libm/include/` that defines `hidden` and
nothing else.  This same shim path now keeps any future glibc-isms in
the data headers from leaking into the build.

**2. spike-b's runtime image had no source files.**  spike-d's
Dockerfile `FROM`s the spike-b runtime stage and re-invokes the build
with `-DHAVE_LIBM`, but the spike-b runtime stage shipped only built
ELFs — no `lua_runtime.c`, no Lua source tree, no `crt0.S`.  Spike-d's
build failed on `No rule to make target /spike-b/ports/rv32emu/lua_runtime.c`.
Resolved with a one-liner addition to spike-b's Dockerfile that copies
the relevant port sources and the Lua source tree (excluding spike-b's
own `build/` artefacts) into the runtime image.

**3. GCC LTO crashes under qemu-user-amd64 on Apple Silicon.**  The
amd64 spike-a build crashed inside `lto-wrapper`/`cc1` during rv32emu's
final link.  Stack trace ended in `lto_main()`; this is a known
qemu-user × GCC LTO interaction and is exactly the kind of host-side
divergence PLAN.md's third fallback step (`CONFIG_LTO=n`) anticipates.
Resolved by adding `FC32_DISABLE_LTO=1` as a `--build-arg` to spike-a's
Dockerfile, which `sed`s `CONFIG_LTO=y` → `CONFIG_LTO=n` in the rv32emu
config before the make step.  Spike-d's `docker-build-amd64` invokes
spike-a's docker build directly with this arg set; the arm64 path is
unaffected and keeps LTO on.

Disabling LTO does not change interpreter semantics (still a software
RV32IMFC interpreter) and the digest streams agree across the two
images, so the workaround is correctness-neutral.  Performance loss is
not relevant — Spike D is a correctness spike, not a perf spike.

---

## Answers to the open questions in PLAN.md

**1. Does rv32emu compiled on `linux/amd64` produce the same guest
semantics as rv32emu compiled on `linux/arm64`?**  Yes, end-to-end.
Same cart ELF (byte-identical, see ELF gate above) plus rv32emu built
on each host produces the same per-frame DIGEST stream.  Caveat: the
amd64 build runs with `CONFIG_LTO=n` (per issue 3 above); arm64 runs
with `CONFIG_LTO=y`.  Different LTO settings did not change observable
guest semantics.  This is consistent with rv32emu being a pure
interpreter — LTO can change codegen of the host-side decoder loop
without changing the guest-visible bytes the interpreter writes back
into rv32emu's memory image.

**2. Is musl libm cleanly portable to RV32IMFC freestanding?**  Yes,
for the float-precision subset.  The 14 transcendental + 5 helper + 4
data-table source files compile clean against the shim `libm.h` plus
`features.h`.  The only local additions needed in the freestanding
runtime are `scalbn`/`scalbnf` (one-line aliases of `ldexp`/`ldexpf`)
and the `<math.h>` superset (`isnan`, `float_t`, `double_t`).  No
`<errno.h>` or `<fenv.h>` symbols were exercised by any of the
linked-in float kernels — both the rv32emu execution and the digest
diff confirm that.  This translates one-for-one into ADR-0007's
production runtime requirement: musl's float-precision math is good as
the deterministic libm reference.

**3. Which RNG?**  PCG32 (Melissa O'Neill, 2014) with the published
default stream constant.  The spike pins seed `0xfc320001` and stream
`0x14057b7ef767814f`, and the RNG state is part of the digest, so any
PCG32 implementation drift between hosts would surface as a frame-0
mismatch.  None observed.

**4. What goes in the state buffer?**  The packed POD shown above —
frame counter, RNG state, four f32 accumulators (`accum_sin`,
`accum_cos`, `accum_sqrt`, `accum_misc`), and 64 mob slots
(`x`, `y`, `vx`, `vy`, `state`).  NaN-canonicalised on the write site;
hashed via FNV-1a-64.

**5. Does Whetstone need to live in the same binary as the Lua
workload?**  No, and the separation is load-bearing.  `whetstone.elf`
is pure C (no Lua VM in the loop) and runs the libm-heavy modules
directly.  `lua_cart_det_*.elf` runs the same Lua VM as spike-b under
the same interpreter.  Both ELFs produce bit-identical digest streams
across hosts; if a future change broke one but not the other, the
asymmetry would name the layer.

**6. Where does `printf("%g", float)` formatting happen?**  Audited.
The cart's `printf` is spike-b's hand-rolled `vsnprintf` (host-
independent by construction — runs in the guest).  Digest emission is
hex via `%08x%08x` of the FNV-1a halves, never `%g`/`%e`/`%f`.  No
`%g` format specifier appears anywhere in the spike's digest-relevant
code paths.

---

## What this spike decides

- Yes, the same RV32IMFC cart binary, run under rv32emu built and
  executed on `linux/amd64` and `linux/arm64` (Docker on Apple
  Silicon, qemu-user emulating amd64), produces bit-identical per-frame
  digests over a 30-frame workload that exercises f32 arithmetic,
  musl libm transcendentals, and a seedable RNG.
- The cart ELFs themselves are byte-identical across host
  toolchains (`riscv64-linux-gnu-gcc` 13 from Ubuntu 24.04 on both
  arm64 and amd64).  Toolchain pinning is implicit (same Ubuntu base,
  same package version, same source) but not yet explicit.
- musl 1.2.5's float-precision libm subset can be ported to
  freestanding RV32IMFC for cart use with a 13-line `features.h`
  shim, a self-contained `libm.h` shim, and no other surgery.  The
  ported kernels link clean and produce bit-stable output across the
  two hosts.
- Spike E and Spike F now have a reference digest stream
  (`digests.arm64.txt` / `digests.amd64.txt`, identical) to compare
  their WASM runs against.

## What this spike does not decide

- **WASM-target determinism.**  Spike F (`spike-f-results.md`) has
  already revised the framing: Lua-direct-on-WASM uses Emscripten's
  vendored musl, not the same one this spike pinned.  Cross-stack
  determinism (rv32emu+Lua-in-RV32IMFC vs. Lua-direct-on-WASM) is a
  separate question, decided later, against the digest reference
  produced here.
- **Real-hardware confirmation.**  The amd64 side ran under
  qemu-user-amd64 inside Docker on Apple Silicon — three layers of
  emulation.  That answers the host-libc / host-toolchain question
  (qemu-user emulates user-space x86-64 faithfully enough for rv32emu
  to behave as if compiled and run on x86-64 glibc) but not "real
  Intel CPU vs. real Cortex-A53".  Because rv32emu is a software
  interpreter and SoftFloat handles all guest FP, host CPU FP behaviour
  should not influence the digest; a real-hardware run is a low-risk
  follow-up.
- **Toolchain pinning.**  Ubuntu 24.04's `gcc-13-riscv64-linux-gnu`
  package version happened to match across the arm64 and amd64 base
  images on the day of the spike (otherwise the ELF byte gate would
  have failed).  Production must pin the cross-compiler version
  explicitly in the Dockerfile.
- **Coroutine, GC, and audio determinism** named in ADR-0007.  The
  cart runtime here is small enough that none of these surfaces are
  exercised.  GC behaviour follows trivially from Lua-VM behaviour,
  which is what this spike measures via `lua_cart_det_*.elf`.
- **Production libm choice.**  This spike ports musl float-precision
  only.  ADR-0007 names musl as a reference, not as a binding
  constraint.  The fallback to openlibm is documented in PLAN.md but
  was not exercised — musl ported cleanly first time.
- **Input-event determinism** named in ADR-0007.  The carts run a
  fixed workload with no input.  Input snapshot semantics belong in
  the full runtime.
- **NaN canonicalization for cart-side state writes** beyond the
  spike's own digest emission.  Production needs a richer policy
  across every state-buffer write site; the spike only canonicalises
  inside `frame_state_emit_digest`.
- **Performance.**  No timing harness in spike-d; spike-b and spike-e
  cover throughput.
