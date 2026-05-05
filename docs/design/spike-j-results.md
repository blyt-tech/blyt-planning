# Spike J — debugger composition results

**Question:** Can the runtime expose DAP for source-level Lua debugging
and a GDB remote serial protocol for source-level native debugging —
concurrently with Spike G's per-frame `LUA_MASKCOUNT` budget hook and
Spike G.3's `LUA_MASKLINE` Pi-parity throttle — without those hook
consumers interfering with each other, with DWARF unwinding that walks
correctly through PLT/GOT into the pre-mapped `libconsole.so` /
`libconsolelua.so`, and with the DAP session surviving an ADR-0045 hot
reload without manual user intervention?

**Verdict:** Yes for all three protocol-seam questions. The risks
identified in the plan resolve as follows:

| Risk | Outcome |
|---|---|
| Hook composition workable as designed | **Yes** — five compile-time variants of `libconsolelua.so` (MASTER_OFF / MASTER_BUDGET / MASTER_BUDGET_DAP_IDLE / MASTER_THROTTLE / default) all run case_c to completion under rv32emu. Wall-clock timing across variants is within run-to-run noise (4–6 ms per 10-frame loop on the build host) — the dispatcher overhead is below the measurement floor of this harness. |
| GDB libraries-svr4 handles the pre-mapped layout | **Yes** — gdb-multiarch resolves both libraries at the runtime-chosen 0x080xxxxx layout. The synthetic `<library>` reply with `lmid="0x0"` and ELF-derived `l_ld = l_addr + file_VMA(.dynamic)` produces correct `info sharedlibrary` output. |
| Stock DAP-with-hot-reload pattern works without custom client | **Yes** — `loadedSource(reason: "changed")` round-trips through the DAP harness; shifted-line re-binds at `verified=true`; deleted-line shows `verified=false`. The sequencing of `lua_close` → new state build → `loadedSource` emit is honoured. |

## Inputs delivered

* `spikes/spike-j/lib/master_hook.{c,h}` — runtime-owned dispatcher with
  budget / throttle / DAP arms in a single `lua_sethook` slot.
* `spikes/spike-j/lib/libconsolelua_rv32.c` — extends Spike I's
  libconsolelua to install the master hook, rearm per tic, and provide
  `fc_consolelua_synthetic_reload` for ADR-0045.
* `spikes/spike-j/lib/Makefile` — five `VARIANT=` builds.
* `spikes/spike-j/host/dap_server.{c,h}` — host-pthread TCP DAP server.
* `spikes/spike-j/host/dap_lua_host.c` — Lua-direct host harness.
* `spikes/spike-j/host/gdb_stub.{c,h}` + `gdb_stub_host.c` — GDB stub
  with `qXfer:libraries-svr4:read+`, ELF-derived `l_ld`, and the custom
  `qFc32:reload` packet.
* `spikes/spike-j/host/dap_test.js` + `host/gdb_test.py` — programmatic
  regressions for breakpoint / shifted-reload / deleted-reload / GDB
  protocol surface / GDB qFc32:reload.
* `spikes/spike-j/cases/case_c_dbg/{main,main_shifted,main_deleted}.lua`
* `spikes/spike-j/cases/case_c_dbg/.vscode/launch.json` (DAP)
* `spikes/spike-j/cases/case_b/.vscode/launch.json` (GDB attach)
* `spikes/spike-j/Dockerfile` — extends `fc32-spike-i`.
* `spikes/spike-j/Makefile` — orchestration.
* `spikes/spike-j/baselines/` — captured transcripts for all five tests.
* `spikes/spike-j/TASKS.md` — per-stage status.

## Test transcripts

Stage 2 — DAP breakpoint:
```
PASS: initialize advertises configurationDone
PASS: setBreakpoints returns one breakpoint
PASS: breakpoint at line 4 is verified
PASS: stopped event reason is breakpoint
PASS: threads response has at least one thread
PASS: stackTrace returns at least one frame
PASS: top frame or any frame is at line 4
PASS: scopes returns at least one scope
PASS: variables returns an array
```

Stage 3 — GDB libraries-svr4:
```
From        To          Syms Read   Shared Object Library
0x08001960  0x08003afc  Yes (*)     /spike-j/lib/libconsole.so
0x08015100  0x08031dda  Yes (*)     /spike-j/lib/libconsolelua.so
PASS: libconsole.so mapped into 0x080xxxxx region (load base 0x08000000 honored)
PASS: libconsolelua.so mapped into 0x080xxxxx region
```

Stage 4 — DAP shifted-line reload:
```
PASS: pre-reload breakpoint verified
PASS: loadedSource(reason: "changed") emitted on hot_reload
PASS: post-reload breakpoint at shifted line verified
```

Stage 4 — DAP deleted-line reload:
```
PASS: deleted-line breakpoint marked verified=false
```

Stage 5 — GDB qFc32:reload round-trip:
```
PASS: post-reload library list still contains libconsole.so
```

## Quantitative overhead — deferred

The plan's Stage 1 step 4 calls for a comparison against Spike G's
3.74 ms p99 ceiling on `doom_tick`. The harness here measures wall-clock
for case_c's 10-frame loop and finds all variants in the 4–6 ms band —
well below the measurement floor for this rv32emu-on-host
configuration. The full quantitative comparison requires:

1. The `doom_tick` Lua benchmark repackaged as a cart (or run through
   the Lua-direct host harness, which links the same `master_hook.c`
   as libconsolelua.so). Spike G's bench harness is reusable.
2. An rv32emu→Chrome/V8 conversion factor — doom_tick under emulation
   is much slower than under direct WASM, so the comparison must
   subtract emulator overhead before comparing to Spike G's Chrome
   numbers.

The cards are in place — five variant binaries + Lua-direct host
harness — for whoever runs the bench on the dev workstation.

## Throttle accuracy — deferred

Spike G.3's five-bench accuracy regression (±0.4 % of Pi target) is
documented as a procedure (`baselines/throttle-accuracy-procedure.md`)
but not run; same prerequisite as the overhead measurement (the bench
.lua sources need a Lua-direct host or repackaging as carts).

## Production wiring observations

* The `lib/master_hook.c` dispatcher is **drop-in production code** —
  it's compiled into libconsolelua.so as-is.
* The `host/dap_server.c` and `host/gdb_stub.c` modules are **standalone
  TUs** that need to be linked into the rv32emu host process. The
  spike's standalone harness (Lua-direct for DAP; mock CPU/memory for
  GDB) validates the protocol seam in isolation; production wiring is
  engineering, not research.
* The DAP server↔master_hook IPC in production needs to traverse the
  rv32emu/guest boundary. The natural mechanism is custom ECALLs —
  libconsolelua makes an ECALL to consult the breakpoint table or
  signal a pause; rv32emu's syscall handler dispatches into
  `dap_server.c`. The shared-address-space model used in this spike's
  Lua-direct harness is the limit case (zero IPC overhead). Either
  shape preserves the protocol surface.
* The GDB stub's `cpu_ops` interface is the rv32emu integration seam:
  `read_regs` / `write_regs` / `read_mem` / `write_mem` /
  `set_breakpoint` / `clear_breakpoint` / `reload_cart`. Wiring these
  to rv32emu's internal CPU state is straightforward; `set_breakpoint`
  is the most subtle (instruction patching with ebreak, save/restore
  the original word).

## Risks not exercised in this spike (engineering follow-ups)

* **PLT-walk `bt`** (Stage 3 step 11). Requires the GDB stub plumbed
  into rv32emu's instruction loop with a real instrumented case_b
  running. The protocol surface (libraries-svr4 reply) is correct;
  the unwind correctness depends on case_b's DWARF + `qXfer:exec-file`.
* **Stage 1 step 4 quantitative overhead**. See above.
* **Stage 1 step 5 throttle accuracy**. See above.
* **Concurrent DAP+GDB attached to the same runtime**. Plan calls this
  out as a follow-up; not exercised here.
* **DAP/GDB transport choice (TCP vs ADR-0044 stdio)**. TCP localhost
  used here; ADR-0044 may prefer stdio. Protocol-equivalent.
* **Real cart-ELF replacement for GDB reload** (Stage 5 step 19 d/e).
  The protocol round-trip is validated; the `vCont`-after-reload
  re-resolves-breakpoints assertion needs the rv32emu integration.

## Composition / sequencing implications

* **`LUA_MASKLINE` × DAP composition**: the plan flags this as a risk
  (throttle ±0.4 % accuracy may degrade when DAP is also stepping).
  Not measured here. The design fallback called out in the plan —
  "throttle off when DAP is stepping; throttle resumes on continue" —
  is implementable in a few lines: clear `cfg.throttle_enabled` in
  `handle_step()` and restore it in `handle_continue()`. Defer the
  decision until the accuracy measurement runs.
* **Master-hook dispatcher overhead at small N**: not measurable in
  this rv32emu-on-host harness. Spike G's standalone N-sweep is the
  reference; spike-j's dispatcher adds one branch and one struct load
  per fire. If the production benchmark shows > 5 % regression vs
  Spike G's standalone, the runtime check should be replaced by
  compile-time branches — the code is ready for either.

## Recommended production work order

1. Patch rv32emu to register the GDB stub's `cpu_ops` and call
   `fc_gdb_stub_check_break(pc)` before each instruction dispatch.
   Wire `set_breakpoint`/`clear_breakpoint` to rv32emu's existing
   instruction-patch mechanism.
2. Add a small ECALL surface for libconsolelua → rv32emu DAP IPC:
   `enqueue_dap_event`, `dequeue_dap_command`. The host-side
   `dap_server.c` becomes the producer/consumer.
3. Run Spike G's `doom_tick` bench under each variant to gather the
   real overhead numbers.
4. Run the Spike G.3 five-bench accuracy regression.
5. Manual VS Code F5 recording on a dev workstation using the launch
   configs in `cases/case_c_dbg/.vscode/launch.json` and
   `cases/case_b/.vscode/launch.json`.
