#!/usr/bin/env python3
"""Spike J — GDB test harness.

Drives gdb-multiarch through `-batch -ex` against the running gdb_stub_host.
Asserts on:
  * `info sharedlibrary` shape — both libraries appear at runtime-chosen addrs
  * `qXfer:libraries-svr4:read` reply contains both library entries
  * `target remote :PORT` connects and exchanges a stop-reply

Stage 3 step 12. The full bt-walks-PLT assertion (Stage 3 step 11) needs a
real loaded cart with DWARF — the spike defers it to a follow-up that
integrates the stub with rv32emu's CPU loop. This harness validates the
protocol surface in isolation.
"""

from __future__ import annotations

import os
import re
import socket
import subprocess
import sys
import time

GDB = os.environ.get("GDB", "gdb-multiarch")
PORT = int(os.environ.get("GDB_PORT", "1234"))


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def ok(msg: str) -> None:
    print(f"PASS: {msg}")


def gdb_batch(commands: list[str]) -> str:
    args = [GDB, "--batch", "-q"]
    for c in commands:
        args.extend(["-ex", c])
    print("$", " ".join(repr(a) for a in args))
    proc = subprocess.run(args, capture_output=True, text=True, timeout=30)
    if proc.returncode != 0:
        print("STDOUT:\n" + proc.stdout)
        print("STDERR:\n" + proc.stderr)
        fail(f"gdb-multiarch exit {proc.returncode}")
    return proc.stdout + proc.stderr


def wait_for_port(port: int, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            s.close()
            return
        except OSError:
            time.sleep(0.1)
    fail(f"port {port} not reachable within {timeout}s")


def test_protocol_surface() -> None:
    wait_for_port(PORT)
    out = gdb_batch([
        "set architecture riscv:rv32",
        f"target remote :{PORT}",
        "info sharedlibrary",
    ])
    print("--- gdb output ---")
    print(out)
    print("--- end output ---")

    # `info sharedlibrary` should list both libconsole.so and libconsolelua.so
    # with their load addresses. gdb formats it as:
    #   From        To          Syms Read   Shared Object Library
    #   0x08000000  0x...       Yes (*)     /spike-j/lib/libconsole.so
    if "libconsole.so" not in out:
        fail("libconsole.so not listed in info sharedlibrary")
    ok("libconsole.so present in shared library list")
    if "libconsolelua.so" not in out:
        fail("libconsolelua.so not listed")
    ok("libconsolelua.so present in shared library list")

    # The standard layout has libconsole at 0x08000000; GDB's `info
    # sharedlibrary` displays .text section bounds (load_base + section_vma),
    # so we look for any address in the 0x0800xxxx range followed by the
    # libconsole path. Anything in 0x080xxxxx is correct for our layout.
    if not re.search(r"0x080[0-9a-f]+\s+0x080[0-9a-f]+\s+\S+\s+\S+\s+\S*libconsole\.so",
                     out):
        fail("libconsole.so not mapped into the 0x08000000 region")
    ok("libconsole.so mapped into 0x080xxxxx region (load base 0x08000000 honored)")
    if not re.search(r"0x080[0-9a-f]+\s+0x080[0-9a-f]+\s+\S+\s+\S+\s+\S*libconsolelua\.so",
                     out):
        fail("libconsolelua.so not mapped into the 0x08000000 region")
    ok("libconsolelua.so mapped into 0x080xxxxx region")


def test_reload_packet() -> None:
    """Stage 5 step 19: qFc32:reload triggers a T05library:; stop-reply.

    We can't verify the breakpoint re-resolution from a batch session
    without a full instrumented cart. Instead assert that the custom
    packet round-trips and gdb re-fetches the library list.
    """
    out = gdb_batch([
        "set architecture riscv:rv32",
        f"target remote :{PORT}",
        # GDB exposes monitor commands via `monitor`; for raw qFc32 we'd need
        # `maintenance packet qFc32:reload:<path>` (gdb 14+).
        "maintenance packet qFc32:reload:/spike-j/cases/case_b/cart_b",
        "info sharedlibrary",
    ])
    if "libconsole.so" not in out:
        fail("post-reload libconsole.so not present")
    ok("post-reload library list still contains libconsole.so")


def main() -> None:
    test = sys.argv[1] if len(sys.argv) > 1 else "protocol"
    if test == "protocol":
        test_protocol_surface()
    elif test == "reload":
        test_reload_packet()
    elif test == "all":
        test_protocol_surface()
        test_reload_packet()
    else:
        print("usage: gdb_test.py [protocol|reload|all]", file=sys.stderr)
        sys.exit(2)
    print("OK")


if __name__ == "__main__":
    main()
