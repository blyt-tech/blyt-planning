# Pending: console name

The console has no chosen name. This is a deliberate deferral until
foundational design is stable. The following items use placeholders
throughout the design documents and must be revisited once a name exists.

---

## C API prefix

**Placeholder:** `fc_` (functions/types) and `FC_` (constants).  
**Appears in:** `fc_cart.h`, `fc_runtime.h`, all C API symbols
(`fc_image_h`, `fc_voice_play`, `FC_OK`, `FC_BLIT_FLIP_H`, etc.).  
**Action:** Replace `fc_`/`FC_` with a prefix derived from the console name.

## C header filenames

**Placeholder:** `fc_cart.h`, `fc_runtime.h`.  
**Action:** Rename to match the chosen API prefix.

## Lua API namespace

**Placeholder:** `console.*` (e.g., `console.gfx.blit`, `console.input.pressed`).  
**Action:** Replace `console` with the console name or an agreed short form.

## Packer CLI binary name

**Placeholder:** `console` (e.g., `console pack`, `console new`, `console run`,
`console watch`).  
**Action:** Rename binary to the console name or an agreed short form.

## Cart file extension

**Placeholder:** `.cart`.  
**Appears in:** ADR-0024, distribution docs, packer output.  
**Action:** Choose a name-derived extension (e.g., `.fc32`, `.mycart`).

## Manifest source filenames

**Placeholder:** `cart.info.yaml`, `cart.config.yaml`, `cart.build.yaml`.  
**Appears in:** ADR-0073.  
**Note:** The `cart.*` prefix could remain as a stable convention regardless
of console name, or could be replaced with a name-derived prefix. Decision
can wait until other naming is settled.

## ELF OSABI value

**Placeholder:** unassigned.  
**Appears in:** ADR-0024 (`e_ident[EI_OSABI]`).  
**Action:** Choose a value in the OS/proc-specific range (64–255) and
document it as the official OSABI for this console. If the project registers
with the RISC-V community or publishes a spec, the value can be formalised
there.

## FlatBuffers preamble type tags

**Placeholder:** `"CINF"` (cart info), `"CCFG"` (cart config).  
**Appears in:** ADR-0073.  
**Note:** `CINF`/`CCFG` are derived from "cart info/config" and are
reasonable permanent identifiers regardless of console name. Revisit only
if a name-derived tag convention is preferred.

## Runtime and library names

**Placeholder:** `libconsolelua` (Lua host shim library).  
**Action:** Rename to match the console name.

## SDK package names

**Placeholder:** none assigned yet.  
**Scope:** Cargo crate name, any npm package (browser runtime), VS Code
extension publisher ID and extension name.  
**Action:** Register package names once the console name is chosen.

## Default asset constant names

**Placeholder:** `FC_FONT_BUILTIN`, `FC_PALETTE_VGA`, `FC_PALETTE_DEFAULT`,
etc. (defined in `fc_cart.h`).  
**Action:** Rename with the chosen API prefix; the suffix conventions
(`_FONT_BUILTIN`, `_PALETTE_VGA`) can remain stable.
