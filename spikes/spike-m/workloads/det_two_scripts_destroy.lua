-- Spike M Stage 3 — destroy/recreate validates slot reclamation.
--
-- Frames 0..19: cutscene (slot 0) + AI (slot 1) running.
-- Frame 20: cart calls `blyt32.coroutine.destroy(ai)`; slot 1 freed.
-- Frame 21: cart creates a fresh "loader" script; gets slot 1.
-- Frames 21..29: cutscene + loader running.
--
-- The buffer at save frame S=20 must contain slot 0 only;
-- at S∈[21,29] it must contain slots 0 and 1, with slot 1's
-- blob shape and contents matching the LOADER, not the AI.
--
-- The cart structurally encodes the topology so its load-side
-- create order matches the save-side slot allocation order.  The
-- alternative (run-time body-id check inside the wrapper) is
-- discussed in the result write-up; the structural approach keeps
-- the wrapper minimal at the cost of cart authoring discipline.

local cs_body = function (ctx)
    while ctx.step < 30 do
        if (ctx.pc or "loop_top") == "loop_top" then
            ctx.step  = ctx.step + 1
            ctx.angle = (ctx.step * 17) % 360
            ctx.pc    = "after"
            coroutine.yield()
        end
        ctx.pc = "loop_top"
    end
end

local ai_body = function (ctx)
    while ctx.tics < 30 do
        if (ctx.pc or "loop_top") == "loop_top" then
            ctx.tics    = ctx.tics + 1
            ctx.chasing = (ctx.tics % 6) >= 3
            ctx.pc      = "after"
            coroutine.yield()
        end
        ctx.pc = "loop_top"
    end
end

local loader_body = function (ctx)
    while ctx.progress < 100 do
        if (ctx.pc or "loop_top") == "loop_top" then
            ctx.progress = ctx.progress + 7
            ctx.pc       = "after"
            coroutine.yield()
        end
        ctx.pc = "loop_top"
    end
end

local resume_frame = console.frame()
local cs, ai, loader

if console.is_load_resume() and resume_frame > 20 then
    -- AI was destroyed at frame 20 — don't recreate it.
    cs = blyt32.coroutine.create(cs_body, { step = 0, angle = 0 })
    if resume_frame > 21 then
        -- Loader was created at frame 21 — recreate (with saved bytes).
        loader = blyt32.coroutine.create(loader_body, { progress = 0 })
    end
else
    -- Fresh run, or load before destroy.
    cs = blyt32.coroutine.create(cs_body, { step = 0, angle = 0 })
    ai = blyt32.coroutine.create(ai_body, { tics = 0, chasing = false })
end

local last  = console.num_frames()
local start = resume_frame

for f = start, last - 1 do
    if f == 20 and ai then
        blyt32.coroutine.destroy(ai)
        ai = nil
    end
    if f == 21 and not loader then
        loader = blyt32.coroutine.create(loader_body, { progress = 0 })
    end

    if cs then
        local _, ctx = blyt32.coroutine.resume(cs)
        if ctx then
            console.add_misc(ctx.step)
            console.add_misc(ctx.angle)
        end
    end
    if ai then
        local _, ctx = blyt32.coroutine.resume(ai)
        if ctx then
            console.add_misc(ctx.tics)
            console.add_misc(ctx.chasing and 1 or 0)
        end
    end
    if loader then
        local _, ctx = blyt32.coroutine.resume(loader)
        if ctx then
            console.add_misc(ctx.progress)
        end
    end
    console.commit_frame()
end
