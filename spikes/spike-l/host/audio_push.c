/* Spike L — per-render-frame audio batch sizing with drift correction.
 *
 * Libretro's audio model: the core calls audio_sample_batch_t(buf, N)
 * once per retro_run; the frontend mixes that into its host audio at the
 * sample rate declared in retro_get_system_av_info. The runtime mixer
 * produces samples at its own internal cadence (48 kHz in spike L) and
 * the adapter has to emit the right batch count per render frame so the
 * cumulative drift stays bounded.
 *
 * Approach: target = sample_rate / declared_fps per call (e.g. 800 frames
 * per call at 48 kHz / 60 fps), with a single-sample correction term
 * that re-aligns the cumulative count whenever it has drifted away from
 * the ideal (= run_calls * sample_rate / declared_fps). This keeps the
 * total drift to ≤ 1 sample at any point — the gate from PLAN.md
 * Stage 3 step 12.
 *
 * If RetroArch's frontend audio rate ever differs from the declared
 * runtime rate (some platforms run libretro cores at non-60 cadences),
 * the adapter would resample here. Spike L's gate is 60 fps + 48 kHz,
 * so the resampler stays a no-op and is intentionally not implemented
 * (engineering on top per PLAN.md §audio decision).
 */

#include <stdint.h>

int adapter_compute_audio_frames(uint32_t sample_rate,
                                 uint32_t declared_fps,
                                 uint64_t total_pushed,
                                 uint64_t total_run_calls)
{
    if (declared_fps == 0) return 0;
    /* Ideal cumulative count after the *next* push completes:
     *     ideal_after = (total_run_calls + 1) * sample_rate / declared_fps
     * We push exactly enough samples this round to land on ideal_after.
     * The integer division absorbs the per-frame fractional remainder
     * inside `total_pushed`'s monotonic count, so over N rounds the
     * cumulative count tracks ideal exactly modulo a one-sample boundary
     * at each roll-over. */
    uint64_t ideal_after =
        (total_run_calls + 1) * (uint64_t)sample_rate / (uint64_t)declared_fps;
    if (ideal_after <= total_pushed) return 0;
    return (int)(ideal_after - total_pushed);
}
