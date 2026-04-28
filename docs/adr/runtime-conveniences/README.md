# Runtime-Provided Conveniences

Things the runtime provides for free — no cart code required. Covers default
assets available from the first frame, and runtime-managed features with their
own manifest declarations and UI (speedrun tooling, palette cycling, screen
shake, pause menu, credits).

| # | Decision |
|---|----------|
| [0042](0042-default-assets-in-runtime-binary.md) | Default assets bundled in the runtime binary |
| [0015](0015-speedrun-tooling.md) | Speedrun tooling built on deterministic replay |
| [0051](0051-screen-shake-tracked-in-save-state.md) | Screen shake tracked in save state |
| [0061](0061-palette-cycling.md) | Palette cycling — manifest-declared, auto-advancing |
| [0064](0064-pause-menu-credits-quit.md) | Pause menu, credits, and quit — runtime-provided with manifest declaration |
| [0014](0014-manifest-declared-achievements.md) | Achievements — deferred to a future version |
| [0075](0075-gameplay-capture.md) | Gameplay capture — GIF and screenshot |
