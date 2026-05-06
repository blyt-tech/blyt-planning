-- Spike M Stage 3 — two parallel scripts, no entity references.
--
-- Validates that two managed coroutines running concurrently each
-- get their own slot, their own ctx blob bytes, and round-trip
-- independently across save/restore.  Slot 0 is a cutscene, slot 1
-- is a simple AI script that toggles state every 3 ticks.
--
-- Both bodies use the re-entrant `ctx.pc` idiom from Stage 2 so the
-- all-S sweep passes.  Per-frame digest folds in fields from each
-- script's ctx so any cross-pollination at the slot level shows up
-- as a localised digest divergence.

local cutscene = blyt32.coroutine.create(function (ctx)
    while ctx.step < 30 do
        if (ctx.pc or "loop_top") == "loop_top" then
            ctx.step  = ctx.step + 1
            ctx.angle = (ctx.step * 17) % 360
            ctx.pc    = "after"
            coroutine.yield()
        end
        ctx.pc = "loop_top"
    end
end, { step = 0, angle = 0 })

local ai = blyt32.coroutine.create(function (ctx)
    while ctx.tics < 30 do
        if (ctx.pc or "loop_top") == "loop_top" then
            ctx.tics    = ctx.tics + 1
            ctx.chasing = (ctx.tics % 6) >= 3
            ctx.pc      = "after"
            coroutine.yield()
        end
        ctx.pc = "loop_top"
    end
end, { tics = 0, chasing = false })

local last  = console.num_frames()
local start = console.frame()

for f = start, last - 1 do
    local _, cs_ctx = blyt32.coroutine.resume(cutscene)
    local _, ai_ctx = blyt32.coroutine.resume(ai)
    if cs_ctx then
        console.add_misc(cs_ctx.step)
        console.add_misc(cs_ctx.angle)
    end
    if ai_ctx then
        console.add_misc(ai_ctx.tics)
        console.add_misc(ai_ctx.chasing and 1 or 0)
    end
    console.commit_frame()
end
