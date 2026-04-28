# ADR-0045: Hot reload via the save/restore mechanism

## Status
Accepted

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
- Type-changed fields: require author migration hook or dropped with warning.
- Complex restructuring: optional `on_migrate(old, new)` callback.

**Signal protocol:** packer watch mode signals the runtime via the existing
DAP passthrough connection (`hot_reload` custom command). The runtime
intercepts this command before forwarding — it is the one DAP message the
runtime acts on directly rather than piping to the VM. Alternative channels
(Unix socket, filesystem marker) available for non-DAP setups.

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
