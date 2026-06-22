# ADR-0125: Save file format — versioned chunked binary

## Status
Accepted

## Context

ADR-0013 establishes four save mechanisms. The cart-managed in-game save
(mechanism 1) serialises cart state buffers plus runtime-owned state (RNG,
audio tracks, channel volumes and fades, palette cycling, timers) into a
persistent file. Both the runtime and the cart code evolve over time; save
files written by older versions must remain loadable by newer ones.

Two orthogonal version axes exist:

- **Runtime save format version** — the envelope and section layout, owned
  by the runtime. Incremented when the runtime changes what it writes.
- **Cart save version** — declared by the cart author. Used to drive
  cart-side migration logic in `on_load_state` (ADR-0087).

These are independent: a cart update can bump its save version without
touching the runtime format, and a runtime update can add a new section
without requiring cart authors to do anything.

## Decision

### Envelope format

Save files use a **chunked binary format** with a fixed header followed by
tagged sections. Unknown sections are skipped; missing sections are
default-initialised. This gives forward and backward compatibility across
independent evolution of the runtime and cart schemas.

```
Header (32 bytes, fixed):
  magic             u32    0x424C5953  ("BLYS")
  format_version    u16    envelope format; incremented if header layout changes
  flags             u16    bit 0: section payloads zstd-compressed (ADR-0026)
                           bits 1–15: reserved, must be zero
  runtime_version   u32    major<<16 | minor — runtime that wrote this file
  cart_version      u32    cart-declared save_version; 0 if not declared
  cart_schema_hash  u64    hash of manifest state_buffers schema at pack time
  section_count     u32
  reserved          u32    must be zero

Sections (repeated section_count times, any order):
  tag               u32    four printable ASCII bytes
  size              u32    byte length of payload
  payload           [size bytes]
```

A reader that encounters an unknown tag skips `size` bytes and continues.
A reader that does not find an expected section default-initialises that
subsystem's state.

### Section tags

| Tag    | Content |
|--------|---------|
| `CART` | Cart state buffers — field-matched by name when schema changed |
| `RTNG` | Runtime RNG state |
| `RAUD` | Runtime audio state (tracks, channel volumes, fade curves) |
| `RPAL` | Palette and palette-cycling state |
| `RTIM` | Timer and alarm state |
| `RDSC` | Save description buffer contents (ADR-0087) |

Each section's payload begins with a 1-byte **internal version** number.
This allows a subsystem's serialised layout to evolve independently of the
envelope `format_version` — the `RAUD` section can add a field for a new
fade curve type without bumping the envelope version.

### Cart save version

Cart authors declare a monotonically increasing integer in `cart.config.yaml`
(see the placement amendment below):

```yaml
# cart.config.yaml
save_version: 3        # monotonic integer; increment when migration is needed
```

The human-readable `version: "1.2.0"` (not interpreted by the runtime) stays
in `cart.info.yaml`, where a frontend displays it.

The packer compiles `save_version` into `.cart.config`. The runtime stores it
verbatim in the save header and passes it to `on_load_state` via
`blyt_load_info_t.saved_cart_version` (ADR-0087). Cart code uses it to run
version-specific migration:

```c
void blyt_cart_on_load_state(blyt_load_info_t info) {
    if (info.saved_cart_version < 3) {
        // save predates quest system — set starting chapter
        blyt_buffer_set_u8(S_QUESTS, 0, S_QUEST_CHAPTER, 1);
    }
}
```

`save_version` is optional. Carts that do not declare it receive
`saved_cart_version == 0` in `blyt_load_info_t`.

### Schema change detection and the `CART` section

`cart_schema_hash` is computed by the packer from the manifest's
`state_buffers` declarations. When loading a save:

- **Hash matches current cart**: the `CART` section payload is copied
  directly into state buffers — no field matching needed.
- **Hash differs**: the runtime performs field-by-field name matching using
  the schema embedded in the cart's `.cart.config` ELF section:
  - Fields present in both save and current schema: copied to current
    position.
  - Fields in current schema absent from save: zero-initialised.
  - Fields in save absent from current schema: dropped.

`blyt_load_info_t.buffers` (ADR-0087) reflects the outcome of this walk —
`was_restored` and `fields_restored` indicate which buffers and fields
received values from the save versus were zero-initialised.

### `CART` section layout

```
For each declared state buffer (in manifest declaration order):
  name_hash    u32    hash of the buffer name — used for matching
  payload_size u32    byte length of this buffer's data
  payload      [payload_size bytes]
```

When the schema hash matches, payloads are copied directly. When it differs,
each buffer's payload is matched by `name_hash` against the current schema,
and field offsets are recomputed from the `.cart.config` ELF section.

## Consequences

- Old saves remain loadable across cart updates that add, remove, or
  reorder fields. Cart authors increment `save_version` and handle
  migration in `on_load_state` as needed.
- Runtime subsystems evolve independently via per-section internal
  versioning. A new audio feature adds a field to the `RAUD` section
  without touching any other section.
- The schema hash provides a fast path (direct copy) for the common case
  where the schema hasn't changed.
- zstd compression is opt-in per the flags field; the runtime can compress
  saves where storage size matters (e.g. flash-limited hardware targets).
- Save files are self-describing enough for a standalone tool to inspect
  them: the header identifies the runtime and cart versions, and section
  tags identify subsystem payloads.

## Amendment — `save_version` is declared in `cart.config.yaml`

The original decision placed `save_version` in `cart.info.yaml` / `.cart.info`,
co-located with the human-readable `version`. On review this was corrected to
**`cart.config.yaml` / `.cart.config`**, per ADR-0073's consumer/timing
placement rule. Rationale:

- **The frontend never reads it.** libretro treats save data (SRAM via
  `retro_get_memory_data`/`_size`; save states via `retro_serialize`/
  `retro_unserialize`) as opaque byte blobs — the only validation surface is
  the core's boolean return; the frontend does persistence and screenshot
  thumbnails, never content introspection. blyt's saves are not even libretro
  SRAM/save-states: they are internal `.blys` files the cart reads via the
  `SAVE_READ` ECALL, with the libretro core returning `NULL`/`0` from
  `retro_get_memory_data`. No frontend, present or future, consumes
  `save_version`.
- **The runtime is the sole consumer, at save-write time**, where it stamps the
  running cart's `save_version` into the `BLYS` header. The value reported to
  `on_load_state` comes from that save header (the version that *wrote* the
  save), not from the loading cart's manifest — so the manifest field is never
  read on the load path either.
- The human-readable `version` (frontend-displayed via `blyt_cart_version()`)
  remains in `cart.info.yaml`. `version` and `save_version` look like siblings
  but have different consumers, so they live in different manifests.
