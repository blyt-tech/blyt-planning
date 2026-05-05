# case_b — GDB stub launch target

Source lives in `spikes/spike-i/cases/case_b/` (`cart_b.c`, `mylib.c`,
`Makefile`). The Spike J Dockerfile copies the built binary into
`spike-j/cases/case_b/cart_b`.

`.vscode/launch.json` here is the F5 attach config for Stage 6 step 22.
Workflow:

1. `make run-gdb` — starts the gdb stub host on port 1234.
2. F5 in VS Code with `cart_b` open — attaches via Native Debug.
3. Set a breakpoint at `mylib_value` in `mylib.c`; continue; observe the
   cross-binary backtrace from `mylib_value` through `fc_cart_draw` into
   `fc_console_main` (libconsole's `.text`).
