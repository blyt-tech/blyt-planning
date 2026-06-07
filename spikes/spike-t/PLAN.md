# Spike T — ECALL-bridged Lua C API on the WASM target

Validates ADR-0130. Spike definition and headline questions:
[`early-validation-spikes.md`](../../docs/design/early-validation-spikes.md#spike-t--ecall-bridged-lua-c-api-on-the-wasm-target).
Results (when run): [`spike-t-results.md`](../../docs/design/spike-t-results.md).

## Where the code lives

Unlike earlier spikes, Spike T runs against the production blyt runtime
rather than a standalone harness: the mechanisms under test (ECALL
dispatch, `.lua_exports` registration, the WASM frontend's coroutine
loop) already exist there, and a standalone reproduction would validate
the wrong code. Implementation is on the `spike-t-lua-bridge` branch of
the blyt repo (local; not pushed). This directory holds the test carts,
run scripts, and captured outputs/digests.

## Layout

- `carts/` — spike test carts (stage 1 pairs-order cart; stage 2 scalar
  bridge cart; stage 3 strings/errors cart; stage 4 tables cart; stage 5
  combined gate cart; overhead benchmark cart)
- `run-stage*.sh` — per-stage build + run + compare scripts (rv32 path
  via the integration-test QEMU harness; WASM path via node)
- `digests/` — captured per-path outputs for the byte-exact gate
- `results/` — raw measurement output for question (f)

## Stages

1. **Seed fix + `pairs()` parity** — fixed `luai_makeseed()` in all Lua
   builds; identical iteration order rv32 vs WASM.
2. **Bridge skeleton + scalar ops** — stub `libblyt32lua.so` variant,
   `BLYT_ECALL_LUA_OP = 10`, exchange thread, bridged trampoline,
   `flags` byte, one ported export; suspended-thread panic side
   experiment.
3. **Strings + error model** — `PUSHLSTRING`/`TOLSTRING` + arena/retry;
   `ERRMSG`/`ERROR` + snapshot/restore + `BLYT_RUN_FN_ERROR`.
4. **Tables** — `CREATETABLE`/`GETFIELD`/`SETFIELD`/`GETI`/`SETI`/
   `RAWLEN`/`NEXT`.
5. **Gate + measurements** — combined cart byte-exact gate (hard block
   for ADR-0130 acceptance); overhead numbers.

Exit criteria and failure fallbacks per headline question (a)–(f) are in
the spike definition.
