# ADR-0045: Hot reload via the save/restore mechanism

## Status
Accepted; **amended 2026-05-06** based on Spike N findings —
diagnostic format, rejected-reload-preserves-state rule,
`blyt32.on_hot_reload_failed` hook surface, M↔N composition note,
and `on_retype` mandatory-for-retypes rule added.  See "Amendment —
Spike N findings" at the bottom of this document.

## Context

A fast edit–run iteration loop requires hot reload: apply code and asset
changes to a running cart without restarting it from the beginning. Options
include function-level hot reload (patch individual functions in the running
VM), module-level reload (reload changed modules), or full-reload with state
preservation (restart the cart but restore its game state).

Function-level hot reload struggles with orphaned closures and stale
references. Module-level reload has the same problems at a coarser granularity.
Full reload with state preservation avoids these entirely, provided the state
serialization is reliable.

## Decision

**Hot reload via the save/restore mechanism** (the same one used for save
games, save states, and rewind).

On an explicit reload signal (IDE save triggering packer rebuild, or manual
hotkey), the runtime:

1. Keeps the running cart live while the packer rebuilds the cart artifact
   in the background. The author keeps interacting; nothing freezes.
2. Once the packer reports success, calls `save_state()` to snapshot all
   tracked state regions. Snapshotting at this point — not when the signal
   arrived — captures the latest gameplay state and avoids a wasted
   snapshot when the build fails (a packer error simply leaves the running
   cart untouched).
3. Loads the new cart (fresh code, fresh assets, fresh runtime-owned state).
4. Calls `load_state(snapshot)` to restore the cart to its previous state,
   applying layout migration if state schemas changed.
5. Resumes execution on the next frame.

**Layout migration on reload** (when state buffer schemas change):
- Matching fields (same name, same type): copied over.
- New fields: default-initialized (zero or author-specified default).
- Removed fields: dropped.
- Type-changed fields: require author `on_retype` callback (see
  §"Migration policy — type change" below), or dropped with a warning.
- Complex restructuring: optional `on_migrate(old, new)` callback.

**Rejected-reload-preserves-state rule:** if any step of the migration
walk fails (save-flatten error, `on_retype` rejection, slot body lookup
failure), the runtime rejects the reload entirely.  All tracked regions
remain in their pre-reload state — no partial mutation.  The pre-reload
cart continues running.  Rejection is signalled to the harness via
`FAILED <reason>\n` on the reload socket.

**Migration policy — type change:** if a field's type changes between
save and load and no `on_retype` callback is registered, the field is
**dropped** (new value zero-initialised) with a printed warning.  Carts
that retype fields SHOULD register `on_retype` to avoid unexpected data
loss.  The production packer (ADR-0009) SHOULD emit a build-time
diagnostic when a retype edit is detected without a registered callback.

**Signal protocol:** packer watch mode signals the runtime via the existing
DAP passthrough connection (`hot_reload` custom command). The runtime
intercepts this command before forwarding — it is the one DAP message the
runtime acts on directly rather than piping to the VM. Alternative channels
(Unix socket, filesystem marker) available for non-DAP setups.

The packer signals RELOAD only after all output files in `build/` are
coherent (every write-then-rename complete).  The runtime's RELOAD listener
is single-threaded; if RELOAD arrives before the build is coherent the
runtime SHOULD reject it with `FAILED pending`.  The harness retries after
the `BUILD-DONE` signal arrives.  This ensures the runtime never snapshots
a partially-written `build/`.

**Lua reload — code changes:** ADR-0012's constrained-ctx-shape contract
(validated by Spike M) prevents closures from entering the `ctx` table at
save time via `BLYT_ERR_FLATTEN_UNSUPPORTED_TYPE`.  This makes the
"closure-in-state" failure mode structurally unreachable; carts cannot
persist a closure that would become stale after a reload.  ADR-0045 does
not need to enumerate this as a runtime failure mode — it is prevented at
the save boundary by the ADR-0012 mechanism.

**Cart-side hooks:** carts may register a Lua function at
`blyt32.on_hot_reload_failed`.  The runtime fires this hook once per failed
slot with `(slot: int, reason: string)`.  The hook is a notification point;
it cannot resume the reload, request a fresh `init`, or roll the snapshot
forward — those semantics are deferred to a later ADR.

**Failure is a first-class outcome:** for edits that invalidate a live
coroutine (renamed or deleted function), the runtime surfaces a clean
diagnostic rather than silently corrupting or crashing.  The fixed format
(for byte-for-byte cross-host comparability) is:

```
hot_reload: failed to migrate slot <N> (persistent script)
  body: cart.lua:<line> (old) -> cart.lua:??? (new)
  reason: function '<name>' not found in new code
  surfaced via: blyt32.on_hot_reload_failed(slot=<N>, reason=...)
```

For deleted functions, "not found in new code" is replaced by "was deleted
from new code".  This format is a joint ADR-0045 / ADR-0083 surface; see
ADR-0083's amendment for the relationship to crash diagnostics.

**DAP server continuity:** after the new cart loads and `load_state` restores
game state, the in-VM DAP server reinitialises and re-registers any
breakpoints that VS Code held before the reload. From the author's perspective
the debug session is uninterrupted.

**Dev mode:** `--dev` flag (or env var). Same binary; enables file watching,
DAP server, debug overlay, relaxed error reporting, and permissive memory
limits.

## Consequences

- Hot reload reuses already-built, already-tested machinery. No separate
  hot-reload system to implement and maintain.
- Every reload cycle exercises `save_state`/`load_state`. Bugs in the save
  contract (missing state regions, migration issues) surface during
  development, not when players lose save data.
- No orphaned closures or stale references — all code is reloaded fresh;
  state is restored via POD serialization.
- The mental model is clean and consistent: "game state survives reload;
  code, assets, and all Lua closures are rebuilt fresh."
- Authors learn the patterns required for correct save games (handler by
  name/ID, computed caches rebuilt in `init`) during daily development
  rather than at shipping time.
- Performance targets: snapshot + restore < 2 ms; Lua-only rebuild < 500 ms;
  asset-only rebuild < 100 ms; native code rebuild < 3 s.

## Amendment — Spike N findings (2026-05-06)

Spike N (`spikes/spike-n/`, results in `docs/design/spike-n-results.md`)
validated the hot-reload mechanism end-to-end: 6 native edits, 10 Lua
edits (including 5 coroutine/failure cases), 148 cross-host runs, all
PASS.  The spike answered all six load-bearing questions and produced the
amendments incorporated above.  Key quantitative findings:

**Migration policy coverage:** across 10 schema-relevant edits, 80% are
covered by the default policy (copy matching, zero-init added, drop
removed).  The remaining 20% are retype edits that require `on_retype`.
The long tail does not dominate; ADR-0045 stands as written for the
majority of edits.

**Latency:** all edits pass both the ADR-0045 gates (< 3 s native,
< 500 ms Lua) and the wider ADR-0044 gates.  Stub-packer measurements;
production asset transforms are out of scope for this spike.

**Spike M ↔ Spike N composition:** ADR-0012's constrained-ctx-shape
contract (Spike M) makes the "closure-in-state" failure mode unreachable
at the save boundary.  The two spikes compose cleanly: Spike M's guard
makes the case unnecessary to enumerate as a runtime failure mode here.

**Clean failure surface:** l6, l9, l10 produce byte-identical diagnostics
across arm64 and amd64.  No silent corruption, no segfault, no
half-restored state observed.

**Signal protocol:** the racy-edit negative test confirms the runtime
correctly rejects RELOAD signals received before all `build/` files are
coherent.

### Open follow-ups (non-blocking for amendment ratification)

- Asset-only hot reload ("strict subset" claim, not yet measured).
- WASM and QEMU native reload paths.
- DAP-side signal protocol (Spike J's seam; composes with N's
  state-preservation seam).
- Reload-while-debugging (N + J full composition).
- Lua-callback shape for `on_retype` (N's callbacks are C functions).
- Schema-migration DSL if retype frequency increases in production carts.
- Coroutine-mid-resume save (out of N's scope; all saves at frame boundaries).
