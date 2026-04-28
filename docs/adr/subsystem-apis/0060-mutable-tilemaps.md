# ADR-0060: Mutable tilemaps — manifest-declared, diff-based save, per-instance flags

## Status
Accepted

## Context

Tilemaps loaded from cart resources are read-only by default (they are
decompressed from the resource and used as-is). Some games require modifiable
tilemaps: digging games, destructible environments, puzzle games where tiles
are moved. The save/restore mechanism must handle mutable tilemap state
efficiently without saving the entire tilemap on every frame.

## Decision

**Mutable tilemaps are declared in the manifest; their save representation is
a diff against the resource baseline.**

```yaml
# cart.config.yaml
mutable_tilemaps: [world_map, dungeon_level_1]
```

For each mutable tilemap, the runtime maintains two structures:
1. The base tile data (loaded from the cart resource, read-only copy).
2. A diff table: a sparse map of `(x, y) → new_tile_id` for cells that have
   been modified.

`fc_tilemap_set_tile()` writes to the diff table. `fc_tilemap_get_tile()`
checks the diff table first, then falls back to the base data. Save/restore
serializes only the diff table (compact for typical cases where few tiles
change). A full reset clears the diff table, restoring the base resource.

**Per-instance tile flags** are separate from per-type tile flags:

- **Per-type flags** (`FC_TILE_SOLID`, `FC_TILE_WATER`, etc.) are declared
  in the cart resource and describe the tile type's properties.
- **Per-instance flags** are stored per-tile in the mutable diff table
  (e.g., a flag marking "this specific wall tile has been broken").

Per-instance flags exist only for declared-mutable tilemaps and are
serialized with the diff table.

```c
fc_result_t fc_tilemap_set_tile(fc_tilemap_h tm, int32_t x, int32_t y,
                                 uint16_t tile_id, uint16_t instance_flags);
fc_result_t fc_tilemap_get_tile(fc_tilemap_h tm, int32_t x, int32_t y,
                                 uint16_t *out_tile_id,
                                 uint16_t *out_instance_flags);
fc_result_t fc_tilemap_reset(fc_tilemap_h tm);  // restore to resource baseline
```

## Consequences

- Read-only tilemaps (the common case) have zero overhead: no diff table.
- Mutable tilemaps declared in the manifest can be identified by the packer
  for memory budget purposes.
- Diff-based save is efficient: a dungeon where 50 tiles have been destroyed
  serializes 50 entries, not the entire map.
- Per-instance flags enable per-cell state (broken, visited, locked) without
  requiring a separate state buffer per tilemap.
- Mutable tilemaps that approach full-map modification (procedurally generated
  worlds) may have large diffs; authors should be aware of the memory cost.
