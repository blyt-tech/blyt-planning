# Spike J — task tracker

Stage-by-stage checklist. ✓ = validated end-to-end; ▢ = scaffolded /
deferred (engineering follow-up). The full result write-up lives at
`docs/design/spike-j-results.md`.

## Stage 1 — master hook composition

- ✓ `lib/master_hook.{c,h}` — dispatcher with budget / throttle / DAP arms
- ✓ `lib/libconsolelua_rv32.c` — extends Spike I's libconsolelua to install
  the master hook at lua_State creation, rearm per tic, and provide
  `fc_consolelua_synthetic_reload`
- ✓ Five compile-time variants (`MASTER_OFF` / `MASTER_BUDGET` /
  `MASTER_BUDGET_DAP_IDLE` / `MASTER_THROTTLE` / default)
- ✓ All five variants build cleanly under `make docker-build`
- ✓ All five variants run case_c to completion under rv32emu (smoke test
  in `baselines/master-overhead-*.txt`)
- ▢ Stage 1 step 4 overhead measurement vs Spike G's 3.74 ms p99 ceiling
  on `doom_tick`. Wall-clock per case_c run is in the 4–6 ms band across
  variants — within timer noise. Quantitative comparison needs the
  `doom_tick` bench repackaged or run through a Lua-direct host. See
  `docs/design/spike-j-results.md` for the deferral note.
- ▢ Stage 1 step 5 throttle accuracy regression on the 5-bench set. See
  `baselines/throttle-accuracy-procedure.md`.

## Stage 2 — DAP server

- ✓ `host/dap_server.{c,h}` — host-pthread TCP listener + handlers for
  initialize / setBreakpoints / threads / stackTrace / scopes / variables
  / continue / next / stepIn / stepOut / pause / disconnect / terminate
  / custom hot_reload
- ✓ `host/dap_lua_host.c` — Lua-direct host harness
- ✓ `cases/case_c_dbg/main.lua` — case_c plus `step_label` helper
- ✓ `host/dap_test.js` (mode `breakpoint`) — programmatic regression
  passing all 9 assertions; transcript at `baselines/dap-test.txt`
- ▢ Stage 2 step 8 manual VS Code recording (developer workstation).

## Stage 3 — GDB stub

- ✓ `host/gdb_stub.{c,h}` — qSupported / qXfer:exec-file:read /
  qXfer:libraries-svr4:read / g / G / m / M / Z0 / z0 / vCont /
  qFc32:reload
- ✓ `host/gdb_stub_host.c` — standalone harness with ELF-derived
  `l_ld` parsing
- ✓ `host/gdb_test.py` — gdb-multiarch driver passing 4 assertions on
  `info sharedlibrary`; transcript at `baselines/gdb-test.txt`. Both
  libraries resolve at the runtime-chosen 0x080xxxxx layout.
- ▢ Stage 3 step 11 PLT-walk `bt` — needs rv32emu integration (cpu_ops
  hooked into instruction loop).
- ▢ Stage 3 step 13 case_d `.cart.resources` byte read-back — same
  rv32emu-integration prerequisite.

## Stage 4 — DAP reload-while-debugging

- ✓ `lib/libconsolelua_rv32.c::fc_consolelua_synthetic_reload`
- ✓ `host/dap_server.c::handle_hot_reload`
- ✓ `cases/case_c_dbg/main_shifted.lua` — five-comment-line variant
- ✓ `cases/case_c_dbg/main_deleted.lua` — `step_label` removed
- ✓ `host/dap_test.js` mode `reload-shifted` — transcript at
  `baselines/dap-reload-shifted.txt`
- ✓ `host/dap_test.js` mode `reload-deleted` — transcript at
  `baselines/dap-reload-deleted.txt`
- ▢ Stage 4 step 17 edge case (new function above old breakpoint line) —
  property of VS Code's marker tracking; manual observation only.

## Stage 5 — GDB reload-while-debugging

- ✓ `gdb_stub.c::handle_qFc32_reload` — `T05library:;` stop-reply emit
- ✓ `host/gdb_test.py` mode `reload` — transcript at
  `baselines/gdb-reload.txt`
- ▢ Stage 5 step 19 d/e — actual breakpoint re-resolution after a
  cart-ELF replacement; needs rv32emu integration.
- ▢ Stage 5 step 20 deleted-symbol pending-breakpoint variant — needs
  `case_b_optional` cart and rv32emu integration.

## Stage 6 — VS Code launch configurations

- ✓ `cases/case_c_dbg/.vscode/launch.json` (DAP, port 5678)
- ✓ `cases/case_b/.vscode/launch.json` (Native Debug GDB attach, port 1234)
- ▢ F5-end-to-end recording on a clean VS Code profile (developer
  workstation).

## Risk-area outcomes

| Risk | Outcome |
|---|---|
| Hook composition workable as designed | **Yes** — five variants build and run; carts complete; dispatcher overhead below the harness measurement floor |
| GDB libraries-svr4 handles the pre-mapped layout | **Yes** — both libraries resolve at runtime-chosen 0x080xxxxx with ELF-derived `l_ld` |
| Stock DAP-with-hot-reload pattern works without custom client | **Yes** — `loadedSource(reason: "changed")` + verified-line re-binding round-trip cleanly |

See `docs/design/spike-j-results.md` for the full write-up, transcripts,
and the production-wiring observations.
