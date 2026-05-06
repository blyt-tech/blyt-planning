-- Spike N Stage 3 — Lua cart after edit l1 (body change).
--
-- l1: change score_for_combo to return combo * 50 instead of combo * 25.
-- No schema change — POD state survives byte-equal; only the score
-- computed from frame S+1 onward changes.

local function score_for_combo(combo)
    return combo * 50   -- changed from * 25
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
        console.set_cart_state(st)
        console.add_misc(st.score + ctx.step)
    end
    console.commit_frame()
end
