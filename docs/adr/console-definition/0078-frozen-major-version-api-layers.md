# ADR-0078: Frozen major-version API layers — backwards-compatible runtime subsystem

## Status
Accepted

## Context

ADR-0032 establishes that a major version bump signals a breaking change and
that the runtime refuses carts requiring a higher major version than it
implements. This ADR addresses the complementary question: what happens to
carts from *older* major versions as the runtime evolves?

Without a deliberate policy, two failure modes arise:

- **Silent breakage**: the v2 runtime changes an API subtly; v1 carts load
  but misbehave.
- **Hard rejection**: the v2 runtime refuses v1 carts entirely; players with
  old carts need to find a v1 runtime.

Neither is acceptable for a platform. Carts should be durable artefacts: a
cart that worked the day it was published should work on the latest runtime.

## Decision

**Each major API version is implemented as a frozen, self-contained layer
in the runtime. When a new major version ships, the previous version's layer
is frozen — its API surface, semantics, and behaviour are locked. The runtime
ships all supported major-version layers simultaneously and selects the
appropriate layer per cart based on its declared `api_version`.**

### Per-cart API layer selection

The cart's `api_version` (from `.cart.info`, read before the runtime
initialises) determines which API layer handles it for the duration of
that session:

- **ECALL dispatch (native and Lua shim carts)**: the runtime maintains
  separate ECALL dispatch tables per major version. A v1 cart's ECALLs
  route to v1 handlers; a v2 cart's ECALLs route to v2 handlers. ECALL
  numbers, signatures, and semantics within a major version are frozen.
- **Lua API module (`console.*`)**: the runtime loads the `console` module
  for the cart's declared major version. V1 carts receive the frozen v1
  `console` module; v2 carts receive the v2 module. Both can coexist in
  the runtime binary.

The selection is transparent: cart code calls the API it was written
against and receives the behaviour it was written for, regardless of which
major version of the runtime it is running on.

### Guarantee: latest runtime plays all carts of all supported major versions

The latest runtime release must be able to play any cart published for any
supported major version. Players do not manage multiple runtime installs or
match runtime versions to carts.

Within a major version, this follows from ADR-0032's minor-version
backward-compatibility guarantee: a runtime implementing v1.9 can run any
cart declaring v1.0 through v1.9.

Across major versions, this follows from frozen layers: a v2 runtime
includes the complete frozen v1 layer and can run v1 carts with identical
behaviour to the v1 runtime at the time of freezing.

### Frozen layer maintenance

A frozen major-version layer is not abandoned — it receives:

- **Security fixes**: vulnerabilities in a frozen layer are patched.
- **Critical bug fixes**: crashes or data-loss bugs are fixed even in
  frozen layers.

A frozen layer does not receive:

- New API surface (that would change the v1 spec retroactively).
- Behaviour changes that alter how existing v1 carts run.
- Performance improvements that change observable timing.

### Support horizon

The number of simultaneously supported major versions is a practical
decision deferred to when v2 exists. The principle is: at minimum, the
current and previous major versions are always supported. Very old major
versions may be deprecated with sufficient advance notice (a release cycle)
and clear migration guidance. V1 is likely to remain supported indefinitely
given the platform's early stage.

### What constitutes a major-version break

For reference alongside this ADR, changes that require a major bump:

- Removing or renaming API functions.
- Changing the signature or semantics of existing API calls.
- Changing ECALL numbers or argument layouts.
- Changing the behaviour of existing manifest fields.
- Removing resource types or changing how existing resource types are
  interpreted.

Changes that do not require a major bump (minor version):

- Adding new API functions.
- Adding new manifest fields (ignored by older runtimes).
- Adding new resource types (unused by older carts).
- Performance improvements with no observable behaviour change.

## Consequences

- Published carts are durable: a cart that works today works on future
  runtimes without modification.
- Authors do not need to republish or recompile carts when the runtime
  updates.
- Players do not manage multiple runtime versions; one runtime install
  plays everything.
- The runtime binary grows with each new major version, as frozen layers
  accumulate. This is a deliberate trade-off — binary size grows slowly
  (major versions are infrequent) and the alternative (broken old carts)
  is worse.
- The v1 Lua `console.*` module and ECALL table become a stable,
  documented contract the moment v2 ships, not before. During v1
  development, the API can still evolve freely.
