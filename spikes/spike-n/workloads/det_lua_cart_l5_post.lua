-- Spike N Stage 3 — Lua cart after edit l5 (function-body change, case i).
--
-- l5: change score_for_combo body to use a table lookup.
-- No schema change; no closure or coroutine holds a reference to the
-- old function — the runtime rebuilds the closure fresh on reload.
-- PRE code is det_lua_cart_l4_post (combo_mult version).

local SCORE_TABLE = { 0, 50, 100, 150, 200, 250, 300, 350, 400, 450 }

local function score_for_combo(combo_mult)
    -- New body: table lookup instead of multiplication.
    local idx = math.floor(combo_mult * 10) + 1
    if idx < 1 then idx = 1 end
    if idx > #SCORE_TABLE then idx = #SCORE_TABLE end
    return SCORE_TABLE[idx]
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
        st.combo_mult = (ctx.step % 10) * 0.1
        st.score      = score_for_combo(st.combo_mult)
        console.set_cart_state(st)
        console.add_misc(st.score + ctx.step)
    end
    console.commit_frame()
end
