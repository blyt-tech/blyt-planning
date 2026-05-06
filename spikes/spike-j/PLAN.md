# Spike J — implementation plan

**Question (per `docs/design/early-validation-spikes.md` §J):** Can the
runtime expose DAP for source-level Lua debugging and a GDB remote serial
protocol for source-level native debugging — concurrently with Spike G's
per-frame `LUA_MASKCOUNT` budget hook and Spike G.3's `LUA_MASKLINE`
Pi-parity throttle — without those hook consumers interfering with each
other, with DWARF unwinding that walks correctly through PLT/GOT into the
pre-mapped `libconsole.so` / `libconsolelua.so` (Spike I's loader layout),
and with the DAP session surviving an ADR-0045 hot reload without manual
user intervention?

**Why this spike exists:** §21 of the high-level design treats debugger
support as engineering rather than research, but three load-bearing
assumptions in that framing have not been tested:

1. **One Lua hook slot, three would-be consumers.** Spike G owns the slot
   for `LUA_MASKCOUNT` budgeting; Spike G.3 owns it for `LUA_MASKLINE`
   throttling; the DAP server wants it for stepping and breakpoints. Lua
   exposes one hook slot per `lua_State`. Whether they can coexist by
   chaining through a runtime-owned `master_hook(L, ar)` — and still
   preserve Spike G's < 0.34 ms `doom_tick` p99 overhead and Spike G.3's
   ±0.4 % calibration accuracy — is unproven.

2. **DWARF unwinding through PLT into pre-mapped libraries at non-conventional
   addresses.** Spike I places `libconsole.so` at `0x08000000` and
   `libconsolelua.so` at the next 4 KiB-aligned slot above it — *below* the
   cart in guest memory. Whether GDB's standard `qOffsets` / shared-library
   reporting protocol handles this layout is unverified. Failure mode is
   silent: stepping appears to work but `bt` stops at the PLT.

3. **DAP session continuity across hot reload via stock VS Code behaviour.**
   ADR-0045 promises uninterrupted debugging across reload without any custom
   VS Code extension — the runtime emits `loadedSource(reason: "changed")`
   and VS Code re-sends `setBreakpoints` against the editor's current
   (line-shift-tracked) positions. Three failure modes silently degrade
   the headline edit-and-debug workflow: server forgets to emit
   `loadedSource`; server emits before the new cart is mapped (re-binds
   against stale code); GDB-side `library-loaded` / process-replacement
   notification is missing.

J validates the **debugger-side protocol seam** for all three risks. M
validates the snapshot/restore side; the production combination of "real
reload + real debugger" composes their results.

**Dependencies:**
- Spike G (`LUA_MASKCOUNT` budget hook, the chosen N, `hook_host.c` shim
  shape; the budget-firing and overhead-measurement harness).
- Spike G.3 (`LUA_MASKLINE` accumulated-debt throttle, `throttle_host.c`,
  the Pi-parity calibration constants).
- Spike I (the cart format end-to-end: `crt0.o`, `libconsole.so` runtime
  driver, `libconsolelua.so` Lua VM wrapper, the case b C cart, the case c
  Lua cart, `fc32_dynload` multi-library loader patch).
- ADR-0024 / ADR-0025 (cart ELF format, only `libconsole.so` /
  `libconsolelua.so` permitted DT_NEEDED).
- ADR-0045 (hot reload via save/restore; the DAP `hot_reload` custom
  command and the `loadedSource(reason: "changed")` continuity protocol).
- §21 of `docs/design/high-level-design.md` (the debugger feature surface
  this spike validates).

---

## Key design decisions

### `master_hook(L, ar)` is the only `lua_sethook` consumer

The runtime installs exactly one hook at `lua_State` creation — a
dispatcher in `libconsolelua.so` that fans out to whichever combination
of budget / throttle / DAP handlers is enabled for the current build. No
production component calls `lua_sethook` directly.

```c
// libconsolelua.so — runtime-owned, exactly one installation per lua_State
typedef struct {
    bool      budget_enabled;     // Spike G — LUA_MASKCOUNT path
    bool      throttle_enabled;   // Spike G.3 — LUA_MASKLINE path
    bool      dap_enabled;        // this spike — line/call/return as DAP needs
    uint64_t  budget_ns;
    uint64_t  budget_tic_start_ns;
    uint64_t  throttle_start_ns;
    uint64_t  throttle_target_ns;
    /* DAP state pointer omitted — see §DAP below */
} hook_config_t;

static void master_hook(lua_State *L, lua_Debug *ar) {
    if (cfg.budget_enabled   && ar->event == LUA_HOOKCOUNT) budget_check(L, ar);
    if (cfg.throttle_enabled && ar->event == LUA_HOOKLINE)  throttle_step(L, ar);
    if (cfg.dap_enabled)                                    dap_dispatch(L, ar);
}
```

The dispatch mask is the **union** of every enabled handler's
requirements: `LUA_MASKCOUNT | LUA_MASKLINE | LUA_MASKCALL | LUA_MASKRET`
when DAP is attached and stepping. The mask is recomputed and
`lua_sethook` re-installed only when the configuration changes (DAP
attach/detach; throttle disable from the dev UI). The hot path — every
call to `master_hook` — does not touch `lua_sethook`.

The dispatcher does **not** attempt to "skip" handlers based on event
type; it filters on `ar->event` per handler. This keeps the per-handler
cost identical to the standalone Spike G / Spike G.3 implementation
*except* for one extra branch and one extra struct load per fire.

### DAP server lives in `libconsolelua.so`, not `libconsole.so`

Per §21: "the Lua interpreter lives in the runtime's native address
space" — the runtime has full access to `lua_*` debug API. This spike
places the DAP server inside `libconsolelua.so` because that is the
binary that already owns the `lua_State` and the master hook
configuration. `libconsole.so` exposes a thin
`fc_console_dap_listen(int port)` entry point that `libconsolelua.so`
implements; production carts that don't use Lua get no DAP server (case
b's GDB-only path is correct for them).

The DAP server runs on a separate native thread (host-OS pthread, not
guest), reads the protocol on a TCP socket, and communicates with the
`master_hook` via a small command/response queue protected by a mutex.
The hook is the only consumer that runs inside the Lua VM — every state
transition (pause / step / continue / breakpoint-set) is requested by
the DAP thread and applied by the master hook on its next fire.

### GDB stub lives in rv32emu, not in either runtime library

The GDB remote serial protocol speaks at the *guest CPU* level —
register read/write, memory read, single-step, breakpoints — so it must
be inside the emulator. `libconsole.so` and `libconsolelua.so` are
guest-side libraries; they have no access to rv32emu's CPU state.

The stub is a separate TCP listener inside rv32emu (host thread; same
process, separate socket from DAP). It reports the pre-mapped library
load addresses to GDB via the standard `qXfer:libraries-svr4:read` reply
when the client requests it, *not* via `qOffsets` — `qOffsets` carries a
single (text, data, bss) triple and cannot describe two pre-mapped
libraries. The synthetic library list is built from `fc32_dynload`'s
in-memory map: cart at `0x00010000`, libconsole at `0x08000000`,
libconsolelua at the post-libconsole slot. Each library entry carries
the host filesystem path of the original `.so` so GDB can re-open it
locally for symbol load.

### Synthetic reload, not real reload

Per the spike scope: the reload-while-debugging tests use a **synthetic
reload** that exercises only the protocol seam, not the snapshot/restore
mechanism. Procedure for the DAP path:

1. Tear down the cart's `lua_State`.
2. Re-load the cart's Lua bytecode (possibly modified — line-shifted, or
   with the breakpoint line deleted).
3. Re-create a fresh `lua_State`, re-install the master hook, re-emit
   `loadedSource(reason: "changed")` for the cart's source path.

State migration is explicitly skipped — the goal is to validate that
the protocol seam (event emission timing, `setBreakpoints` re-binding,
`verified: true/false` semantics) works end-to-end with stock VS Code.
Real state-preserving reload is Spike N's scope (was Spike M before
the renumber that inserted the managed-coroutine spike at M).

For the GDB path the synthetic reload is simpler: `fc32_dynload` is
asked to replace the cart's PT_LOAD region with a freshly-built cart
ELF (different DWARF, possibly shifted line tables); the stub emits a
`library-loaded` notification (or, equivalently, simulates a process
replacement via `vCont` / stop-reply with a new `T05library:` packet).

---

## Inputs we already have

- **Spike G's `hook_host.c`**: the `LUA_MASKCOUNT` hook structure (per-tic
  rearm, `now_ns` reading, `luaL_error` on overrun) and the chosen
  production N. Spike J extracts the budget-check body into
  `budget_check(L, ar)` and calls it from `master_hook`. The
  `tic_start_ns` / `budget_ns` state moves into the shared `hook_config_t`.

- **Spike G.3's `throttle_host.c`**: the accumulated-debt throttle body
  (`start_ns` / `target_ns`, the per-line debt update, the spin-when-ahead
  branch) and the calibrated `ns_per_line` table. Spike J extracts the
  throttle body into `throttle_step(L, ar)` similarly.

- **Spike G's no-hook baselines (Chrome desktop)**: `doom_tick` mean
  1.71 ms / p99 3.40 ms; the +0.34 ms (10 %) overhead budget Spike G's
  `LUA_MASKCOUNT` already consumes most of. Spike J's overhead budget on
  top of Spike G's is a further ≤ 10 % of Spike F's p99 — i.e. the
  master-hook-with-DAP-attached-but-idle binary must stay within the same
  3.74 ms p99 ceiling Spike G hit. The spike J build with no DAP and no
  throttle (just the dispatcher around the budget body) must be
  *byte-overhead-equivalent* to Spike G's standalone build modulo the
  one extra branch and struct-load per fire.

- **Spike G.3's calibration table**: `ns_per_line` per bench at the 6×
  Pi target. Reused without re-derivation; throttle accuracy is
  re-measured (with the master hook in place) but not re-calibrated.

- **Spike I's runtime layout**: `libconsole.so` at `0x08000000`,
  `libconsolelua.so` at `0x08000000 + round_up_4k(libconsole_PT_LOAD)`,
  cart at `0x00010000`. Case b cart (`cart_b` with `mylib.c` in
  `.text.mylib`) is the GDB-stub target. Case c cart (`cart_c`,
  Lua-only) is the DAP target. `fc32_dynload`'s in-memory map is the
  data source for the GDB `qXfer:libraries-svr4:read` reply.

- **Spike I's case c `main.lua`**: the 10-line `init`/`update`/`draw`
  cart that emits `frame N`. Spike J modifies it to make breakpoint
  positions interesting (a function call from `update` into a helper, so
  step-in / step-out have observable effect; a local with a string value
  so locals inspection has a non-trivial result).

- **Spike I's `Dockerfile`**: build environment with the RV32IMFC
  toolchain, rv32emu sources, Lua 5.4.7, and Spike H's QEMU image. Spike
  J extends it with a host-side GDB build (`gdb-multiarch` for the
  `riscv:rv32` target) and Node modules for a small DAP client harness.

- **VS Code on host** (developer workstation): used for the F5 / launch
  config validation. The VS Code extensions required are stock — built-in
  DAP support and the standard "Native Debug" GDB-RSP extension. No
  custom extension code is part of this spike.

---

## What we are NOT building

- **Production debugger UI**: watchpoints, conditional breakpoints with
  full expression evaluation, reverse-step, multi-thread support. Once
  the composition story is settled these are engineering, not research.
- **Hardware GDB stub**: §21 calls for the GDB stub to also run on the
  native RISC-V hardware path, exposed over TCP from the dev-mode device
  image. That is a Spike H follow-up on real hardware, not this spike.
- **Full hot-reload mechanics**: snapshot, state migration, native-cart
  restart. Spike N's scope (was Spike M before the renumber). J
  validates only the *debugger-side protocol seam* using a synthetic
  reload that throws away game state.
- **DWARF symbol parsing inside the runtime**: GDB does the DWARF work
  on the host side after we hand it library paths via the libraries-svr4
  reply. The runtime never reads `.debug_*` sections.
- **A `hot_reload` DAP custom command in production form**: a minimal
  custom-command path that triggers the synthetic reload is wired up in
  the spike (the only DAP message the runtime acts on directly per
  ADR-0045), but signal-protocol details (Unix socket fallback,
  packer-watcher integration, layout migration) are out of scope.
- **Process-isolation considerations**: seccomp, namespaces, cgroups
  (Spike H). The DAP and GDB listeners bind to localhost on dev-mode
  ports; production will eventually gate them behind dev-mode builds per
  ADR-0065 and §21's hardware-debugging note, but the spike does not
  exercise that.
- **Mobile / Chrome / browser DAP**: this spike runs on the rv32emu
  emulator target only. The Lua-direct WASM path's DAP story (Spike F/G
  build) is structurally compatible — `master_hook` is the same code —
  but a browser-side TCP socket replacement (WebSocket bridge) is a
  follow-up, not this spike.

---

## Approach

Six stages. Stages 1 and 2 build the protocol-level machinery (master
hook, DAP server, GDB stub) in isolation. Stages 3 and 4 validate the
two reload-while-debugging seams. Stages 5 and 6 close on overhead and
the F5-end-to-end criterion.

### Stage 1 — master hook composition

1. Create `spikes/spike-j/` mirroring spike-i's layout: `Makefile`,
   `Dockerfile` (FROM `fc32-spike-i AS builder`), `lib/`, `cases/`,
   `patches/`, `host/` (for the new DAP server / GDB stub host code).
   Symlink `cases/case_b → ../../spike-i/cases/case_b` and
   `cases/case_c → ../../spike-i/cases/case_c` — this spike does not
   re-author the carts.

2. Author `lib/master_hook.c` inside `libconsolelua.so` (extend Spike I's
   `libconsolelua_rv32.c`). The structure:
   - `static hook_config_t cfg;` — single global (one `lua_State` per
     runtime in this spike).
   - `void master_hook(lua_State *L, lua_Debug *ar)` — dispatcher.
   - `static void budget_check(lua_State *L, lua_Debug *ar)` —
     verbatim from Spike G's `hook_fn` body, reading `cfg.budget_*`.
   - `static void throttle_step(lua_State *L, lua_Debug *ar)` — verbatim
     from Spike G.3's body, reading `cfg.throttle_*`.
   - `static void dap_dispatch(lua_State *L, lua_Debug *ar)` — stub
     for now (returns immediately); fleshed out in Stage 2.
   - `void fc_consolelua_master_hook_install(uint32_t mask, int count)` —
     the only `lua_sethook` call in the runtime; recomputes the mask
     from `cfg.*_enabled` and re-installs.

3. Build three `libconsolelua.so` variants for overhead measurement:
   - **MASTER_OFF** — `cfg.{budget,throttle,dap}_enabled = false`,
     `lua_sethook(L, NULL, 0, 0)`. Should be byte-equivalent to Spike I's
     `libconsolelua.so`. This is the no-hook baseline.
   - **MASTER_BUDGET** — `cfg.budget_enabled = true` only. Should match
     Spike G's measured overhead within ±5 %.
   - **MASTER_BUDGET_DAP_IDLE** — `cfg.budget_enabled = cfg.dap_enabled
     = true`, but no DAP client connected (the dispatch reaches
     `dap_dispatch` and returns immediately). This is the production
     idle case for the spike's headline number.

4. Run Spike I case c under each variant and confirm:
   - `MASTER_OFF` produces byte-identical FRAME output to Spike I's
     case c run.
   - `MASTER_BUDGET` p99 overhead vs MASTER_OFF is within ±5 % of Spike
     G's measured overhead. (Larger drift means the dispatcher itself is
     the bottleneck and the design needs revisiting.)
   - `MASTER_BUDGET_DAP_IDLE` p99 overhead vs MASTER_OFF is ≤ 10 % of
     Spike F's `doom_tick` p99 (3.74 ms ceiling).

   The benchmark is `doom_tick` running on rv32emu with the case-c-style
   shell wrapping it (Lua cart, libconsolelua's hook composition active).
   Re-use Spike G's bench harness; the rv32emu target is slower than
   Lua-direct WASM, so an additional same-target Spike-I-baseline pass
   is needed to subtract out the emulator overhead before comparing to
   Spike G's Chrome numbers. Document the rv32emu→Chrome conversion in
   the result write-up.

5. Throttle accuracy regression test: re-run Spike G.3 Stage 4 with
   MASTER_THROTTLE (= `cfg.throttle_enabled` only) on `doom_tick`,
   `entity_update`, `binarytrees`, `mandelbrot`, `spectral-norm`. The
   target accuracy is unchanged — within ±0.4 % of Pi target across
   the five benches. Any regression means the dispatcher's per-line
   overhead is non-negligible at `LUA_MASKLINE` granularity and the
   throttle's `ns_per_line` calibration needs adjustment.

Exit criterion: `MASTER_BUDGET_DAP_IDLE` p99 stays within Spike G's
3.74 ms ceiling on `doom_tick`; throttle accuracy on the five-bench set
is within ±0.4 % of Pi target.

### Stage 2 — DAP server, breakpoints / step / stack / locals

6. Implement a minimal DAP server in `lib/libconsolelua_dap.c`. Wire
   protocol over a TCP socket on `127.0.0.1:5678` (configurable). Handle
   exactly the requests VS Code's built-in DAP client emits during a
   normal breakpoint session:
   - `initialize`, `launch`, `configurationDone`
   - `setBreakpoints` (verified-line response)
   - `threads`, `stackTrace`, `scopes`, `variables`
   - `continue`, `next`, `stepIn`, `stepOut`, `pause`
   - `disconnect`, `terminate`
   - Custom `hot_reload` request (used in Stage 4)

   The server runs on a host pthread spawned at
   `fc_consolelua_dap_listen(port)` time (called from Spike I's runtime
   driver before `fc_cart_init` when `--dev --debug` is set on the
   rv32emu host CLI). State transitions submitted by the server are
   applied by `dap_dispatch` on its next master-hook fire; the hot path
   never blocks on the DAP socket.

7. Author `cases/case_c_dbg/main.lua` — case c plus a helper:
   ```lua
   local frame = 0
   local function step_label(n) return "tick=" .. n end   -- step-in target
   function init()   frame = 0 end
   function update() frame = frame + 1 end
   function draw()
       local label = step_label(frame - 1)                -- line 6
       console_print("frame " .. (frame - 1) .. " " .. label .. "\n")
   end
   ```
   Build this as a Spike I case c variant. Verify Spike I's case c output
   shape is preserved (modulo the extra `tick=N` suffix).

8. Manual VS Code test (recorded in the result doc):
   - Open `cases/case_c_dbg/main.lua` in VS Code.
   - Set a breakpoint at `step_label`'s first line.
   - F5 with the launch config from Stage 6.
   - Confirm: breakpoint hits on first frame; `step_label` appears as
     the top frame in the call stack; `n` is visible in the Variables
     pane with value `0` (then `1`, `2`, ... on subsequent continues).
   - Step Over → returns to `draw` line 7.
   - Step Out → returns to `update` (next tick boundary).

   Programmatic regression: a Node-based DAP client harness in
   `host/dap_test.js` that runs the above sequence headless and asserts
   on the message/response stream. This is the gate; manual VS Code is
   the supporting evidence.

Exit criterion: the DAP test sequence completes end-to-end with stock
VS Code (manual) and the Node harness (programmatic); breakpoint hit,
step-over, call-stack, locals, and upvalues all succeed against case c.

### Stage 3 — GDB stub with libraries-svr4 reporting

9. Implement the GDB remote serial stub in `host/gdb_stub.c` inside
   rv32emu. TCP socket on `127.0.0.1:1234` (configurable). Handle the
   minimum protocol surface VS Code's "Native Debug" extension drives
   during a breakpoint session:
   - `qSupported` (advertise `qXfer:libraries-svr4:read+`,
     `qXfer:exec-file:read+`, `swbreak+`)
   - `qXfer:exec-file:read` (return cart binary path)
   - `qXfer:libraries-svr4:read` (synthesised library list — see below)
   - `g`/`G` (registers), `m`/`M` (memory), `vCont` (run, step, range-step)
   - `Z0`/`z0` (software breakpoint set/clear via instruction patching
     in the cart's loaded image — same mechanism rv32emu uses for its
     existing trace/break support, exposed through the stub)
   - `?` (last-stop reason), `T05` stop-reply
   - Custom `qFc32:reload` packet (used in Stage 5)

   The synthesised library list is built from `fc32_dynload`'s in-memory
   map. For each loaded ELF, emit a `<library>` entry with the load
   address (`l_addr`) and the host filesystem path of the original `.so`
   (`name`). GDB re-opens the file locally to read DWARF — the runtime
   never sends DWARF over the wire.

   ```xml
   <library-list-svr4 version="1.0">
     <library name="/build/libconsole.so"    lm="0x..." l_addr="0x08000000" l_ld="0x..."/>
     <library name="/build/libconsolelua.so" lm="0x..." l_addr="0x080..."   l_ld="0x..."/>
   </library-list-svr4>
   ```

   The cart binary itself is reported via `qXfer:exec-file:read`; GDB
   loads its DWARF directly without a `<library>` entry.

10. Build Spike I case b with `-g -gdwarf-4` (DWARF v4 — the version
    `gdb-multiarch` 14.x parses cleanly for `riscv:rv32`). Confirm
    `readelf --debug-dump=decodedline cart_b` shows source line tables
    for `cart_b.c` and `mylib.c`.

11. Manual GDB session (recorded in the result doc):
    ```
    riscv32-linux-gnu-gdb cart_b
    (gdb) target remote :1234
    (gdb) info sharedlibrary           # both .so visible at the right addrs
    (gdb) break mylib_value
    (gdb) continue
    Breakpoint hit at mylib.c:N
    (gdb) bt
    #0 mylib_value at mylib.c:N
    #1 fc_cart_draw at cart_b.c:M
    #2 fc_console_main at libconsole_rv32.c:K
    #3 _start at crt0.S:7
    (gdb) print &fc_console_print      # libconsole symbol
    $1 = (void (*)(const char *)) 0x0800XXXX <fc_console_print>
    ```
    The `bt` is the load-bearing assertion — every frame must resolve to
    a source line, including the cross-binary unwind from `mylib_value`
    (cart `.text.mylib`) through `fc_cart_draw` (cart `.text`) into
    `fc_console_main` (libconsole's `.text`, pre-mapped at the
    runtime-chosen address). If the unwind stops at the PLT, the
    `qXfer:libraries-svr4:read` reply is wrong (likely off-by-load-base)
    and GDB cannot map PCs to libconsole's DWARF.

12. Programmatic regression: `host/gdb_test.py` drives the same sequence
    via the GDB Python API or `gdb.execute("...")` from a `-batch -ex`
    invocation. Asserts on `info sharedlibrary` shape, the breakpoint
    line, and the `bt` frame count and source-mapping pattern.

13. Memory read sanity: `(gdb) x/4x &_cart_lua_bytecode` is **not** part
    of case b (case b has no `_cart_lua_bytecode`). Use case d for that
    check — the cart's `.cart.resources` bytes at the right guest
    addresses must match `xxd cart_d.luac` byte-for-byte. Add this as a
    case d test under the same gdb_test harness.

Exit criterion: `info sharedlibrary` reports both libraries at the
runtime-chosen addresses; the `bt` from `mylib_value` walks the PLT
into `fc_console_print` with correct source mapping; case d's
`.cart.resources` bytes read back identically to the original luac
output.

### Stage 4 — DAP reload-while-debugging (synthetic)

14. Implement the synthetic reload path in `lib/libconsolelua_reload.c`:
    ```c
    void fc_consolelua_synthetic_reload(const uint8_t *new_bytecode,
                                        uint32_t new_size,
                                        const char *source_path) {
        lua_close(L);
        L = luaL_newstate();
        configure_sandbox(L);              /* ADR-0079 */
        fc_consolelua_master_hook_install(/* current mask */, /* N */);
        luaL_loadbuffer(L, (const char*)new_bytecode, new_size, "@cart");
        lua_pcall(L, 0, 0, 0);
        dap_emit_loaded_source(source_path, "changed");
    }
    ```
    The custom `hot_reload` DAP request invokes this. The function
    *does not* preserve game state — that's Spike N (was Spike M
    before the renumber).

15. Test sequence (programmatic, in `host/dap_reload_test.js`):
    a. Build `case_c_dbg/main.lua` with breakpoint at line 47 (the
       `step_label` body); load it as the initial bytecode.
    b. Connect a stock VS Code (or the Node DAP harness emulating VS
       Code's flow). Set the breakpoint. Run; confirm hit at line 47.
    c. Build a second variant: same source, but five `-- comment` lines
       inserted above `step_label`. The breakpoint marker in the editor
       tracks to line 52 (VS Code's standard line-shift behaviour).
       Save the editor state.
    d. The harness sends `hot_reload` with the second variant's
       bytecode and the source path.
    e. Runtime calls `synthetic_reload`; emits
       `loadedSource(reason: "changed")`.
    f. Without any user interaction, VS Code (or the harness) re-sends
       `setBreakpoints` with `lines: [52]`. Server responds
       `verified: true`.
    g. Continue running. Breakpoint hits at line 52. Capture the
       sequence; assert no spurious hit at the original line 47 (which
       is now a comment line).

16. Deleted-line variant: the second variant deletes the `step_label`
    body entirely (so line 47 / 52 is not executable in the new cart).
    Repeat steps c–f. Server response must be `verified: false`. VS
    Code (or harness) shows a hollow gutter marker. Confirm: no
    spurious hit on the next continue.

17. Edge case to record (not gated on): an edit that adds a *new*
    function above `step_label` with code at the breakpoint's old line
    number. VS Code's line-shift tracking should still re-bind to the
    correct (now-shifted) `step_label` body, *not* to the new function.
    This is a property of the editor's marker tracking, not the
    runtime — but the result doc captures the observed behaviour to
    validate the assumption.

Exit criterion: shifted-line case re-binds and hits the correct new
line with no manual UI action; deleted-line case shows hollow gutter
with no spurious hit; both behaviours observed under stock VS Code with
no custom client extension.

### Stage 5 — GDB reload-while-debugging (cart ELF replacement)

18. Implement the GDB-side synthetic reload as a `qFc32:reload` custom
    packet in `gdb_stub.c`. On receipt: ask `fc32_dynload` to swap the
    cart's PT_LOAD region with a freshly-built cart ELF; the cart's
    DWARF and line tables may differ. Emit a stop-reply that VS Code's
    GDB extension treats as a process re-event:
    - `T05library:;` — triggers re-fetch of `qXfer:libraries-svr4:read`
      and `qXfer:exec-file:read`. GDB re-applies its breakpoints
      against the new line tables automatically.

19. Test sequence (`host/gdb_reload_test.py`):
    a. Build case b variant 1 with breakpoint at `mylib_value` body.
       Run under GDB; confirm hit.
    b. Build case b variant 2: same `mylib.c`, but with five blank
       lines inserted above `mylib_value`. New DWARF, shifted line
       table.
    c. Send `qFc32:reload` with variant 2's path.
    d. GDB receives the `T05library:;` stop. It re-fetches
       libraries-svr4, re-fetches exec-file, re-resolves all
       breakpoints. The harness asserts `info breakpoints` shows the
       breakpoint at the new line number.
    e. Continue. Breakpoint hits. No spurious hit at the old line
       number (which is now a blank line in variant 2).

20. Deleted-line variant: variant 2 removes `mylib_value` entirely.
    `info breakpoints` should show the breakpoint as `pending`
    (unbound) after the reload. Continue: no spurious hit. The
    cart now fails at link time if `mylib_value` is referenced
    elsewhere, so use a separate test cart (`case_b_optional`) where
    `mylib_value`'s presence is conditional and the call site is
    `__attribute__((weak))`.

Exit criterion: GDB extension automatically re-applies breakpoints
after `qFc32:reload` (no `monitor reload-symbols` or similar manual
step); shifted-line case binds to the correct new PC; deleted-symbol
case shows pending breakpoint with no spurious hit.

### Stage 6 — VS Code launch configurations

21. `cases/case_c_dbg/.vscode/launch.json` — Lua cart DAP launch:
    ```json
    {
      "version": "0.2.0",
      "configurations": [{
        "type": "fc32-lua",
        "request": "launch",
        "name": "Spike J — Lua cart (case c)",
        "program": "${workspaceFolder}/cart_c",
        "stopOnEntry": false,
        "debugServer": 5678
      }]
    }
    ```
    The `debugServer` field tells VS Code's built-in DAP client to
    connect to the runtime's already-listening server on port 5678
    instead of spawning a debug adapter. The runtime must be started
    separately (`make run-dap`) before pressing F5. The result doc
    captures the launch flow and confirms F5 → breakpoint hit →
    Variables pane populated.

22. `cases/case_b/.vscode/launch.json` — C cart GDB launch using the
    Native Debug extension:
    ```json
    {
      "version": "0.2.0",
      "configurations": [{
        "type": "gdb",
        "request": "attach",
        "name": "Spike J — C cart (case b)",
        "executable": "${workspaceFolder}/cart_b",
        "target": "localhost:1234",
        "remote": true,
        "cwd": "${workspaceFolder}",
        "valuesFormatting": "parseText",
        "gdbpath": "riscv32-linux-gnu-gdb"
      }]
    }
    ```
    Same operational pattern: `make run-gdb` to start rv32emu listening
    on 1234, then F5 attaches. Validation: F5 → breakpoint at
    `mylib_value` → Variables pane shows `n` and the cross-binary `bt`
    surfaces `fc_console_main`.

Exit criterion: F5 hits a breakpoint and shows the right frame in the
Variables panel for both launch configurations on a clean VS Code
profile (no extensions installed beyond stock + Native Debug).

---

## Risk notes

- **Master-hook dispatcher overhead at small N.** Spike G's chosen N
  (TBD from Spike G's results) sets the baseline. The dispatcher adds
  one branch per fire (`if (cfg.budget_enabled && ar->event == ...)`).
  At Spike G's N the budget hook fires roughly once per microsecond on
  `doom_tick`; the additional branch should be ≤ 5 ns on V8/rv32emu —
  inside the noise floor — but if Stage 1 step 4 shows > 5 % regression
  vs Spike G's standalone build the dispatcher needs to be hand-rolled
  against the specific config (e.g. compile-time branches with
  `__builtin_expect`) rather than runtime-checked.

- **`LUA_MASKLINE` × DAP composition.** When DAP is attached and the
  user is stepping, the mask is `MASKLINE | MASKCOUNT | MASKCALL |
  MASKRET` — every Lua VM line costs one master-hook fire. Spike G.3
  measured `MASKLINE`-only overhead; adding the DAP arm to the
  dispatcher and the throttle arm in parallel may push past Spike G.3's
  ±0.4 % ceiling. Stage 1 step 5 explicitly tests this. If the
  composition fails the accuracy gate, the production model for dev
  mode is "throttle off when DAP is stepping; throttle resumes on
  continue" — record this as a design implication, not a spike failure.

- **GDB `qXfer:libraries-svr4:read` synthetic format gotchas.** The
  schema specifies `lm`, `l_addr`, `l_ld` fields; `l_addr` is the load
  base, `l_ld` points to the dynamic section's runtime address. GDB
  uses `l_addr` to compute symbol PCs but `l_ld` to walk the
  link-map — getting `l_ld` wrong silently breaks subsequent library
  list updates. Spike J emits `l_ld = l_addr + e_phoff_of_dynamic`;
  verify with `(gdb) info sharedlibrary` showing the right "From"/"To"
  columns. If wrong, the GDB unwind across PLT silently gives the
  wrong source location.

- **DWARF v4 vs v5 across `riscv32-linux-musl-gcc` and `gdb-multiarch`.**
  Recent gcc defaults to DWARF v5, which `gdb-multiarch` 14.x parses
  for `riscv:rv32` but with occasional line-table glitches. Spike J
  forces `-gdwarf-4` on cart and library builds. If the bt in Stage 3
  step 11 shows the wrong source line, switch to `-gdwarf-3` (older
  but more reliably parsed) and document.

- **`loadedSource(reason: "changed")` event ordering vs `lua_close`.**
  The synthetic-reload path closes the old `lua_State` *before* loading
  the new bytecode, then emits `loadedSource`. A naive implementation
  could emit `loadedSource` after `lua_close` but before `lua_pcall`
  finishes loading the new module — VS Code re-sends `setBreakpoints`
  in response, the server tries to resolve them against a half-loaded
  state, and `verified` flags lie. Sequence the emit *after* the new
  state is fully prepared and the master hook is re-installed.

- **`Z0` software breakpoint patching across reload.** When the cart
  ELF is replaced, any `Z0` packets the stub previously honoured were
  patched into the *old* PT_LOAD bytes. After reload those patches are
  gone (the new ELF was loaded fresh). GDB's stock behaviour after a
  process replacement is to re-send `Z0` for every breakpoint, so the
  patches are reapplied automatically. Stage 5 step 19d's test
  asserts this — if GDB does *not* re-send `Z0` (some configurations
  cache breakpoints client-side), the stub needs to track and re-apply
  them itself.

- **Stock VS Code's line-shift tracking is the dependency, not the
  runtime's.** Stage 4's "no manual UI action" criterion depends on VS
  Code's editor tracking the breakpoint marker through the user's edits
  *before* the reload. If the user closes the file between edit and
  reload, VS Code drops the marker and the runtime's `loadedSource`
  re-prompt re-binds against the original line number — which the
  spike treats as user error, not a runtime bug. Document this.

- **DAP transport choice (TCP vs stdin/stdout).** This spike uses TCP
  on localhost. Production may prefer stdio piping per ADR-0044's
  packer-runtime IPC. The protocol surface is identical either way;
  the spike's harness can be re-pointed without code changes. Out of
  scope to decide here.

- **Concurrent DAP and GDB attached to the same runtime.** Possible in
  principle (Lua DAP debugs Lua-side; GDB debugs the rv32emu CPU
  state). Possibly useful for debugging cart C bindings called from
  Lua. Spike J does *not* exercise this composition — only one
  debugger attached at a time. Whether the two stop-the-world
  protocols can interleave (DAP wants to pause the Lua VM; GDB wants
  to stop the CPU; either can leave the other in an inconsistent
  state) is a follow-up question.

---

## Deliverables

- `spikes/spike-j/Dockerfile` — extends `fc32-spike-i`, adds host-side
  GDB (`gdb-multiarch`), Node modules for the DAP test harness, Python
  for the GDB test harness.
- `spikes/spike-j/Makefile` — orchestration:
  - `make master-overhead` — Stage 1 overhead measurement (three
    libconsolelua variants).
  - `make master-throttle-accuracy` — Stage 1 throttle regression on
    five benches.
  - `make dap-test` — Stage 2 programmatic DAP regression.
  - `make gdb-test` — Stage 3 programmatic GDB regression.
  - `make dap-reload-test` — Stage 4 DAP reload regression.
  - `make gdb-reload-test` — Stage 5 GDB reload regression.
  - `make run-dap` / `make run-gdb` — start rv32emu with the relevant
    listener for manual VS Code F5 sessions.
- `spikes/spike-j/lib/master_hook.c` — the master hook dispatcher,
  added to `libconsolelua.so`.
- `spikes/spike-j/lib/libconsolelua_dap.c` — the DAP server (host
  pthread, TCP socket, request handlers, command queue).
- `spikes/spike-j/lib/libconsolelua_reload.c` — the synthetic reload
  path; the `hot_reload` custom DAP request handler.
- `spikes/spike-j/host/gdb_stub.c` — the GDB remote serial stub inside
  rv32emu; the `qFc32:reload` custom packet handler.
- `spikes/spike-j/host/dap_test.js` — Node DAP client harness (Stages
  2, 4).
- `spikes/spike-j/host/gdb_test.py` — Python GDB harness (Stages 3, 5).
- `spikes/spike-j/cases/case_c_dbg/` — case c variant with the
  step-into helper and the line-shift / deleted-line edits.
- `spikes/spike-j/cases/case_b/.vscode/launch.json` and
  `cases/case_c_dbg/.vscode/launch.json` — VS Code launch configs for
  the F5 criterion.
- `spikes/spike-j/baselines/` — the master-hook overhead measurements
  (rv32emu doom_tick under MASTER_OFF / MASTER_BUDGET /
  MASTER_BUDGET_DAP_IDLE), the throttle accuracy table, the DAP and
  GDB regression transcripts.
- `spikes/spike-j/TASKS.md` — per-stage checklist, kept current as
  work proceeds.
- `docs/design/spike-j-results.md` — the write-up: overhead numbers
  (with the rv32emu→Chrome conversion documented), throttle accuracy,
  the DAP/GDB session transcripts, the reload-while-debugging
  observations (shifted-line, deleted-line, edge cases), the F5-end-
  to-end evidence, the three risk-area outcomes (hook composition
  workable as designed Y/N; GDB libraries-svr4 handles the pre-mapped
  layout Y/N; stock DAP-with-hot-reload pattern works without custom
  client extension Y/N), open items for production, and any
  composition/sequencing implications surfaced in stage 1 step 5 or
  the dispatcher overhead measurement.
