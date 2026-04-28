# Architecture Decision Records

Decisions extracted from the high-level design document and the API design
document. Each ADR records a single decision with its context, rationale,
and consequences.

## Sections

| Section | Description |
|---------|-------------|
| [console-definition](console-definition/README.md) | ISA, hardware, display/audio specs, numeric model, determinism, sandboxing, size classes, netplay |
| [distribution-targets](distribution-targets/README.md) | Frontend implementations, distribution channels, game-service integration |
| [cart-format](cart-format/README.md) | ELF container, manifests, resource addressing, compression, asset pipeline |
| [api-conventions](api-conventions/README.md) | C and Lua API shape, packer-generated constants, performance strategy |
| [state-and-save](state-and-save/README.md) | POD buffers, save mechanisms, preferences, field constants, slot management |
| [subsystem-apis](subsystem-apis/README.md) | Input, graphics, audio, resources, RNG, localisation, text, utilities |
| [runtime-conveniences](runtime-conveniences/README.md) | Default assets, speedrun tooling, screen shake, palette cycling, pause menu and credits |
| [dev-experience](dev-experience/README.md) | CLI packer, VS Code integration, hot reload, dev instrumentation |

