# Stage API

Design decisions for Stage, the optional cart-side game logic layer. Stage
provides structured patterns for entity lifecycle, event-driven communication,
scene management, sequential scripting, camera handling, and input translation
— all designed to work correctly with fc32's save-state and hot-reload
constraints.

| # | Decision |
|---|----------|
| [0089](0089-stage-game-logic-layer.md) | Stage — a structured game logic layer for cart development |
| [0090](0090-named-handler-functions.md) | Named handler functions with compile-time IDs |
| [0091](0091-event-publish-subscribe.md) | Event publish/subscribe system |
| [0092](0092-scene-lifecycle-and-stack.md) | Scene lifecycle and stack |
| [0093](0093-system-execution-order.md) | System execution order |
| [0094](0094-sequence-scripting.md) | Sequence scripting with automatic serialization |
| [0095](0095-camera-state-and-transform.md) | Camera state and world-to-screen transform |
| [0096](0096-entity-slot-generation-counters.md) | Entity slot generation counters |
| [0097](0097-timer-and-alarm-system.md) | Timer and alarm system |
| [0098](0098-fsm-helper.md) | Lightweight FSM helper |
| [0099](0099-prefab-spawn-templates.md) | Prefab and entity spawn templates |
| [0100](0100-input-intent-translation.md) | Input intent translation |
