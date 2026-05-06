#!/usr/bin/env python3
"""Spike L Stage 4 step 17 — rewind capacity calculation.

Compute how many frames RetroArch's default rewind buffer holds at the
core's measured serialize_upper_bound, and convert to seconds at the
declared fps. Spike L's gate is ≥ 5 s of rewind history.
"""

import argparse
import sys
from pathlib import Path

import retro_driver as RD


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--core", required=True)
    p.add_argument("--cart", required=True)
    p.add_argument("--buffer-mb", type=float, default=10.0,
                   help="RetroArch default rewind buffer is ~10 MB.")
    p.add_argument("--fps", type=int, default=60)
    p.add_argument("--out", required=True)
    a = p.parse_args()

    core = RD.RetroCore(a.core)
    if not core.load(a.cart):
        sys.exit("FAIL: retro_load_game")
    upper_bound = core.serialize_size()

    # RetroArch reserves a small overhead per snapshot for diff metadata
    # (rewind stores deltas between consecutive snapshots after the first).
    # We treat the bound conservatively: capacity = buffer_bytes / upper_bound.
    buffer_bytes = int(a.buffer_mb * 1024 * 1024)
    frames       = buffer_bytes // upper_bound if upper_bound else 0
    seconds      = frames / float(a.fps)

    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w") as f:
        f.write(f"serialize_upper_bound_bytes={upper_bound}\n")
        f.write(f"buffer_size_bytes={buffer_bytes}\n")
        f.write(f"declared_fps={a.fps}\n")
        f.write(f"capacity_frames={frames}\n")
        f.write(f"capacity_seconds={seconds:.2f}\n")
        verdict = "PASS" if seconds >= 5.0 else "FAIL"
        f.write(f"verdict={verdict}\n")

    if seconds >= 5.0:
        print(f"PASS: rewind capacity {seconds:.1f} s "
              f"({frames} frames, {upper_bound} B/frame)")
        core.close()
        sys.exit(0)
    print(f"FAIL: capacity {seconds:.2f} s < 5 s gate")
    core.close()
    sys.exit(1)


if __name__ == "__main__":
    main()
