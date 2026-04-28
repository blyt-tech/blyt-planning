# ADR-0026: Per-resource zstd compression with packer-driven selection

## Status
Accepted

## Context

Cart size caps (ADR-0030) make on-disk compression valuable — a 16 MB cart
can hold 32–60 MB of effective content at typical ratios. The compression
strategy must be compatible with mmap access (ruling out whole-cart
compression), must not require fully decompressing the cart on load (ruling
out preloading everything), and must be transparent to authors.

LZ4 was considered. It is simpler and faster to decompress but achieves
2–3× ratios vs. zstd's 3–5×. For storage-constrained carts, ratio matters
more than marginal decode speed.

Whole-cart compression was ruled out because it breaks mmap (the cart index
needs to be readable without decompressing the whole file) and would cause
memory blow-up for large-class carts (a 64 MB Large cart with 200 MB expanded
content cannot fit whole-expanded in the 32 MB runtime budget).

## Decision

**Per-resource compression using zstd at medium compression level, with
packer-driven selection.**

The `.cart.resources` directory (metadata, indices, names) is always
uncompressed and mmap-friendly. Individual resource bodies have a per-resource
`none` or `zstd` compression flag.

**The packer decides per-resource** based on measured compressibility:
- Already-compressed content (tracker modules, ADPCM/QOA audio, PNG-sourced
  sprites) ships uncompressed.
- Text (Lua source, manifests), tilemaps, sprite sheets from uncompressed
  sources, and native code ship zstd-compressed.
- Threshold: compress if savings exceed 5% of resource size.

Authors do not configure compression; the packer evaluates and chooses.

**Runtime behavior:**
- Uncompressed resources: returned as direct pointers into the mmap'd cart
  bytes (zero-copy, instant).
- Compressed resources: decompressed into runtime-allocated memory on first
  access; cached for subsequent accesses.
- Decompressed cache counts against the 16 MB cart-visible working memory
  budget.

## Consequences

- Typical cart shrinks 30–50% on disk — meaningful distribution benefit,
  especially for browser load times.
- Pay-as-you-go: decompression cost only for resources actually used.
- Invisible to cart authors; packer handles all decisions.
- Distribution size (cart file) is decoupled from runtime working set
  (decompressed cache), giving authors more content budget without runtime
  memory pressure.
- zstd decoder is ~50 KB — negligible against a runtime binary already
  containing Lua VM, RISC-V interpreter, SDL, etc.
- Cache eviction (LRU under memory pressure) is deferred to v2; v1 keeps
  decompressed resources resident. Explicit release API (ADR-0027) lets
  authors reclaim memory proactively.
