# ADR-0032: Major.minor API versioning

## Status
Accepted

## Context

The cart format will evolve over time. Carts need to declare which version of
the API they target; the runtime needs a policy for handling version mismatches.
Semantic versioning (major.minor) is the standard model for distinguishing
breaking from non-breaking changes.

## Decision

Carts declare a target API version (`api_version`) in `cart.info.yaml`,
which the packer compiles into the `.cart.info` ELF section (ADR-0073). The
frontend reads this before loading the runtime.

- **Major version bump:** breaking change. The runtime refuses to load carts
  requiring a higher major version than it implements. The player sees a clear
  error ("this cart requires a newer runtime version").
- **Minor version bump:** backward-compatible feature addition. A runtime
  implementing API 1.5 can run a cart declaring API 1.3 — it just doesn't use
  the 1.4–1.5 features.
- **Patch:** reserved for bug fixes in the runtime with no cart-visible
  behavior change.

## Consequences

- Cart authors know exactly which runtime version is required to run their
  cart.
- The runtime can grow new API surface (new `console.*` functions, new
  manifest fields, new resource types) as minor bumps without breaking
  existing carts.
- Breaking changes (removing API surface, changing behavior) require a major
  bump and a migration path for existing carts.
- Tooling (packer, SDK) can warn authors when they use API features that
  aren't available in older runtime versions they want to support.
