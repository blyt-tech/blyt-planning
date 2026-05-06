-- Spike N Stage 3 — Lua cart after edit l3 (remove POD field).
--
-- l3: remove `bonus` field from the C-side POD buffer.
-- Schema change: v2 → v3.  The migration walk drops bonus.
-- PRE code is det_lua_cart_l2_post (which has bonus).

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
        -- bonus field no longer exists in this version.
        console.set_cart_state(st)
        console.add_misc(st.score + ctx.step)
    end
    console.commit_frame()
end
