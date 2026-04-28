# Cart Format and Asset Pipeline

The cart container format, manifest structure, resource addressing, compression,
and the asset pipeline — including the external tools the packer accepts as
input (Aseprite, Tiled, LDtk, BMFont, Rhubarb) and how they map to the
runtime's internal formats.

| # | Decision |
|---|----------|
| [0024](0024-unified-risc-v-elf-cart-format.md) | Unified RISC-V ELF format for all carts |
| [0025](0025-lua-carts-as-elf-with-lua-host-api.md) | Lua carts are ELF carts using the runtime's Lua-host API |
| [0026](0026-per-resource-zstd-compression.md) | Per-resource zstd compression with packer-driven selection |
| [0040](0040-resource-names-in-source-ids-at-runtime.md) | Resource addressing — names in source, compile-time constants, IDs at runtime |
| [0073](0073-three-manifest-files.md) | Three manifest files — cart.info, cart.config, cart.build |
