# Spike W — in-VM cart module swap for seamless reload-while-debugging

**Status:** design captured, spike not yet run (unpushed working doc).
**Date:** 2026-06-23.
**Origin:** design session while grilling issue #90 (VS Code project-dir dev
mode + hot reload). The grill surfaced that the issue's native-dev plan
conflicts with the as-built runtime; the discussion then moved past #90 into how
to get the *most seamless* reload-while-debugging experience, which is what this
doc captures. #90 is **shelved** pending this spike; update #90 after.

Related: ADR-0044 (CLI packer + VS Code dev loop), ADR-0045 (hot reload via
save/restore — see its "dedicated dev control channel" amendment), ADR-0104
(VS Code dev shell). Depends-on history: #84/#86/#87/#88 (all merged).

---

## 1. Terminology (pinned to stop the recurring confusion)

"native" was being used for three different things. Going forward:

- **player** — the locally-built desktop runtime (`blytplay`/`blytdebug`,
  `frontends/player/`) with an embedded rv32emu interpreter. This is what the
  #90 `mode: 'native'` actually means. Test helpers `run_cart_native*` are the
  **player** leg.
- **metal** — the bare-metal RISC-V launcher/runtime (`frontends/native/`,
  seccomp, `ld-blyt.so`, the QEMU "native gate", `blyt_native[_riscv64]`).
  **Metal debugging is out of scope** for all of this work.
- **wasm** — the Emscripten browser runtime.

The **guest dynamic loader** likewise differs by target: in the **emulated**
path (player + wasm) the cart is loaded by the host runtime over rv32emu in
`runtime/host/src/libblyt/cart_run.c`. `ld-blyt.so` is the **metal**-only ELF
interpreter. β (below) is a `cart_run.c` change, *not* an `ld-blyt.so` change.

Proposed follow-up (separate mechanical PR, not this spike): rename
`run_cart_native*`→`run_cart_player*`, `native_qemu`→`metal_qemu`,
`blyt_native*`→`blyt_metal*`; add a "player vs metal vs wasm" note to CLAUDE.md.

---

## 2. The #90 conflict that started this

#90's plan (§3) wants player-dev F5 to launch `blyt debug` + `blytdebug`, open a
WASM panel *alongside* a native window, and have **both reload** on edit, with
the native window reloading "via the dev control TCP connection." The as-built
runtime can't deliver that from extension code:

- **Wiring gap.** devtool's dev-ctrl is a broadcast **hub that listens**
  (`run.rs` `start_dev_ctrl_relay`, PORT+6) and the watcher broadcasts `reload`
  to clients that connect *in* (`run.rs:1233-1240`, only on a *successful*
  rebuild whose ELF bytes changed). The player's `--dev-ctrl-port` **also
  listens** (`frontends/player/main.c:268`, `dev_ctrl_start`). Both ends listen
  → nothing bridges devtool→player today. ADR-0045 says "`blyt debug` connects
  to this port to drive reloads," but #88 implemented the watcher as a hub
  broadcaster and never wired the connect-to-player half.
- **Reload destroys the debugger.** `blyt_libretro_reload()`
  (`frontends/libretro/blyt_libretro.c:410`) does `blyt_session_destroy` +
  recreate. The GDB/DAP server is created **per session**, right after
  `blyt_session_create` in `retro_load_game`
  (`blyt_libretro.c:186-223`: `blyt_session_gdb_listen`/`dap_listen`), and the
  reload path does **not** re-establish it. So the old listener and the live
  lldb-dap/VS Code connection are torn down and the new session has no debug
  server. ADR-0045 explicitly defers "DAP/GDB reattach across a native reload."

So full native-dev reload-while-debugging is not reachable from the extension
alone.

---

## 3. Decisions locked for #90 (apply when we resume)

- **Scope:** extension-only + one small player change (see wiring). WASM-dev is
  the headline; player-dev reload-while-debugging is deferred to the seamless
  work this spike feeds.
- **WASM-dev (default mode):** project-dir launch + watcher + WASM-panel live
  reload + DAP auto-reconnect (all runtime-supported today).
- **player-dev:** devtool + player, **no browser panel** (a dev session is
  *either* wasm *or* player, never both at once — the §3 "both views" idea is
  dropped). Player debug is **direct-connect** (works today). Player **reload
  wired for run-mode** (Ctrl+F5 / blytplay); **reload suppressed during an
  active debug session** until the seamless path lands.
- **Reload wiring = option 2 (player dials the devtool hub).** The devtool is
  **unchanged** (its hub already broadcasts to all TCP clients); the only change
  is a small **outbound dev-ctrl client mode in `frontends/player/main.c`**
  reusing `dev_ctrl_dispatch`. Chosen over "devtool connects to player" (which
  has a build/serve chicken-and-egg: the player needs `build/.dbg.elf` before it
  can announce a port, and the devtool is what builds it) and over
  "extension bridges" (the deferred Node socket-relay). Note: this is the
  opposite direction from the wasm runtime (which dials the devtool), but it
  matches the player already exposing DAP/GDB as listening ports.
- **Naming:** new extension modes are **`player` / `player-cart`**, not
  `native`/`native-cart` (no production users → free); internal
  `_blytMode:'native'` → `_blytMode:'gdb'` (it's the lldb/GDB adapter axis, not
  a runtime).
- Generated `launch.json` / `blyt setup vscode` omit the `cart` field and rely
  on `detectAnyCart()` (issue §6/§7). `buildCart()` removed from server-starting
  modes (the devtool builds internally). `mode: 'cart'`/`'native-cart'` (→
  `'player-cart'`) preserve the old explicit-build behaviour under new names.
- The dev ELF (`build/.dbg.elf`) is produced by `blyt build code --debug <dir>`
  (no server) as well as by `blyt debug <dir>`.

---

## 4. The seamless-debug exploration

### 4a. Why a persistent relay matters
The wasm relay survives reload not by holding a connection but because its
**listening ports are process-level** (`run.rs:336-374` accepts a fresh ws+tcp
pair each session). On reload both sides reconnect to those stable ports and the
relay re-pairs (the GDB-restart / §5 auto-reconnect flow). The player today
hosts its debug listener **bound to the session**, so reload destroys the port
and the debugger has nowhere to reconnect — that's the whole gap. Routing player
debug through a persistent devtool relay would give the same stable
reconnection point and let the extension reuse the wasm reconnect logic.

### 4b. The native-vs-Lua breakpoint split (key result)
- **Native code (C/Rust, GDB RSP):** breakpoints are **address-based** —
  VS Code sends `{source,line}`, **lldb-dap** resolves line→address against
  DWARF and sends `Z addr`. A code-changing recompile moves addresses, so any
  scheme that replays old addresses (relay-level, or player-retained) re-arms
  breakpoints at the wrong lines. Correct re-resolution can only happen in the
  debugger client, which requires it to re-read the rebuilt ELF.
- **Lua (`blytdebug --debug`, DAP):** breakpoints are **source-line**, armed by
  the player's DAP server via the master hook — no addresses. So the player can
  retain its `{source:line}` set across a Lua-state recreate, re-arm it, and
  **keep the DAP socket open** → fully transparent reload-while-debugging, no
  VS Code session churn. (Wrinkle: a reload arriving while halted at a
  breakpoint queues until the next `continue`, since dev-ctrl is serviced
  between frames.)

So: **Lua transparent reload is cleanly achievable; native code needs the
debugger to re-resolve.**

### 4c. The breakthrough for native — model reload as a solib event
The GDB stub **already** reports the cart as an SVR4 shared library via
`qXfer:libraries-svr4:read` (`gdb_stub.c:151,186-224`: `<library name=PATH
l_addr=…>`), and lldb reads DWARF from that named file. That is the same
machinery that makes breakpoints survive `dlclose`/`dlopen`. So a hot reload can
be expressed as a **shared-library reload event**: keep the stub + connection
alive, recompute the cart's library entry, and emit a library-change stop
(GDB RSP `T05library:;`). lldb then re-fetches the library list, sees the cart
at its new `l_addr`, **re-reads the rebuilt DWARF, and re-resolves/re-inserts
breakpoints natively** — no DAP restart, no proxy state-mirroring, correct even
when code moves. Encouragingly, the per-session GDB state in `cart_run.c`
(`struct blyt_session`: `gdb_libs`/`gdb_libs_ffi`/`gdb_nlibs`/`gdb_exec_path`,
`gdb_bps`/`gdb_nbp`, indirected via global `g_gdb_session`) already persists
across a module swap; only the cart library entry needs recomputing.

This converges Lua and native on "the runtime re-establishes debug state, the
client re-resolves," rather than "tear down and rebuild the VS Code session."

### 4d. Two reload models
- **α — persist the stub above session-recreate.** Keep ADR-0045's
  destroy+recreate-session reload, but lift the stub/connection above it; after
  recreate, recompute the layout and fire the library event. Closer to today,
  but the stub must rebind to a brand-new VM and we still cross the rv32emu
  **session-recreate global-state hazard** (we've already hit a block-chain
  cache UAF on session re-create — fixed in fork blyt-patches@4c3ab7c; rv32emu
  has single-VM-per-process globals).
- **β — in-VM cart module swap (preferred to spike).** The VM, libblyt32 /
  libblyt32lua, and the stub all **persist**; only the **cart `.so`** is
  unloaded and the rebuilt one loaded through the guest loader in `cart_run.c`,
  then `init()` re-runs and state restores. This is a `dlclose`/`dlopen` of the
  cart inside one VM. It **sidesteps the recreate-UAF entirely** (the VM never
  dies) and maps perfectly onto the solib model lldb expects. Cost: the guest
  loader must support unload+reload of the cart module in place (carts load once
  at startup today), with memory-hygiene / determinism care for the old module's
  mappings and any libblyt32 state pointing at it.

---

## 5. β spike plan (this spike)

Goal: determine whether β is viable. Probe the two genuine unknowns; defer the
rest. **Run G3 first** (cheap, de-risks the external lldb dependency before
investing in loader work).

- **G3 — does lldb re-resolve on a solib reload?** With the stub persisted and
  reporting the cart library reloaded (new `l_addr`, same path, rebuilt DWARF) +
  a `T05library:;` stop, does lldb **re-read the rebuilt ELF and re-resolve** a
  source-line breakpoint to its new address — or cache the module by name and
  serve stale symbols? Largely testable on its own via
  `tests/integration/tests/{gdb,lldb_dap}`. If it caches, β's native seamlessness
  needs a workaround (e.g. vary the reported library path per reload). **Probe
  this first.**
- **G1 — in-VM cart module swap (core feasibility / bulk of the work).** Can
  `cart_run.c`'s loader, without tearing down the VM / libblyt32 /
  libblyt32lua, unmap the current cart's segments, load the rebuilt cart, redo
  relocations, and re-bind libblyt32's callback pointers (`init`/`update`/`draw`/
  `on_save_state`/`on_load_state`) to the new cart's exports — leaving the cart
  runnable on new code with state restored? Main risks: stale references into
  old cart memory, allocator hygiene.
- **G2 — state restore across the swap** (snapshot→swap→reinit→restore matches
  today's HOT_RELOAD semantics). Should largely reuse existing snapshot/restore;
  validate, don't rebuild.
- **G4 — determinism / memory hygiene** (bit-identical vs a fresh load).
  **Post-viability gate** — out of scope for this spike, but the spike must not
  design something that precludes it.

Spike runs in its own worktree/branch (e.g. `spike-cart-module-swap`); the
`90-vscode-dev-mode-debug` worktree stays untouched and #90 stays open-but-
shelved.

---

## 5a. Crux finding (during G3 setup): cart is the *main executable*, not a library

The cart is loaded at **guest bias 0** and presented to lldb as the **main
executable**, not a shared library:
- `cart_run.c:2158-2169` — `cart_lib.bias = 0`; `resolve_elf_plt(cart->map, …,
  0, …)`. The cart occupies low guest memory at bias 0; runtime libs load at the
  128 MiB region.
- The `library-list-svr4` (`gdb_stub.c:186-224`, populated at
  `cart_run.c:2108-2132`) lists only the cart's `DT_NEEDED` `.so` deps
  (libblyt32.so, …). The cart is reported via `qXfer:exec-file:read`
  (`gdb_exec_path`) with `main-lm="0x0"`. No `qOffsets` in the stub — lldb
  assumes the PIE main exe at bias 0, which it is.

**Implication for β (the crux):** Unix never reloads a main executable (you
`exec` a new process), so **lldb has no native mechanism to re-read a changed
main exe** on a stop event. A synthetic `T05library` re-resolves breakpoints in
the runtime `.so`s, **not** in the cart's own code — which is what users debug.

Therefore seamless cart-code re-resolution via the solib trick requires β to
**load the cart as a genuine shared library** (a `dlopen`'d module over a stub/
empty main exe), not as the bias-0 main exe. This aligns with β being literally
a `dlclose`/`dlopen` of the cart, but it is a real change to how the cart is
loaded and presented (`cart_run.c`), and it is the central thing the G3 probe
must validate: *does lldb re-read the cart's DWARF and re-resolve a **cart-code**
breakpoint when the cart is presented as a library and a solib-reload event
fires?* If no → the seamless cart path is blocked and cart debugging falls back
to a main-exe reattach (terminate/relaunch) regardless of β.

Sub-question: how lldb should learn the cart's (new) load bias across a reload
if the cart-as-library is re-mapped at a different base.

## 5b. G3 result (measured): lldb caches solib DWARF — relocate-only, no re-read

Built two carts whose `blyt_lldb_test_fn` sits at different addresses (a fatter
padding function in v2), set a source breakpoint against v1 (resolved A1), then
overwrote the cart file with v2 and fired a synthetic `T05library` via the new
dev-control `solib_swap` (re-mapping the cart library to base `0x40000000`).
Measured the re-queried breakpoint address A2:

- `v1_func=0x1326`, `v2_func=0x14be`, line-6 offset `0x10`, remap base `0x40000000`.
- **A2 = `0x40001336` = base + A1 (cached v1 + relocate).**
- Re-read v2 would have given `base + v2_func + off = 0x400014ce`. It did not.

**Conclusion:** lldb *does* process the library event (it re-fetched the list and
applied the new base — A2 picked up `0x40000000`), but it **reuses the cached
module's DWARF and does not re-read a same-path file** whose contents changed. It
keys the module by path (and likely UUID/build-id). So the
synthetic-`T05library`-with-same-path approach **relocates but does not
re-resolve changed code** — it does not deliver seamless native reload on its own.

**Implication for β:** the seamless native path needs a way to force lldb to load
the rebuilt cart as a *new* module. Candidate workaround: report the cart library
at a **unique path per reload** (or change its build-id) so lldb treats it as a
fresh module and reads the new DWARF. Whether that re-resolves breakpoints
cleanly (and how VS Code/lldb-dap reacts to a module identity change) is the next
probe (G3b). If no in-place mechanism works, native cart reload falls back to a
main-exe reattach (terminate/relaunch) regardless of β.

## 5c. G3b result (measured): unique path RE-RESOLVES, but old module is NOT unloaded

Re-ran reporting the cart library at v2's *own distinct path* (`.../v2/build/
v2.dbg.blyt`) + base `0x40000000`.  The gdb RSP trace (blytdebug stderr) is the
ground truth — the DAP `setBreakpoints` re-query is misleading (it returns the
stale old location as the primary `instructionReference`):

```
recv Z0,1336,2                       # initial bp in v1 @ 0x1336
send T05library:;thread:01;          # synthetic reload event
recv qXfer:libraries-svr4:read       # lldb RE-FETCHED the library list
   <library name=".../v2/build/v2.dbg.blyt" l_addr="0x40000000"/>   # new path; old absent
recv Z0,400014ce,2                   # lldb set a NEW bp at 0x400014ce
```

`0x400014ce = base + v2_func + offset` = the **re-read** address.  So **a unique
path makes lldb re-read v2's DWARF and re-resolve the breakpoint correctly** —
seamless native re-resolution is achievable. ✅

**But the old module is not unloaded.** lldb added `Z0,400014ce` *without*
removing `Z0,1336`, even though the old path was absent from the re-fetched list.
The breakpoint now has **two** locations (stale `0x1336` + correct `0x400014ce`);
across many reloads this accumulates stale modules/mappings.  **Absence from the
svr4 list does not trigger unload** — an explicit unload is required, almost
certainly the real `r_debug` rendezvous with `RT_DELETE`/`RT_CONSISTENT` states
rather than synthetic list editing.

**Net G3 outcome:** the cheap synthetic `T05library` + svr4-list approach gets
re-resolution right (with a unique path) but cannot cleanly unload the old
module.  A production-clean seamless native reload therefore needs proper
dynamic-loader rendezvous integration in the guest loader (`cart_run.c` maintains
an r_debug link-map lldb reads; reload drives RT_DELETE then RT_ADD) — a
meaningful but bounded piece (G3c).  The fallback if that proves too costly is a
main-exe reattach (terminate/relaunch) for native carts.  (Lua transparent reload
is unaffected — it does not go through lldb/DWARF at all; §4b.)

(Probe artifacts on branch `spike-cart-module-swap`: `tests/integration/tests/
spike_g3.rs`, the `solib-reload` scenario in `tests/dap/run_lldb_dap_test.mjs`,
and the runtime hooks `fc_gdb_stub_notify_library_change` /
`blyt_session_gdb_simulate_solib_swap` / cart-as-library registration.)

## 5d. G3c finding: the stale copy is the cart-as-*program*, set by lldb-dap, not the stub

Tried presenting the cart as library-only by emptying the stub's
`qXfer:exec-file` (`BLYT_SPIKE_LIB_ONLY`).  No effect: the full attach trace shows
lldb resolving the cart breakpoint at attach (`Z0,1336` at f=0) **before** it ever
fetches `qXfer:libraries-svr4` (f=20).  The main-executable copy of the cart does
**not** come from the stub's exec-file — it comes from **lldb-dap's
`program: <cartPath>`**, which lldb reads directly off disk.  So the stale,
never-unloadable `0x1336` location is the cart-as-*program*.

**Consequence — the real β architecture for seamless native debug:** the cart must
**not be the lldb-dap `program`**.  Point `program` at a stub/placeholder ELF and
load the cart **purely as a shared library** (via the svr4 list, the way the
runtime `.so`s already appear).  Then the cart is never the executable, so it is
cleanly unloadable/reloadable, and the unique-path (or `r_debug`) reload swaps it
without leaving a stale main-exe copy.  This is a debug-session-structure change
in the VS Code extension / lldb-dap launch config (and in how the runtime presents
modules), not just a stub tweak — and it is the crux deliverable for a
production-clean seamless native reload.

## 5e. G3c VALIDATED: stub program + cart-as-library + unique-path → clean reload

Pointed lldb-dap's `program` at a stub riscv32 ELF (`libblytcommon.so`) instead of
the cart, with the cart loaded purely as a library (and the stub exec-file empty).
Re-ran the reload measurement.  Result — the entire session contains exactly **one**
breakpoint packet:

```
recv Z0,400014ce,2     # the ONLY Z0 in the whole session
```

No `Z0,1336` anywhere — the stale cart-as-program copy is gone.  After the
unique-path reload the breakpoint binds to `0x400014ce` (the correct re-read
address in the reloaded cart-library), a single clean location.  **Seamless
native re-resolution with clean unload is validated** with this architecture:

1. lldb-dap `program` = a **stub ELF** (never the cart).
2. The cart is loaded **purely as a shared library** (svr4 list).
3. Reload reports the cart-library at a **unique path + new base**; lldb re-reads
   the new DWARF and rebinds the breakpoint — one location, no stale module.

**Caveat (solvable):** lldb fetched the library list only at the reload event
(f=21), so before that the breakpoint was *pending* (no address bound).  In
production the cart-library must be **announced at attach** (present in the list /
an initial library event) so breakpoints bind before the cart's `init()` runs —
otherwise early breakpoints are missed on first launch.

### Spike W — consolidated outcome (β VALIDATED)
- Seamless native **re-resolution works and is clean** (§5e): with lldb-dap
  `program` = a stub ELF and the cart loaded **as a library**, a unique-path
  reload makes lldb re-read the new DWARF and rebind the breakpoint to the correct
  new address — a single location, no stale modules.
- The blocker in §5b–§5d was that the cart was the **program** (`program=cartPath`,
  read off disk by lldb-dap), a permanent main-exe module that can never be
  unloaded.  Fixed by the stub program + cart-as-library.
- Swap mechanism: **unique-path** is sufficient (proven); a full `r_debug`
  rendezvous (same-path DELETE→ADD) is a larger alternative we did **not** need.
- **β seamless-native is therefore a bounded, multi-part change:**
  1. lldb-dap launch `program` → a stub ELF (extension / launch-config change).
  2. Present the cart **as a library** in the svr4 list, announced **at attach**
     so breakpoints bind before `init()` (runtime: `cart_run.c`).
  3. On reload, re-report the cart-library at a **unique (checksum) path + new
     base** and fire a library event (runtime + dev-control + extension reconnect).
  4. **Hybrid carts (§5f):** add a **post-reload gate** — hold the recreated
     `init()` until *both* the Lua DAP re-arm and the native lldb rebind complete
     (a reload-time `dap_wait_ready`/`gdb_wait_attached` equivalent), since the
     native side blinks while the Lua side stays live.
- Lua transparent reload remains independent and cheaper (§4b) — no lldb/DWARF.

## 5f. Hybrid carts under the new scheme — required post-reload gate

Hybrid carts (one ELF: native code + `.cart.lua`) debug with **two** views: a
native GDB/lldb session and a companion Lua DAP session.  The established
ordering invariant (`frontends/player/main.c`) is a **startup gate**: the cart's
`init()` does not run until **both** breakpoint sets are armed —
`blyt_libretro_dap_wait_ready()` (Lua DAP `configurationDone`) **then**
`blyt_libretro_gdb_wait_attached()` (native) **then**
`gdb_continue_initial_halt()`.  Ownership: the native session owns the process;
the Lua DAP session is a companion holding no process reference.

Under the new seamless scheme the two views reload by **different, asymmetric**
mechanisms:
- **Native:** stub `program` + cart-as-library + unique-path re-report + library
  event → lldb re-reads the new DWARF and rebinds.  This makes the native session
  **blink** (terminate→relaunch / library-event reconnect).
- **Lua:** the player's DAP server re-arms source-line breakpoints on the new Lua
  state and **keeps its socket** (transparent — §4b).

A single rebuilt cart (one new checksum-path ELF) serves both views, so path
coupling is fine.  **But the startup "both-armed-before-`init()`" gate currently
exists only at startup — the new scheme requires a RELOAD equivalent.**

**Requirement (β, hybrid):** after a hot reload, the recreated cart's `init()`
must be held until **both** debug views have re-synced — the Lua DAP re-arm
**and** the native lldb rebind/reconnect — preserving the Lua-first-then-native
order.  Because the native side blinks while the Lua side stays live, the
reconnect is asymmetric; without a reload gate, `init()` could run with only one
side armed and silently drop `init()`-time breakpoints on the not-yet-resynced
view.  Concretely β must add, alongside the runtime reload path, a
`dap_wait_ready`/`gdb_wait_attached` equivalent that fires on **reload** (not just
first attach), coordinated by the process-owning native session.

## 6. Open questions
1. G3: lldb re-read-on-solib-reload behaviour (the pivotal unknown).
2. β trigger: synthetic `T05library:;` from the stub (simpler) vs the SVR4
   `r_debug` rendezvous breakpoint (proper, but needs loader cooperation).
3. G1: how much new capability the `cart_run.c` loader needs to dlclose/dlopen
   the cart, and how it interacts with determinism.
4. Lua path: confirm the player's DAP server can hold the socket + re-arm
   `{source:line}` breakpoints across a Lua-state recreate (the transparent
   case) — may ride on β or be a separate, cheaper change.

---

## 7. Code pointers
- `runtime/host/src/libblyt/cart_run.c` — guest loader; per-session GDB state.
- `runtime/host/src/gdb/gdb_stub.c:151,186-224` — `qSupported`,
  `qXfer:libraries-svr4:read` (cart-as-solib reporting).
- `frontends/libretro/blyt_libretro.c:186-223,410-457` — per-session debug
  listen; `blyt_libretro_reload()` destroy+recreate.
- `frontends/player/main.c:165-278,426-...` — `--dev-ctrl-port`, dev-ctrl
  listener (`dev_ctrl_start`, `dev_ctrl_dispatch`).
- `devtool/src/run.rs:173-220,336-374,948-1063,1170-1240` — relays, relay loop,
  dev-ctrl hub broadcast, file watcher.
- `tools/vscode/extension.js` — modes, adapters, auto-reconnect surface.
