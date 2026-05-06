-- Spike M Stage 3 — auto-reclaim on body return.
--
-- A short script whose body completes (returns from the loop) at
-- frame 10.  After return, the wrapper auto-frees the slot; from
-- frame 11 onwards the cart sees `resume()` returning `(false, nil)`.
--
-- The save buffer at S∈[1,9] contains slot 0 with the script's ctx;
-- at S∈[10,29] the slot is empty (auto-reclaimed by the wrapper
-- when the body returned).  On a load resume the cart uses
-- `console.script_has_saved_bytes(0)` to decide whether to re-create
-- the script — without this check, the cart would unconditionally
-- recreate a fresh script post-completion and diverge from the
-- same-host straight-through (which has no script to fold after
-- frame 10).

local handle
if not console.is_load_resume() or console.script_has_saved_bytes(0) then
    handle = blyt32.coroutine.create(function (ctx)
        while ctx.step < 10 do
            if (ctx.pc or "loop_top") == "loop_top" then
                ctx.step = ctx.step + 1
                ctx.pc   = "after"
                coroutine.yield()
            end
            ctx.pc = "loop_top"
        end
    end, { step = 0 })
end

local last  = console.num_frames()
local start = console.frame()

for f = start, last - 1 do
    if handle then
        local ok, ctx = blyt32.coroutine.resume(handle)
        if ok and ctx then
            console.add_misc(ctx.step)
        end
        if not ok then handle = nil end
    end
    console.commit_frame()
end
