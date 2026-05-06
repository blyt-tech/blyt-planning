#!/usr/bin/env python3
"""Spike L Stage 4 step 15 — programmatic save-state regression.

Run case d to frame N, capture state to file. Continue to frame N+M,
capture FNV-1a-64 digest of the framebuffer. Restart core, run to
frame N, load the saved state, run another M frames, capture the
framebuffer digest at frame N+M. The two digests must be byte-equal.

The cross-build / cross-host guarantee is spike K's; this test confirms
the libretro adapter does not perturb that property — that the round
trip through retro_serialize / retro_unserialize is identity-preserving
under continued execution.
"""

import argparse
import sys
from pathlib import Path

import retro_driver as RD


def run_to_frame(core, frames):
    for _ in range(frames):
        core.run()


def fb_digest(core) -> int:
    if not core.last_video_buf:
        return 0
    return RD.fnv1a_64(core.last_video_buf)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--core", required=True)
    p.add_argument("--cart", required=True)
    p.add_argument("--save-frame", type=int, default=100)
    p.add_argument("--continue-frames", type=int, default=100)
    p.add_argument("--out", required=True)
    a = p.parse_args()

    # ── Pass 1: continuous run to frame N + M, capture digest ────────
    core = RD.RetroCore(a.core)
    if not core.load(a.cart):
        sys.exit("FAIL: retro_load_game pass 1")
    run_to_frame(core, a.save_frame + a.continue_frames)
    digest_continuous = fb_digest(core)
    core.close()

    # ── Pass 2: run to N, save, continue, save again, restart, restore,
    #           continue, capture digest, compare ─────────────────────
    core = RD.RetroCore(a.core)
    if not core.load(a.cart):
        sys.exit("FAIL: retro_load_game pass 2")
    run_to_frame(core, a.save_frame)
    saved = core.serialize()
    core.close()

    core = RD.RetroCore(a.core)
    if not core.load(a.cart):
        sys.exit("FAIL: retro_load_game pass 3")
    if not core.unserialize(saved):
        sys.exit("FAIL: retro_unserialize after fresh load")
    run_to_frame(core, a.continue_frames)
    digest_restored = fb_digest(core)
    core.close()

    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w") as f:
        f.write(f"continuous_digest={digest_continuous:016x}\n")
        f.write(f"restored_digest  ={digest_restored:016x}\n")
        f.write(f"saved_size_bytes ={len(saved)}\n")
        f.write(f"save_frame       ={a.save_frame}\n")
        f.write(f"continue_frames  ={a.continue_frames}\n")
        f.write(f"verdict          =")
        if digest_continuous == digest_restored:
            f.write("PASS\n")
            print("PASS: save/restore digest match")
            sys.exit(0)
        else:
            f.write("FAIL\n")
            print(f"FAIL: continuous {digest_continuous:016x} != "
                  f"restored {digest_restored:016x}")
            sys.exit(1)


if __name__ == "__main__":
    main()
