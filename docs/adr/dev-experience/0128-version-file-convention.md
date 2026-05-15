# ADR-0128: Version file convention

## Status

Accepted.

## Context

The `blyt` repository contains multiple build systems (CMake for the C
runtime, Cargo for `blyt`, `Makefile.libretro` for the libretro
buildbot) that each need to embed or report the same version string. CI
runs the C and Rust builds as separate jobs. A single authoritative source
is needed so all artefacts from one build carry the same version, and so
that the convention is obvious to anyone adding a new build component.

Dev builds need stable version strings locally — a timestamp that changes
every second would cause unnecessary rebuilds across CMake, Cargo, and any
derived artefacts.

## Decision

### `version.txt` — the committed source of truth

A plain-text file `version.txt` at the repository root contains the
current version. It contains exactly one line in one of two forms:

```
1.2.3
```
```
1.2.3-dev
```

A release version contains no suffix. A development version has the `-dev`
suffix. This file is committed and is the only place the version is
maintained.

### `build/version.txt` — the generated build-time version

All build systems read the version from `build/version.txt`, not directly
from `version.txt`. `build/` is gitignored; `build/version.txt` is never
committed.

The content of `build/version.txt` depends on context:

| Context | Content |
|---|---|
| Release version (`1.2.3`) | `1.2.3` — identical to `version.txt` |
| Dev version, local dev | `1.2.3-dev` — verbatim copy, no timestamp |
| Dev version, CI | `1.2.3-dev-20260502-234512` — timestamp appended |
| Dev version, libretro buildbot | `1.2.3-dev-20260502-234512` — timestamp appended |

Timestamp format: `YYYYMMDD-HHMMSS`, second precision.

**Local dev has no timestamp.** A timestamp that changes every second
would invalidate CMake targets and Cargo outputs on every build invocation.
Dev builds are clearly identified by the `-dev` suffix; the timestamp adds
no value locally.

### How each build system creates or consumes `build/version.txt`

**CMake (local dev and CI):**
During CMake configure, if `build/version.txt` does not already exist,
CMake copies `version.txt` verbatim to `build/version.txt`. If the file
already exists (because the CI workflow pre-created it with a timestamp),
CMake leaves it unchanged.

```cmake
if(NOT EXISTS "${CMAKE_BINARY_DIR}/version.txt")
    file(COPY "${CMAKE_SOURCE_DIR}/version.txt"
         DESTINATION "${CMAKE_BINARY_DIR}")
endif()
file(READ "${CMAKE_BINARY_DIR}/version.txt" BLYT_VERSION)
string(STRIP "${BLYT_VERSION}" BLYT_VERSION)
```

**Cargo / `blyt` (`build.rs`):**
`build.rs` reads `build/version.txt` and exposes the version string as a
compile-time environment variable. It also declares a rerun dependency so
Cargo re-embeds the version if the file changes.

```rust
let version = std::fs::read_to_string("../../build/version.txt")
    .expect("build/version.txt not found — run CMake configure first");
println!("cargo:rustc-env=BLYT_VERSION={}", version.trim());
println!("cargo:rerun-if-changed=../../build/version.txt");
```

The CMake build ordering constraint (CMake runs before Cargo, because CMake
produces the `luac` binary that `blyt` embeds) means `build/version.txt`
is always present before Cargo runs.

**`Makefile.libretro` (libretro buildbot):**
The Makefile creates `build/version.txt` itself at the start of the build.
It reads `version.txt`, appends a timestamp if the version ends in `-dev`,
and writes the result.

```makefile
VERSION_BASE := $(shell cat version.txt)
ifeq ($(findstring -dev,$(VERSION_BASE)),-dev)
    BLYT_VERSION := $(VERSION_BASE)-$(shell date +%Y%m%d-%H%M%S)
else
    BLYT_VERSION := $(VERSION_BASE)
endif

$(shell mkdir -p build && echo "$(BLYT_VERSION)" > build/version.txt)
```

### CI workflow (GitHub Actions)

The CI workflow creates `build/version.txt` as its first step, before
invoking CMake or Cargo. All subsequent jobs in the workflow that need the
version either read `build/version.txt` directly (if sharing a filesystem
within one job) or receive it as a workflow-level output passed between jobs.

```yaml
- name: Stamp version
  run: |
    mkdir -p build
    VERSION=$(cat version.txt)
    if [[ "$VERSION" == *-dev ]]; then
      echo "${VERSION}-$(date +%Y%m%d-%H%M%S)" > build/version.txt
    else
      cp version.txt build/version.txt
    fi
    echo "BLYT_VERSION=$(cat build/version.txt)" >> "$GITHUB_OUTPUT"
```

The `GITHUB_OUTPUT` export makes the version string available to downstream
jobs without requiring them to re-read the file, which may not be present
if jobs run on separate runners.

## Consequences

- One line in `version.txt` is the only maintenance point for versioning.
  Cutting a release is: update `version.txt` from `1.2.3-dev` to `1.2.3`,
  tag, then update to `1.3.0-dev`.
- Local dev builds are stable: the version string does not change between
  invocations, so no targets are invalidated by a clock tick.
- CI and buildbot artefacts carry a timestamp that makes two dev builds
  distinguishable from each other, which is useful for bug reports and
  binary distribution.
- Adding a new build component that needs the version requires only reading
  `build/version.txt`; no convention needs to be invented per component.
- The CMake "copy if not exists" rule is the mechanism that makes local dev
  seamless. It must not be changed to "always copy" or CI timestamps would
  be overwritten by the verbatim content.
