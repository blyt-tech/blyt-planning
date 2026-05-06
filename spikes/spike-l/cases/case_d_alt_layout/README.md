# case_d_alt_layout — layout-hash-mismatch negative test

The Stage 4 step 18 negative test from PLAN.md needs a second cart variant
whose `cart_state_t` shape differs from case d's, so its `layout_hash`
(spike-K's safety gate against silent layout drift) differs from the
first variant's. A buffer saved by the first variant must be rejected
when loaded into the second.

## How the variant differs

Spike L is wired against the synthetic facade rather than embedding
rv32emu, so the "alt layout" is realised by building the libretro core
with `-DBLYT_FACADE_ALT_LAYOUT` defined. The compile-time switch:

- Adds an extra `uint32_t alt_extra` field to `cart_state_t` in
  `lib/blyt_facade.c`. Total `cart_state_t` size grows by 4 bytes.
- Updates the layout-description string used for the `layout_hash`
  computation so the spike-K-style FNV-1a-64 produces a different hash.

Build the alt core with:
```
cc ... -DBLYT_FACADE_ALT_LAYOUT ... -o runtime_libretro_alt.so
```

Then `host/layout_mismatch.py` saves a buffer in the primary core and
attempts to load it through the alt core. The expected outcome is
`retro_unserialize → false` (header `layout_hash` ≠ runtime `layout_hash`)
with no crash — the safety gate from spike-K survives the libretro
adapter wrapping.

## Future work (when rv32emu is embedded)

When the facade switches from the synthetic mode to a real rv32emu-backed
runtime loading the case d cart ELF, the alt-layout cart will be a
separate cart_d_alt_layout/ ELF — same Lua, +1 field in the C-side
`cart_state_t`. The layout_hash gate is computed at cart-load time from
the cart's runtime-side region descriptions, so the same negative-test
machinery applies; only the artefact origin changes.
