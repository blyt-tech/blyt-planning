#!/usr/bin/env python3
"""Spike L Stage 4 step 18 — layout-hash-mismatch negative test.

Save state in the primary core (cart_state_t with N fields). Attempt to
load that buffer in the alt-layout core (cart_state_t with N+1 fields).
Expected outcome: retro_unserialize → false (header layout_hash differs
from the alt build's runtime layout_hash). No crash.
"""

import argparse
import sys
from pathlib import Path

import retro_driver as RD


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--core", required=True, help="primary core .so")
    p.add_argument("--alt",  required=True, help="alt-layout core .so")
    p.add_argument("--cart", required=True)
    p.add_argument("--save-frame", type=int, default=100)
    p.add_argument("--out", required=True)
    a = p.parse_args()

    core = RD.RetroCore(a.core)
    if not core.load(a.cart):
        sys.exit("FAIL: load primary")
    for _ in range(a.save_frame):
        core.run()
    saved = core.serialize()
    primary_size = len(saved)
    core.close()

    alt = RD.RetroCore(a.alt)
    if not alt.load(a.cart):
        sys.exit("FAIL: load alt")
    alt_upper = alt.serialize_size()
    rejected = not alt.unserialize(saved)
    alt.close()

    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w") as f:
        f.write(f"primary_save_bytes={primary_size}\n")
        f.write(f"alt_upper_bound={alt_upper}\n")
        f.write(f"alt_unserialize_rejected={rejected}\n")
        f.write(f"verdict={'PASS' if rejected else 'FAIL'}\n")

    if rejected:
        print(f"PASS: alt build rejected primary buffer "
              f"(primary={primary_size}B, alt_bound={alt_upper}B)")
        sys.exit(0)
    print("FAIL: alt build accepted a buffer with a different layout_hash")
    sys.exit(1)


if __name__ == "__main__":
    main()
