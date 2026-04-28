# State and Save

How game state is declared, stored, accessed, and serialised — the POD
typed buffer model, manifest-declared schemas, packer-generated field
constants, active slot management, and the four save mechanisms (in-game
save, save state, rewind, preferences). This section underpins hot reload,
netplay, replay, and rewind, all of which depend on the save/restore
infrastructure.

| # | Decision |
|---|----------|
| [0009](0009-state-buffers-manifest-declared.md) | State buffers are manifest-declared with packer-generated constants |
| [0010](0010-pod-typed-state-buffers.md) | Persistent state in POD typed buffers |
| [0011](0011-lua-soa-metatable-sugar.md) | Lua state buffer ergonomics — SOA metatable sugar, no row proxies in v1 |
| [0012](0012-lua-coroutines-save-restore-hooks.md) | Lua coroutines — raw available; save/restore requires blessed API |
| [0013](0013-four-save-mechanisms.md) | Four distinct save mechanisms |
| [0057](0057-state-buffer-field-constants.md) | State buffer field access via fc_field_h compile-time integer constants |
| [0058](0058-active-slot-management.md) | Active slot management built into the buffer API |
