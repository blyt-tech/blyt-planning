-- Spike N Stage 3 — Lua cart after edit l4 (retype field).
--
-- l4: retype `combo` (int32) → `combo_mult` (float32) in the C-side buffer.
-- on_retype fires: old integer combo is mapped to combo * 0.1f.
-- PRE code is det_lua_cart_l3_post (combo as int32).

local function score_for_combo(combo_mult)
    -- Now uses float multiplier instead of int combo.
    return combo_mult * 500.0
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
        st.step       = ctx.step
        -- combo_mult was set by on_retype to old_combo * 0.1; update each frame.
        st.combo_mult = (ctx.step % 10) * 0.1
        st.score      = math.floor(score_for_combo(st.combo_mult))
        console.set_cart_state(st)
        console.add_misc(st.score + ctx.step)
    end
    console.commit_frame()
end
