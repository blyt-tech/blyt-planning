-- Spike M Stage 6 — negative test: slot-blob bit-flip / truncation.
--
-- A managed coroutine runs to frame 5 normally, saves, then on the
-- harness side the buffer hex is corrupted (a slot-bytes byte
-- bit-flipped) before the load cart consumes it.  The load cart
-- attempts `lua_table_unflatten` and must fail cleanly with
-- BLYT_ERR_FLATTEN_OVERFLOW (or similar) rather than crashing.
--
-- This workload is identical in structure to det_cutscene_linear;
-- the corruption injection lives in scripts/run-truncation-test.sh.

local cs = blyt32.coroutine.create(function (ctx)
    while ctx.step < 30 do
        ctx.step  = ctx.step + 1
        ctx.angle = (ctx.step * 17) % 360
        coroutine.yield()
    end
end, { step = 0, angle = 0 })

local last  = console.num_frames()
local start = console.frame()

for f = start, last - 1 do
    local ok, ctx = blyt32.coroutine.resume(cs)
    if ok and ctx then
        console.add_misc(ctx.step)
        console.add_misc(ctx.angle)
    end
    console.commit_frame()
end
