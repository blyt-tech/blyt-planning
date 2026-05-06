-- Spike M Stage 2 — branched cutscene workload (re-entrant form).
--
-- A naïve branched body fails the all-`S` sweep because, on a load
-- resume, the new Lua coroutine restarts at line 1 of the body and
-- always takes the outer loop's first arm — so a save taken inside
-- (or just before) the bonus arm produces a continuation that skips
-- the arm entirely.  PLAN.md § "The body must be re-entrant" calls
-- this out: the body's first action on a restore must reach the
-- equivalent yield point by virtue of `ctx`-encoded program-counter
-- state, not by relying on the Lua coroutine's preserved stack.
--
-- The pattern: each yield site sets `ctx.pc` to a marker; each
-- gating block tests `ctx.pc` so a fresh body restart with `ctx.pc`
-- already set fast-forwards past completed steps and re-enters the
-- block that owns the live yield.  The naïve body is documented as
-- the alternative-fallback (Eris) mechanism's motivation in the
-- spike's result write-up.

local cs = blyt32.coroutine.create(function (ctx)
    while ctx.step < 30 do
        -- Phase A — outer loop's increment + yield.
        if (ctx.pc or "loop_top") == "loop_top" then
            ctx.step  = ctx.step + 1
            ctx.angle = (ctx.step * 17) % 360
            ctx.pc    = "after_a"
            coroutine.yield()
        end

        if ctx.step == 12 then
            -- Phase B — first bonus yield.
            if ctx.pc == "after_a" then
                ctx.bonus = (ctx.bonus or 0) + 5
                ctx.pc    = "after_b"
                coroutine.yield()
            end
            -- Phase C — second bonus yield.
            if ctx.pc == "after_b" then
                ctx.bonus = ctx.bonus + 1
                ctx.pc    = "after_c"
                coroutine.yield()
            end
        end

        ctx.pc = "loop_top"
    end
end, { step = 0, angle = 0 })

local last  = console.num_frames()
local start = console.frame()

for f = start, last - 1 do
    local ok, ctx = blyt32.coroutine.resume(cs)
    if ok and ctx then
        console.add_misc(ctx.step)
        console.add_misc(ctx.angle)
        if ctx.bonus then console.add_misc(ctx.bonus) end
    end
    console.commit_frame()
end
