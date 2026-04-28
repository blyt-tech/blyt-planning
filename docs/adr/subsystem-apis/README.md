# Subsystem APIs

The specific API surface for each subsystem — both the C API and its Lua
counterpart. Covers input, graphics, audio, text, localisation, resources,
RNG, tilemaps, and supporting utilities.

### Input

| # | Decision |
|---|----------|
| [0018](0018-single-pointer-input-abstraction.md) | Single-pointer input abstraction |
| [0019](0019-cosmetic-device-info-not-capability.md) | Expose cosmetic device info to carts; withhold capability info |
| [0043](0043-carts-declare-inputs-used.md) | Carts declare which inputs they use |
| [0070](0070-text-input-blocking-model.md) | Text input — blocking model with two modes |

### Graphics

| # | Decision |
|---|----------|
| [0048](0048-no-clip-rect-or-camera-offset.md) | No clip rectangle or camera offset in the runtime |
| [0049](0049-transparency-via-palette-index-255.md) | Transparency via palette index 255 convention |
| [0050](0050-fill-pattern-primitives.md) | Fill pattern — 4×4 bitmask for dithering and hatching |
| [0052](0052-batch-drawing-api-variants.md) | Batch drawing API variants |
| [0060](0060-mutable-tilemaps.md) | Mutable tilemaps — manifest-declared, diff-based save, per-instance flags |
| [0069](0069-text-rendering-params-struct.md) | Text rendering via params struct |
| [0072](0072-bmfont-internal-font-format.md) | BMFont as internal font format; TTF/OTF input-only |

### Audio

| # | Decision |
|---|----------|
| [0053](0053-audio-single-music-channel-stem-muting.md) | Audio — single music channel with stem muting |
| [0054](0054-audio-voice-groups-vs-tags.md) | Audio — voice groups vs voice tags |
| [0055](0055-three-layer-volume-model.md) | Three-layer volume model |
| [0056](0056-speech-api-rhubarb-lip-sync.md) | Speech API and Rhubarb lip sync pipeline |

### Resources

| # | Decision |
|---|----------|
| [0027](0027-explicit-resource-release.md) | Explicit resource release API |
| [0028](0028-persistent-resources-in-manifest.md) | Persistent resources declared in cart manifest |
| [0029](0029-memory-introspection-api.md) | Memory introspection API |

### Utilities

| # | Decision |
|---|----------|
| [0041](0041-multiple-named-seedable-rng-streams.md) | Multiple named seedable RNG streams |
| [0062](0062-localisation.md) | Localisation — packer-driven, compile-time integer key constants |
| [0063](0063-locale-stored-as-preference.md) | Locale stored as a preference, not game state |
| [0071](0071-easing-library.md) | Easing library — no runtime tween manager |
