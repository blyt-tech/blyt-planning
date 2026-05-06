# Spike D — implementation plan

**Question (per `docs/design/early-validation-spikes.md` §D):** does the same
cart workload, given the same inputs, produce bit-identical per-frame
output on two materially different host platforms — x86-64 Linux and
ARM64 Linux — without heroic effort?

**Dependency:** Spike B (Lua-in-RV32IMFC under rv32emu). Spike A only
indirectly: the rv32emu binary and SoftFloat archive that Spike B inherits.

**Success criterion:** A SHA-256 of the per-frame digest stream from a
load-bearing cart workload (Lua-in-RV32IMFC under rv32emu, with f32
arithmetic, transcendentals via a deterministic libm, and a seedable RNG)
is **bit-identical** between an `linux/amd64` host and an `linux/arm64`
host. Same outcome for a supplementary native-C Whetstone-shaped workload.
A pass means the digest streams match byte-for-byte without
post-processing. A fail means the fail is *diagnosed* — which frame, which
operation, which layer (interpreter, SoftFloat, libm, cart code, RNG) —
even if not yet resolved.

---

## Why this is a risk

ADR-0007 (structural determinism) says the platform is fully deterministic
given identical inputs. The theory is sound: the cart's RV32IMFC bytecode
is the canonical execution surface, rv32emu is a pure interpreter, and FP
goes through SoftFloat. In principle the same guest binary produces the
same bytes regardless of the host that runs the interpreter. In practice,
several places can leak host behaviour:

1. **Host libm via interpreter dispatch.** rv32emu builds with the host
   compiler; if any code path inside the interpreter uses host FP
   (statistics, throttle math, `printf("%g", …)` for timing output) the
   host libm shows up in *interpreter* output. We do not directly digest
   interpreter output, but timing and trace lines could end up mixed
   into stdout next to digest lines. Strict separation matters.

2. **Guest libm is stubbed.** Spike B's `lua_runtime.c` returns 0 from
   `sinf`/`cosf`/`expf`/`logf`/`powf`/`atan2f`. None of Spike B's
   benchmarks call them in hot paths, so the stack ran without anyone
   noticing. Spike D *does* exercise transcendentals — it has to, or the
   spike isn't testing what ADR-0007 names as the highest-risk surface.
   The guest needs a real, deterministic libm linked in. ADR-0007 names
   musl libm as the reference.

3. **SoftFloat correctness, not just SoftFloat use.** Spike A's archive
   compiles SoftFloat with `-O2` on the host compiler. SoftFloat is
   *designed* to be bit-identical, but the build is the host's gcc; if
   that compiler ever applies an FP-affecting optimization (it should
   not, under default flags) the output diverges. Pin the build flags
   and verify the SoftFloat archive's contents are byte-identical
   across the two host images.

4. **No RNG in the existing cart runtime.** Spike B's carts don't seed
   or call an RNG. ADR-0007 names RNG as a structural source of
   non-determinism that must be controlled — runtime-owned, seedable,
   tracked in state. The spike must add a guest-side seedable RNG and
   exercise it (mob spawn positions, projectile counts, AI random
   choices) so divergence in RNG state propagates into the digest.

5. **Compiler-flag drift across hosts.** If the `linux/amd64` Docker
   base image and the `linux/arm64` base image happen to use different
   gcc point releases with different default fast-math settings, the
   RV32IMFC cart binary itself may differ before it ever runs.
   `riscv64-linux-gnu-gcc` is the cross-compiler in both, but the
   Ubuntu package versions can drift. The cart ELF must be byte-identical
   across the two builds — that is the *first* check, before the run-
   time digest comparison.

6. **State-buffer serialization.** Spike B has no state-buffer concept
   yet. The digest definition is part of this spike: which fields, in
   which order, with which packing rules, hashed how. NaN canonicalization
   (ADR-0007) becomes load-bearing here — any uncanonicalized NaN in the
   buffer will propagate two valid bit patterns into the digest and
   create false-positive divergence on a host whose math happens to mint
   a different NaN bit pattern.

The spike's job is to walk through these and either eliminate them or
expose the one that breaks first.

---

## Key open questions going in

1. **Does rv32emu compiled on `linux/amd64` produce the same guest
   semantics as rv32emu compiled on `linux/arm64`?** The interpreter is
   pure C; SoftFloat is pure C; both should be bit-identical. But "should
   be" is the thing this spike tests. If the answer is no, find which
   compilation unit leaks host behaviour — most likely a `printf` format
   path, a libm call inside the throttle, or a compiler-introduced
   constant fold over guest-FP-shaped data.

2. **Is musl libm cleanly portable to RV32IMFC freestanding?** musl
   targets a hosted POSIX environment. Spike B's freestanding stubs
   replaced libm with returns-0; replacing them with real musl
   transcendentals means dropping selected `src/math/*.c` files into the
   build. Most of musl libm is pure C with no syscall surface; the
   awkward parts are `errno` writes and FP-environment inspection.
   Audit which musl files we need (`sinf`, `cosf`, `sqrtf`, `expf`,
   `logf`, `powf`, `atan2f`, plus any helpers) and confirm they don't
   pull in `<errno.h>` or `<fenv.h>` in ways our freestanding runtime
   can't satisfy. If they do, stub `errno` to a thread-local int and
   stub `feraiseexcept` to a no-op — both are documented as acceptable
   for deterministic floating-point per ADR-0007.

3. **Which RNG?** PCG32 (oneline `state = state*6364136223846793005ULL +
   inc; xsh = ((state>>18)^state)>>27; rot = state>>59;`) is small,
   well-specified, has a known reference implementation, and is trivial
   to reproduce. xoshiro256** is also fine. Pick one and pin it.
   The choice doesn't matter for the spike — the requirement is that
   the algorithm is fixed and reproducible, not that it is "the" cart
   RNG. Production may pick differently.

4. **What goes in the state buffer?** Minimum to make divergence
   visible: per-mob `(x, y, vx, vy)` as f32, the RNG state words, an
   accumulator for `sum += sin(angle)+cos(angle)+sqrt(d)` over the
   whole frame, and a frame counter. Layout the struct as a packed
   POD with explicit field order. Canonicalize NaN on write
   (`if (isnan(f)) f = canonical_qnan;`).

5. **Does Whetstone need to live in the same binary as the Lua
   workload?** No. Whetstone is a native-C cart ELF — same compile
   path as `doom_tick_c.c` from Spike B — that exercises f32
   transcendentals densely without the Lua layer. Running it as its
   own ELF isolates the libm question from the Lua VM question. Both
   ELFs must produce bit-identical digest streams; failures on one but
   not the other narrow the cause.

6. **Where does `printf("%g", float)` formatting happen?** Spike B's
   `snprintf` is a hand-rolled simplified `%g` that does not match
   glibc's rounding. It runs in the guest, so it is host-independent
   by construction — but the spike must not accidentally route any
   number formatting through the host. The digest emission is hex of
   raw bytes (no `%g`, no `%f`); the timing lines are integer
   microseconds. Audit the cart runtime to confirm no `%g`/`%e`/`%f`
   format specifier appears in digest-relevant code paths.

---

## Decisions

### Two-host strategy: Docker `linux/amd64` + Docker `linux/arm64`

ADR-0007 calls for cross-platform bit identity. The cheapest concrete
realisation is two Docker images on the same Apple Silicon host (one
running natively under `linux/arm64`, one under emulation via `qemu-user`
for `linux/amd64`). Same Dockerfile, two `--platform` invocations.

This is *not* a "real x86-64 Linux box vs. real ARM64 Linux box"
measurement. It is a "x86-64 toolchain producing rv32emu running on x86-64
glibc/libm" vs. "arm64 toolchain producing rv32emu running on arm64
glibc/libm", with both built and run inside Linux containers. That is the
question ADR-0007 cares about — host architecture and host libc are the
sources of variation we want to factor out. Apple Silicon's qemu-user
adds a layer of emulation on the amd64 side, but that emulates *user-
space x86-64*; from rv32emu's perspective it is running on an x86-64
glibc/libc/libm. If the digest streams agree, ADR-0007's amd64-vs-arm64
question is answered. If they disagree, we know to find a real x86-64 box
to confirm before declaring a true cross-host divergence.

### Build the same cart ELF in both images and compare bytes

Before running the cart, hash the cart ELF on each side. If the two ELFs
are byte-identical, host-toolchain drift is eliminated as a variable. If
they differ, the spike's first finding is a toolchain pinning failure
(probably `riscv64-linux-gnu-gcc` package version drift between Ubuntu
24.04 amd64 and arm64). Resolve by pinning the package version
explicitly in the Dockerfile.

### Digest scheme: 64-bit FNV-1a over a packed POD state struct, hex-emitted per frame

```c
struct frame_state {
    uint32_t frame;            // monotonic frame counter
    uint64_t rng_state;        // PCG32 state
    uint64_t rng_inc;          // PCG32 stream
    float    accum_sin;        // sum of sinf() calls this frame
    float    accum_cos;        // sum of cosf() calls this frame
    float    accum_sqrt;       // sum of sqrtf() calls this frame
    float    accum_misc;       // catch-all f32 accumulator
    struct {
        float x, y, vx, vy;
        uint32_t state;        // mob.state for AI dispatch
    } mobs[64];
} __attribute__((packed));
```

Canonicalize NaN on write into any field. After each frame, FNV-1a-64
the struct bytes and `printf("DIGEST %u %016llx\n", state.frame, hash)`.
That is the *only* thing on stdout from the cart per frame — no timing,
no debug, no Lua print output.

FNV-1a is simple, has no library dependency, fits in 20 lines of code,
and is deterministic by construction. SHA-256 would be cleaner but
brings cryptographic-library code into the guest for no benefit. Spike-
level overkill is wasted time.

### Transcendentals: musl libm subset, cross-compiled to RV32IMFC

Replace Spike B's stubs in `lua_runtime.c` with the actual musl
implementations of `sinf`, `cosf`, `tanf`, `sqrtf`, `expf`, `logf`,
`logf2`, `powf`, `atan2f`, `floorf`, `ceilf`, `fabsf`. Source files come
from upstream musl (vendored or fetched at build time, pinned to one
release). Compile with the same flags as the rest of the freestanding
runtime: `-march=rv32imfc_zicsr -mabi=ilp32f -O2 -ffreestanding -nostdlib
-fno-fast-math -ffp-contract=off -frounding-math -fsignaling-nans`. Stub
`errno` writes to a thread-local int.

ADR-0007 names musl as the reference. Using musl directly here means the
spike's pass-condition evidence (cross-host bit identity with real
transcendentals) translates one-for-one into ADR-0007's runtime
requirement — no second porting effort needed when the production
runtime gets built.

### RNG: PCG32 in the cart runtime, exposed to Lua

```c
// state.rng = state.rng * 6364136223846793005ULL + state.rng_inc;
// uint32_t xorshifted = ((state.rng >> 18u) ^ state.rng) >> 27u;
// uint32_t rot = state.rng >> 59u;
// uint32_t out = (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
```

Seed from a fixed integer at cart entry (`PCG_SEED 0xfc320001`,
`PCG_INC 0x14057b7ef767814fULL` — the published default). Expose to Lua
as `console.rng()` returning a uint32; Lua can derive integers and
floats from that. The cart's seed and stream are part of the digest.

### Workloads

Three workloads; each emits its own digest stream and is run on both
hosts.

| Workload | Source | What it stresses |
|----------|--------|------------------|
| `det_doom_tick`     | Spike B's `doom_tick.lua` plus PCG32-driven mob spawns and projectile counts | Lua VM + RV32IMFC guest + libm `sqrtf`/`atan2f` + RNG-driven control flow |
| `det_entity_update` | Spike B's `entity_update.lua` plus PCG32-perturbed initial positions and `sinf`/`cosf` per-tic angle update | Pure FP arithmetic through Lua, light branching |
| `whetstone`         | Native C, classic Whetstone benchmark ported to f32 throughout | Dense f32 transcendentals with no Lua layer; isolates libm behaviour |

Each workload runs N frames (30 is enough; the digest stream is the
output, not timing) and emits N `DIGEST` lines. The cart exits cleanly.

### No timing harness

Spike D is a correctness spike. The Spike B / Spike E timing harnesses
are not used. The only output the spike cares about is the per-frame
DIGEST line stream.

---

## What to build

### 1. `lib/musl-libm/` — vendored musl libm subset

Pin to a specific musl release (e.g. 1.2.5). Vendor only the source
files needed for the transcendentals listed above plus their internal
helpers (the `__rem_pio2f`, `__sindf`, `__cosdf` family). Approximately
30 .c files; a few hundred lines of headers stripped down to what the
freestanding build needs.

`lib/musl-libm/Makefile` builds `libm-rv32.a` with the documented flags.

### 2. Updated `lua_runtime.c` with real transcendentals

Replace Spike B's `sinf`/`cosf`/etc. stubs with extern declarations from
musl. Link `libm-rv32.a` into the cart ELF after the freestanding
runtime stubs.

### 3. `cart_runtime/` — state buffer, digest emitter, RNG

```
cart_runtime/
├── digest.c           // FNV-1a-64, fixed
├── digest.h
├── frame_state.h      // struct frame_state layout
├── pcg32.c            // PCG32 reference impl
├── pcg32.h
└── nan_canon.h        // canonical_qnan(), canonicalize_nan()
```

Linked into every Spike D cart ELF (Lua and C alike). A small number of
`extern` calls: `frame_state_init()`, `frame_state_emit_digest(state)`,
`pcg32_next(state)`. The Lua-side bindings live in
`ports/rv32emu/lua_det_bindings.c` (a couple of `lua_pushcfunction`
registrations for `console.rng` and a hook into the cart's main loop
so the digest emits at the right point).

### 4. Spike D cart ELFs

Three new cart targets, all inheriting Spike B's port directory:

- `lua_cart_det_doom_tick.elf`     — `det_doom_tick.lua` embedded
- `lua_cart_det_entity_update.elf` — `det_entity_update.lua` embedded
- `whetstone.elf`                  — pure C Whetstone, no Lua

Each compiles the same way Spike B's carts do: `crt0.S` + `cart_runtime`
+ workload code + Lua VM (for the Lua carts) + freestanding runtime +
SoftFloat archive + `libm-rv32.a`.

### 5. Two Docker build paths

`Dockerfile` parameterised with `--platform` to produce both arm64 and
amd64 images from the same source tree.

```
.PHONY: docker-build-arm64
docker-build-arm64:
	docker build --platform linux/arm64 \
		--build-arg SPIKE_B_IMAGE=fc32-spike-b \
		-t fc32-spike-d-arm64 .

.PHONY: docker-build-amd64
docker-build-amd64:
	docker build --platform linux/amd64 \
		--build-arg SPIKE_B_IMAGE=fc32-spike-b \
		-t fc32-spike-d-amd64 .
```

Spike B's image is itself architecture-tagged via `linux/arm64` in its
own Makefile; for Spike D we add an amd64 variant of the spike-b base
image (one Docker invocation; same Dockerfile, different `--platform`).
That produces `fc32-spike-b-amd64` and `fc32-spike-b-arm64`. Spike D
inherits each.

### 6. ELF-byte-identity check

Before running, hash the cart ELFs in each image and compare. This is
the cheap first check.

```
make elf-bytes-arm64    # writes elfs.arm64.sha256
make elf-bytes-amd64    # writes elfs.amd64.sha256
make elf-bytes-diff     # diffs them
```

A fail at this stage ends the spike pending toolchain pinning fixes.

### 7. Run-and-diff harness

```
make run-arm64    # produces digests.arm64.txt (one DIGEST line per frame, per cart)
make run-amd64    # produces digests.amd64.txt
make diff         # diffs the two; sha256sum each; print PASS/FAIL
```

`digests.{arch}.txt` content shape:

```
=== det_doom_tick ===
DIGEST 0 7f1c4a9e3b56e8d2
DIGEST 1 a30b8e4f7d2c69b1
…
DIGEST 29 …
=== det_entity_update ===
DIGEST 0 …
…
=== whetstone ===
DIGEST 0 …
…
```

`make diff` runs `diff -u` on the two files and computes
`sha256sum digests.arm64.txt digests.amd64.txt`. PASS = exit 0 from
diff. FAIL = the diff output is the spike's primary diagnostic artefact.

If the two streams diverge, the spike's deliverable becomes a *bisection
report*: which cart, which frame, which fields in `frame_state` first
differ between the two hosts. That is enough to identify whether
divergence is in libm, RNG, Lua VM, interpreter, or SoftFloat.

---

## Build environment

```
spikes/spike-d/
├── PLAN.md                       (this file)
├── Dockerfile                    (parameterised by --platform)
├── Makefile
├── lib/
│   └── musl-libm/
│       ├── Makefile              # builds libm-rv32.a
│       ├── (vendored musl src)
│       └── (small header shims)
├── cart_runtime/
│   ├── digest.c
│   ├── digest.h
│   ├── frame_state.h
│   ├── nan_canon.h
│   ├── pcg32.c
│   └── pcg32.h
├── ports/
│   └── rv32emu/
│       ├── Makefile              # builds the three cart ELFs
│       ├── lua_det_bindings.c
│       ├── whetstone.c
│       └── (re-uses spike-b's lua_runtime.c, lua_init_libs.c, crt0.S)
├── workloads/
│   ├── det_doom_tick.lua
│   └── det_entity_update.lua
├── digests/                      # output dir: digests.arm64.txt, digests.amd64.txt
└── elfs/                         # cart ELFs after build (gitignored)
```

The Dockerfile inherits from `fc32-spike-b` (or `fc32-spike-b-amd64` /
`fc32-spike-b-arm64` as appropriate), reusing Spike B's Lua build and
the spike-a-derived rv32emu binary unchanged. The only Spike D additions
to the image are `libm-rv32.a`, `cart_runtime/`, and the three new cart
ELFs.

---

## What this spike decides

- Whether the same RV32IMFC cart binary, run under `rv32emu` built and
  executed on `linux/amd64` and `linux/arm64`, produces bit-identical
  per-frame digests over a workload that exercises f32 arithmetic,
  transcendentals from musl libm, and a seedable RNG.
- Whether the cart ELFs themselves are byte-identical across host
  toolchains (a precondition for the runtime comparison to be meaningful).
- Whether musl libm can be ported to freestanding RV32IMFC for cart use,
  proving out the production transcendental library plan from ADR-0007.
- The reference digest stream that Spike E and Spike F will compare
  their WASM runs against (deferred until D produces digests, per E's
  and F's plan documents).

## What this spike does not decide

- **WASM-target determinism.** Spike F (`docs/design/spike-f-results.md`)
  has already revised the framing — Lua-direct-on-WASM uses a different
  execution model and a different libm (Emscripten's vendored musl). The
  WASM target's determinism is a separate cross-stack question, decided
  later, against the digest reference produced here.
- **Real x86-64 hardware.** The spike runs amd64 under qemu-user inside
  Docker on Apple Silicon. That answers the host-libc / host-toolchain
  question but not the "real Intel CPU vs real ARM CPU" question.
  Because rv32emu is a software interpreter and SoftFloat handles all
  guest FP, host CPU FP behaviour should not influence the digest. If
  the in-Docker spike passes, a real-hardware run is a low-risk follow-
  up.
- **Coroutine, GC, and audio determinism** named in ADR-0007. The cart
  runtime here is small enough that none of these surfaces are
  exercised. Spike B already showed the GC behaves consistently across
  identical runs on the same host; cross-host GC behaviour follows
  trivially from cross-host VM behaviour, which is what this spike
  measures.
- **Production libm choice.** This spike ports musl. The production
  console may select a different deterministic libm; ADR-0007 specifies
  musl as a reference, not as a binding constraint.
- **Input-event determinism** named in ADR-0007. The carts here run a
  fixed workload with no input. Input snapshot semantics belong in the
  full runtime.
- **NaN canonicalization for cart-side state writes**, beyond the spike's
  own digest emission. Production needs a richer policy across every
  state-buffer write site; the spike only canonicalizes inside its own
  `frame_state` writer.

---

## Fallback plan

If the digest streams diverge and root-causing is hard, narrow scope in
this order:

1. **Drop transcendentals.** Run a Whetstone variant that uses only
   add/sub/mul/div + `sqrtf` (which is the bare RV32 `fsqrt.s`
   instruction, software-implemented by SoftFloat — no libm involvement).
   If this passes on both hosts, divergence is isolated to musl libm
   under cross-host compilation, which is a contained fix
   (compiler-flag pinning or musl version pinning).

2. **Drop Lua.** Run the Whetstone-only digest comparison. If it
   passes, divergence is in the Lua VM or its interaction with
   rv32emu, not in the FP layer. Inspect Lua's `lvm.c` for any
   host-FP-leakage paths (`luaO_str2num`, `lua_Number` formatting).

3. **Drop rv32emu optimisations.** Re-run with
   `CONFIG_MOP_FUSION=n`, `CONFIG_BLOCK_CHAINING=n`, and
   `CONFIG_LTO=n` (per Spike A's `rv32emu.config`). If divergence
   resolves, one of those rv32emu features is host-sensitive and
   should be reported upstream.

4. **Bisect by frame.** The digest emits per frame; the first
   divergent frame names the operation. If frame 0 already differs,
   the disagreement is in initialisation (RNG seed, NaN
   canonicalization, mob-array zero-fill ordering). If frame >0 is
   the first divergence, the per-frame work is the suspect.

5. **Bisect by field.** Replace `frame_state` digest with per-field
   digests temporarily; the first diverging field names the layer
   (RNG → PCG32; `accum_sin` → libm; `mobs[].x` → physics integration;
   `mobs[].state` → AI dispatch / Lua VM).

If musl libm proves too painful to port to freestanding RV32IMFC (errno,
fenv, or weak-symbol surface that doesn't cleanly resolve under
`-nostdlib`), the fallback is **openlibm** — a freestanding-friendly fork
maintained for the Julia project, MIT-licensed, used in similar embedded
contexts. That trades the ADR-0007 reference choice for portability;
document the swap in the results file and flag for ADR follow-up.

If the amd64 Docker run is too slow under qemu-user for a 30-frame
workload (rv32emu interpreting RV32IMFC inside qemu-user-amd64 inside
Docker on Apple Silicon is three layers of emulation), drop the inner
workload size — the spike measures determinism, not performance, so 5
frames per workload still produces a meaningful digest stream.
