-- Spike M Stage 4 — exercises every flattener value subtype.
--
-- One managed coroutine whose ctx mutates fields of every supported
-- subtype on every resume:
--   integer      -> ctx.integer     = ctx.step * 13       (i64)
--   float        -> ctx.angle       = math.sin(ctx.step)  (f64, NaN-canon)
--   string       -> ctx.text        = string.format(...)
--   boolean      -> ctx.flag        = (ctx.step % 2 == 0)
--   flat array   -> ctx.list        = {step, step+1, step+2}  (i64 elems)
--
-- The headline cross-host gate is buffer-byte equality at every save
-- frame: if the flattener's output diverges across hosts on any
-- subtype, the buffer hex differs.  Stages 1-3 already exercised the
-- buffer-byte gate; this workload tightens it by ensuring every
-- subtype is touched in every saved blob, so a divergence in any one
-- subtype's encoding produces a visible bit-flip in slot 0's bytes.

local cs = blyt32.coroutine.create(function (ctx)
    while ctx.step < 30 do
        ctx.step    = ctx.step + 1
        ctx.angle   = math.sin(ctx.step) * 1000.0
        ctx.integer = ctx.step * 13
        ctx.text    = string.format("step %d", ctx.step)
        ctx.flag    = (ctx.step % 2 == 0)
        ctx.list    = { ctx.step, ctx.step + 1, ctx.step + 2 }
        coroutine.yield()
    end
end, { step = 0 })

local last  = console.num_frames()
local start = console.frame()

for f = start, last - 1 do
    local _, ctx = blyt32.coroutine.resume(cs)
    if ctx then
        console.add_misc(ctx.step)
        console.add_misc(ctx.angle)
        console.add_misc(ctx.integer)
        -- Strings can't be folded directly; fold its length plus a
        -- byte-sum so a flattener bug in string encoding shows up.
        if ctx.text then
            local sum = 0
            for i = 1, #ctx.text do sum = sum + string.byte(ctx.text, i) end
            console.add_misc(#ctx.text)
            console.add_misc(sum)
        end
        console.add_misc(ctx.flag and 1 or 0)
        if ctx.list then
            for _, v in ipairs(ctx.list) do console.add_misc(v) end
        end
    end
    console.commit_frame()
end
