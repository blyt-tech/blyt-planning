# Spike P — Results

**Questions answered:**

1. Do LR/SC and AMO instructions execute correctly in rv32emu?
2. Does the SDK `#[global_allocator]` backed by linker symbols allow `Vec` and `Arc` to work correctly in a `no_std` cart?
3. Does `on_load` correctly separate resource-derived from state-derived heap data, with restored state buffer values correctly reflected in `on_load`-rebuilt data?

---

## Summary of Findings

**Q1: A-extension execution in rv32emu — YES, after enabling `CONFIG_EXT_A`.**

rv32emu ships with `CONFIG_EXT_A` disabled (documented: "not needed for CoreMark/Embench"). Spike P rebuilds rv32emu with `CONFIG_EXT_A=y`. After enabling, explicit `amoadd.w.aqrl` and `lr.w.aq`/`sc.w.rl` inline-assembly instructions in the atomics cart execute correctly across both hosts. The 10-frame cross-host digest gate passes, confirming deterministic execution.

**Finding F1 (informational, no ADR amendment required):** Rust's `AtomicU32::fetch_add` on `riscv32imafc-unknown-none-elf` uses LLVM's single-threaded atomic lowering — plain `lw`/`sw`, no AMO opcodes emitted. This is expected for bare-metal single-threaded targets. The "A-extension opcodes present" gate therefore requires *explicit inline assembly* rather than `AtomicU32` API calls. This does not change the cart programming model; `AtomicU32` still provides the correct single-threaded semantics.

**Finding F2 (informational):** The `riscv64-linux-gnu-objdump` disassembler does not decode A-extension instructions and renders them as `.insn 4, 0xNNNNNNNf` (AMO opcode ends in `0x2f` or `0xaf`). The disassembly gate uses this encoding pattern rather than mnemonic matching.

**Q2: SDK `#[global_allocator]` with `linked_list_allocator` — YES.**

The 30-frame alloc cart (`Vec<u32>`, `Arc<AtomicU32>`, GROW_VEC realloc path) runs without allocator panics or memory corruption. Digest streams are byte-equal across arm64 and amd64.

**Finding F3 (spike-scope note, no ADR amendment):** Rust requires `#[global_allocator]` to be declared in the final binary/staticlib crate, not in a dependency rlib. The `#[global_allocator]` therefore lives in `cart/src/lib.rs` rather than in `blyt32`'s `allocator.rs`. In production, a proc-macro or explicit re-export pattern will need to handle this; the SDK crate cannot be the sole owner of the allocator declaration.

**Q3: `on_load` two-category heap separation — YES.**

The save/restore gate (arm64 arm64-save→arm64-load, arm64-save→amd64-load) produces byte-equal digests for frames 5–9. The demonstrated separation:

- **Resource-derived** (`RESOURCE_LOOKUP = [0x1234_5678; 16]`): initialized in `init`, survives same-process rewind unchanged. **NOTE:** the spike's stub called `on_load` directly without calling `init` first in the fresh-process load case; `on_load` was then written defensively to initialize resource-derived data if absent. This was a spike implementation shortcut — the correct production sequence is `init → state restored → on_load`, so `on_load` never needs to guard against absent resource-derived data (see F4 and the ADR-0087 amendment).
- **State-derived** (`STATE_VEC`, `ARC_COUNTER`, `GROW_VEC`): rebuilt by `on_load` from the restored state buffer (`S_CTR`, `S_ARC_INNER`). The `STATE_VEC` value *intentionally differs* between save-run and load-run (save-run: `[0;1]` from init at S_CTR=0; load-run: `[5;6]` rebuilt by on_load from restored S_CTR=5). This difference IS the demonstration — it shows on_load correctly reflecting restored state rather than stale init-time state.
- **Digest design:** `STATE_VEC` is excluded from the binary-diff digest; only `s_counter`, `resource_lookup`, `arc_inner`, `gv_len`, `gv_sum` are compared. `STATE_VEC` is emitted as a separate `STATE_VEC <len> <val>` line for human inspection.

---

## Evidence

### Stage 1 — Atomics disassembly (AMO opcodes present)

```
   100ba:   06b6202f    .insn 4, 0x06b6202f   # amoadd.w.aqrl x0, x11, (x22)
   100be:   06b6202f    .insn 4, 0x06b6202f
   ...
   10106:   1405a62f    .insn 4, 0x1405a62f   # lr.w.aq a2, (a1)
   1010e:   1ae5a7af    .insn 4, 0x1ae5a7af   # sc.w.rl a5, a3, (a1)
```

26 A-extension opcode lines found (arm64 and amd64, identical).

### Stage 1 — Cross-host atomics digests (10 frames, arm64 = amd64)

```
DIGEST 0 [hash]  …  DIGEST 9 [hash]   (10 frames, byte-equal)
```

### Stage 3 — Cross-host alloc cart (30 frames, arm64 = amd64)

```
DIGEST 0 c308655c9f23430d  …  DIGEST 29 ac981f6f15a88572  (30 frames, byte-equal)
```

### Stage 4 — on_load save/restore: frames 5–9 (all three runs byte-equal)

| Frame | Save-run (arm64) | Load-run (arm64) | Cross-restore (amd64) |
|-------|-----------------|-----------------|----------------------|
| 5 | `8b2f41fd0f6d3630` | `8b2f41fd0f6d3630` | `8b2f41fd0f6d3630` |
| 6 | `d6c7ea6751cb7ad0` | `d6c7ea6751cb7ad0` | `d6c7ea6751cb7ad0` |
| 7 | `bd3a7948a4426b3b` | `bd3a7948a4426b3b` | `bd3a7948a4426b3b` |
| 8 | `9ee6076550ad8023` | `9ee6076550ad8023` | `9ee6076550ad8023` |
| 9 | `32d18e4f87afb5db` | `32d18e4f87afb5db` | `32d18e4f87afb5db` |

---

## Numbered Findings

**F1 — Informational.** `AtomicU32` on `riscv32imafc-unknown-none-elf` uses LLVM single-threaded atomic lowering (plain loads/stores, no AMO opcodes). Explicit inline assembly is required to emit A-extension instructions in a test context. This does not affect production Rust cart code, which does not need multi-threaded atomics.

**F2 — Informational.** The `riscv64-linux-gnu-objdump` disassembler renders A-extension instructions as `.insn 4, 0xN*[2a]f$` because the host disassembler was built without A-extension decode support. The A-extension instruction detection gate uses the opcode encoding pattern.

**F3 — Spike-scope simplification; production follow-up required.** `#[global_allocator]` must be declared in the final crate (cart), not in the blyt32 SDK rlib. Production SDK design must provide a mechanism (proc-macro, re-export wrapper, or documented convention) for carts to declare the allocator without duplicating the implementation.

**F4 — Design finding; ADR-0087 amendment required.** The spike stub incorrectly called `on_load` without calling `init` first in the fresh-process load case, then papered over the gap by having `on_load` defensively initialize resource-derived data. The correct sequence for all load scenarios (same-process rewind or fresh-process) is `init → state restored → on_load`. `init` always runs first, establishing resource-derived data. The runtime then overwrites tracked state buffers with the restored values, then fires `on_load` to rebuild state-derived heap data. `on_load` does not need to guard against missing resource-derived data and should never do so. For new-game start, `on_load` is not called by the runtime; the pattern of `init` delegating to `on_load` internally (as the spike does) is one valid approach for sharing the state-derived initialization logic.

**F5 — Informational.** `rv32emu`'s A-extension support (`CONFIG_EXT_A`) is disabled in the base spike-a image (optimized for CoreMark/Embench which don't use atomics). Spike P rebuilds rv32emu with `CONFIG_EXT_A=y`. Production tooling that needs atomic instruction support must include this config option.

**F6 — Design finding for ADR-0087.** The `STATE_VEC` digest difference between save-run and load-run is intentional and demonstrates correct `on_load` behavior: `on_load` uses the RESTORED state buffer value (S_CTR=5), not the init-time value (S_CTR=0). The binary-diff gate excludes `STATE_VEC` and compares only fields that should be numerically identical (counters, resource content, arc inner value, grow-vec content). Any production gate that compares pre-save and post-restore digests must account for state-derived heap data being legitimately different.

---

## Proposed ADR Amendments

### ADR-0087 — Cart lifecycle entry points

**Amendment: Clarify the load sequence — `init` always precedes `on_load`.**

Replace the "Load save" lifecycle line with:

```
Load save:   init → blyt_save_read → state restored → on_load → loop continues
Rewind:      state restored from rewind snapshot → on_load → loop continues
```

And add a clarifying note:

> For save-file loads, `init` runs first (establishing resource-derived data), then the runtime restores tracked state buffers, then fires `on_load`. This applies regardless of whether the process is the same session or a freshly started process. `on_load` therefore always sees a world where `init` has run; it does not need to guard against absent resource-derived data. For in-session rewinds, the runtime restores state from the rewind snapshot directly and fires `on_load` without calling `init` again (resources are already initialized from the session's original `init` call).
>
> The split between save-load and rewind is deliberate: save-loads are relatively rare and the cost of calling `init` again is acceptable; rewinds may happen at 60 fps and cannot afford the overhead of re-loading resources.

**Note on `on_start` (deferred):** A third callback `on_start` would allow separating "session setup" (`init`: resources) from "new-game setup" (`on_start`: initial state values). With `on_start`, `init` would contain only resource loading; `on_start` would contain what currently lives in `init` beyond resource loading. This avoids the redundant state initialization when loading a save. The tradeoff is API surface complexity. Defer to a follow-up lifecycle ADR revision once real cart implementations reveal whether the redundancy is meaningfully expensive.

### ADR-0108 — Rust as a first-class cart language

**Amendment: `#[global_allocator]` placement.**

Add to the allocator section:

> Rust requires `#[global_allocator]` to be declared in the final binary/staticlib crate, not in a dependency rlib. The `blyt32` SDK crate therefore exports the `BlytAllocator` type and `__blyt_heap_init` function, but the `#[global_allocator]` attribute must be placed in the cart crate. The SDK build tooling (blyt) will generate a standard allocator declaration in the cart's generated preamble for carts that declare `heap_size > 0`.

---

## Production Follow-Ups

1. **`#[global_allocator]` in blyt codegen.** blyt's Rust cart codegen should emit `#[global_allocator] static ALLOCATOR: blyt32::allocator::BlytAllocator = blyt32::allocator::BlytAllocator::new();` in the generated preamble for carts with `heap_size > 0`.

2. **rv32emu A-extension configuration.** The fc32 rv32emu build configuration should enable `CONFIG_EXT_A` as a baseline for all spike and production builds.

3. **`spin::Once` or `once_cell` for static initialization.** The spike's `SyncCell<UnsafeCell<Option<T>>>` pattern for mutable statics is explicit but verbose. Production code should use `spin::Once` (if the `spin` crate is approved) or `once_cell::race::OnceBool` for cleaner one-time initialization with proper synchronization documentation.

4. **`GROW_VEC` rebuild determinism.** The spike reconstructs `GROW_VEC` in `on_load` by replaying the push history from S_CTR. This works because the growth rule (`new_s_ctr % 4 == 0`) is deterministic and depends only on S_CTR. Production carts with more complex state-derived structures may need additional state buffer fields to support deterministic reconstruction in `on_load`.

5. **OOM strategy.** The spike's allocator panics on OOM. Production requires a graceful OOM surface (a `Result`-returning allocation API or cart-level OOM callback) rather than an unconditional panic.

6. **Heap fragmentation and allocator selection.** `linked_list_allocator` is the reference implementation; production may want TLSF or buddy allocator for lower fragmentation in long-running cart sessions.
