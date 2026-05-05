# Developer Experience

The authoring inner loop and the tools that support it — the CLI packer
and VS Code integration, hot reload via the save/restore mechanism, and
dev-mode instrumentation that compiles away to nothing in release builds.

| # | Decision |
|---|----------|
| [0044](0044-cli-packer-vscode-dev-loop.md) | CLI packer and VS Code dev loop |
| [0045](0045-hot-reload-via-save-restore.md) | Hot reload via the save/restore mechanism |
| [0065](0065-dev-instrumentation-noop-in-release.md) | Dev instrumentation — blyt_dev_* as no-ops in release builds |
| [0074](0074-dev-mode-rewind-capture.md) | Dev-mode rewind capture for debugging |
| [0080](0080-build-and-dev-platform-targets.md) | Build and development platform targets |
| [0088](0088-asset-pipeline.md) | Asset pipeline — packer architecture, resource types, and constant naming |
| [0103](0103-dev-mode-pi-parity-feedback.md) | Dev-mode Pi-parity feedback — throttle vs projection (Proposed) |
| [0104](0104-vscode-dev-shell-runtime-architecture.md) | VS Code dev shell — renderer-side runtime with Node bridge (Proposed) |
