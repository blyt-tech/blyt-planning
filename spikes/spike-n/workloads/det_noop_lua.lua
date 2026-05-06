-- Spike N Stage 1 — no-op Lua cart (floor case for hot reload).
--
-- Single managed coroutine that increments ctx.step per frame.
-- The no-op edit is: rebuild with no source change.  Pre/post buffer
-- bytes must be identical (the floor test).
--
-- Per-frame digest fold: console.add_misc(step).

local cs = blyt32.coroutine.create(function(ctx)
    while ctx.step < 30 do
        ctx.step = ctx.step + 1
        coroutine.yield()
    end
end, { step = 0 })

local last  = console.num_frames()
local start = console.frame()

-- On a load resume, restore cart state from the POD buffer.
if console.is_load_resume() then
    local saved = console.get_cart_state()
    -- The coroutine's ctx is POD and is restored via the persistent_scripts
    -- region; the C-side lua_cart_state is a separate buffer for score/step.
    if saved then
        -- Nothing to do — ctx is already in the slot blob.
    end
end

for f = start, last - 1 do
    local ok, ctx = blyt32.coroutine.resume(cs)
    if ok and ctx then
        console.add_misc(ctx.step)
        -- Also track in the C-side POD buffer for migration tests.
        local st = console.get_cart_state()
        st.step = ctx.step
        st.score = ctx.step * 2
        st.combo = (ctx.step % 10)
        console.set_cart_state(st)
    end
    console.commit_frame()
end
