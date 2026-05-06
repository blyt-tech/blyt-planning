#!/usr/bin/env python3
"""Spike L Stage 3 step 12 — audio cadence sanity check.

A 60-second run in RetroArch should produce 60 × 48000 = 2,880,000
stereo samples ± 1 sample. This script invokes retro_run for the given
number of frames and asserts the total stereo sample count is within
1 of the ideal.
"""

import argparse
import sys
from pathlib import Path

import retro_driver as RD


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--core", required=True)
    p.add_argument("--cart", required=True)
    p.add_argument("--frames", type=int, default=3600,
                   help="Number of retro_run iterations (60 fps × 60 s = 3600).")
    p.add_argument("--out", required=True)
    a = p.parse_args()

    core = RD.RetroCore(a.core)
    if not core.load(a.cart):
        sys.exit("FAIL: retro_load_game")
    info = core.av_info()
    sample_rate = int(info.timing.sample_rate)
    fps         = int(info.timing.fps)

    core.clear_audio()
    for _ in range(a.frames):
        core.run()
    actual = len(core.audio.samples)
    ideal  = a.frames * sample_rate // fps
    drift  = actual - ideal

    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w") as f:
        f.write("metric,value\n")
        f.write(f"frames,{a.frames}\n")
        f.write(f"sample_rate,{sample_rate}\n")
        f.write(f"fps,{fps}\n")
        f.write(f"ideal_samples,{ideal}\n")
        f.write(f"actual_samples,{actual}\n")
        f.write(f"drift_samples,{drift}\n")
        f.write(f"verdict,{'PASS' if abs(drift) <= 1 else 'FAIL'}\n")

    if abs(drift) <= 1:
        print(f"PASS: {actual} samples, drift {drift}")
        core.close()
        sys.exit(0)
    print(f"FAIL: drift {drift} > 1 sample (actual={actual} ideal={ideal})")
    core.close()
    sys.exit(1)


if __name__ == "__main__":
    main()
