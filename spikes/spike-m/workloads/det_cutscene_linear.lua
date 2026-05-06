-- Spike M Stage 1 — linear cutscene workload.
--
-- Single managed coroutine that increments `ctx.step` and `ctx.angle`
-- once per frame.  The body's loop condition (`while ctx.step < 30`)
-- is the resume marker on the load side: on a fresh re-entry of the
-- coroutine after a save/restore, `ctx.step` is the deserialized
-- value and the loop fast-forwards naturally.
--
-- Per-frame digest fold: console.add_misc(step) + console.add_misc(angle).
-- Any flatten/unflatten bug shows up as a divergence in `ctx.step` or
-- `ctx.angle` at the very next frame.

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
