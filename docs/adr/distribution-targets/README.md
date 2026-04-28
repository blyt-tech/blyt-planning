# Distribution Targets

How the runtime reaches players — both the frontend implementations
(SDL2, libretro shim, Emscripten, hardware PID-1) and the distribution
channels (libretro ecosystem, standalone hardware, browser, mobile).
Also covers the game-service integration model at the distribution layer.

| # | Decision |
|---|----------|
| [0016](0016-game-service-integration-at-distribution-layer.md) | Game-service integration at the distribution wrapper layer |
| [0033](0033-runtime-as-library-multiple-frontends.md) | Runtime as a library with multiple thin frontends |
| [0034](0034-custom-libretro-frontends-for-standalone-hardware.md) | Custom libretro frontends for standalone and hardware distribution |
| [0035](0035-minimal-linux-on-hardware.md) | On hardware — minimal Linux (buildroot-based), runtime as PID-1-equivalent |
| [0036](0036-frontend-pulls-model.md) | Frontend-pulls model — frontend owns the fixed-timestep accumulator |
