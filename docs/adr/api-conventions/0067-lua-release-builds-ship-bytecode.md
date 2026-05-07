# ADR-0067: Lua release builds ship bytecode

## Status
Accepted — amended by ADR-0109 (luac runs in the fc32 emulator; compilation
runs for both debug and release builds).

## Context

Lua source files are human-readable text. The Lua compiler (`luac`) compiles
source to bytecode that loads faster and is smaller before compression. Debug
builds need source for error messages and DAP debugging; release builds do not.

## Decision

**Release builds ship Lua bytecode (compiled by `luac`); debug builds ship
Lua source.**

The packer compiles all `.lua` cart files with `luac` when producing a
release build. Bytecode is embedded in the cart ELF in place of source text.

**Size:** bytecode is 30–50% smaller than source before zstd compression. The
reduction is meaningful because zstd compresses both formats well; the pre-
compression saving narrows to ~10–20% after compression, but still worthwhile.

**Lua version mismatch:** bytecode is version-specific. If the runtime's
Lua version and the `luac` version differ, the runtime rejects the bytecode
at cart load time with a hard error. The packer enforces that `luac` version
matches the declared `api_version` in the manifest; a version mismatch is a
packer error, not a runtime surprise.

**Bytecode version in `.cart.lua`.** Lua-specific configuration does not
belong in `cart.config` — since Lua is just another implementation language
(ADR-0025), its settings live in a dedicated `.cart.lua` ELF section read
by `liblua.rv32` during VM initialisation (see ADR-0073). The packer writes
`lua_bytecode_version` (e.g., `"5.4"`) into this section automatically,
derived from the `api_version`. This field is not authored — there is only one supported Lua version in v1
so it is not an authoring concern. When multiple Lua versions are supported,
a `cart.lua.yaml` file will be introduced for author-facing Lua settings and
backwards compatibility addressed at that time.

**Debug builds:** source is shipped as-is. Error messages include line numbers
and source context. The DAP debugger (ADR-0045) attaches to the Lua source
for step-debugging. The `debug` flag in `cart.info` is set to `true` by the
packer (see ADR-0073).

**luac invocation (amendment — ADR-0109):** standard `luac` produces bytecode
for the architecture it runs on; it cannot cross-compile for RV32IMFC. The
packer runs the fc32-native `luac` binary inside the fc32 emulator as a build
step. Compilation runs for both debug and release builds — debug carts ship
source text, but the compilation step still validates the source. The emulator
is therefore a build-time dependency for any cart with Lua source.

**Obfuscation:** bytecode is not an intentional obfuscation mechanism.
Decompilers exist. Authors who need source protection should use native carts.

## Consequences

- Release carts load slightly faster (no parse step; bytecode is loaded
  directly into the VM).
- Release carts are slightly smaller before compression.
- Bytecode/runtime version mismatch is a hard build-time error, not a
  silent runtime failure.
- Debug and release cart formats differ; the packer flag `--release` controls
  which is produced. The dev loop (ADR-0044) always uses debug builds for
  fast iteration.
