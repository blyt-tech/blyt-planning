-- Spike N Stage 3 — Lua cart after edit l2 (add POD field).
--
-- l2: add `bonus` field to C-side POD buffer (int32, zero on load).
-- Schema change: v0 → v2.  The migration walk zero-inits bonus.
-- This file is the POST (load) code; the PRE code is det_lua_cart_l1_post.

local function score_for_combo(combo)
    return combo * 50
end

local cs = blyt32.coroutine.create(function(ctx)
    while ctx.step < 30 do
        ctx.step = ctx.step + 1
        ctx.angle = (ctx.step * 17) % 360
        coroutine.yield()
    end
end, { step = 0, angle = 0 })

local last  = console.num_frames()
local start = console.frame()

for f = start, last - 1 do
    local ok, ctx = blyt32.coroutine.resume(cs)
    if ok and ctx then
        local st = console.get_cart_state()
        st.step   = ctx.step
        st.combo  = ctx.step % 10
        st.score  = score_for_combo(st.combo)
        -- bonus was zero-init'd by migration; accumulate from step.
        st.bonus  = (st.bonus or 0) + ctx.step
        console.set_cart_state(st)
        console.add_misc(st.score + ctx.step + (st.bonus or 0))
    end
    console.commit_frame()
end
