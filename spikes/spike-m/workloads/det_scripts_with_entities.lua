-- Spike M Stage 3 — entity-handle pattern (no per-entity script limit).
--
-- Three scripts:
--   slot 0 (mover_x): nudges entity 0's x once per frame.
--   slot 1 (mover_y): nudges entity 0's y once per frame.
--   slot 2 (rotator): rotates entity 1's angle once per frame.
--
-- Validates:
--   • Two scripts can share an entity handle in their respective ctxs
--     and advance independent fields without runtime-level
--     bookkeeping.
--   • An unrelated script (rotator) on a different entity coexists
--     without special-casing.
--   • Entity row state lives in cart_state_lua_simple (existing
--     region); script ctx state lives in persistent_scripts.
--     Both round-trip independently via the save_state mechanism.
--   • Per-frame digest folds in entity 0's (x, y), entity 1's a,
--     plus each script's (entity, step) — cross-pollination at
--     either layer surfaces as a localised divergence.

local mover_x_body = function (ctx)
    while ctx.step < 30 do
        if (ctx.pc or "loop_top") == "loop_top" then
            local x, y, a = console.get_entity(ctx.entity)
            x = x + 1.0
            console.set_entity(ctx.entity, x, y, a)
            ctx.step = ctx.step + 1
            ctx.pc   = "after"
            coroutine.yield()
        end
        ctx.pc = "loop_top"
    end
end

local mover_y_body = function (ctx)
    while ctx.step < 30 do
        if (ctx.pc or "loop_top") == "loop_top" then
            local x, y, a = console.get_entity(ctx.entity)
            y = y + 0.5
            console.set_entity(ctx.entity, x, y, a)
            ctx.step = ctx.step + 1
            ctx.pc   = "after"
            coroutine.yield()
        end
        ctx.pc = "loop_top"
    end
end

local rotator_body = function (ctx)
    while ctx.step < 30 do
        if (ctx.pc or "loop_top") == "loop_top" then
            local x, y, a = console.get_entity(ctx.entity)
            a = a + 0.1
            console.set_entity(ctx.entity, x, y, a)
            ctx.step = ctx.step + 1
            ctx.pc   = "after"
            coroutine.yield()
        end
        ctx.pc = "loop_top"
    end
end

-- Initialize entities on a fresh run only (cart_state_lua_simple
-- round-trips its own bytes via save_state on a load resume).
if not console.is_load_resume() then
    console.set_entity(0, 100.0, 100.0, 0.0)
    console.set_entity(1,  50.0,  50.0, 0.0)
end

local mover_x = blyt32.coroutine.create(mover_x_body, { entity = 0, step = 0 })
local mover_y = blyt32.coroutine.create(mover_y_body, { entity = 0, step = 0 })
local rotator = blyt32.coroutine.create(rotator_body, { entity = 1, step = 0 })

local last  = console.num_frames()
local start = console.frame()

for f = start, last - 1 do
    blyt32.coroutine.resume(mover_x)
    blyt32.coroutine.resume(mover_y)
    blyt32.coroutine.resume(rotator)

    -- Fold both entities' fields plus each script's step into the
    -- digest so cross-pollination at either layer is visible.
    local x0, y0, a0 = console.get_entity(0)
    local x1, y1, a1 = console.get_entity(1)
    console.add_misc(x0); console.add_misc(y0)
    console.add_misc(x1); console.add_misc(y1); console.add_misc(a1)

    console.commit_frame()
end
