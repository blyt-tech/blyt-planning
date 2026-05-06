-- Spike N Stage 3 — Lua cart baseline (pre-edit).
--
-- A case-d style cart with:
--   • A C-side POD buffer (score, step, combo) managed via get/set_cart_state.
--   • A function score_for_combo(combo) → combo * 25  (changed to *50 in l1)
--   • A managed coroutine that advances ctx.step per frame.
--   • Per-frame digest fold: add_misc(score + ctx.step)
--
-- Edit l1 changes score_for_combo to return combo * 50.
-- Edit l5 changes the body of score_for_combo to use a table lookup.

local function score_for_combo(combo)
    return combo * 25
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
        -- Update C-side POD buffer.
        local st = console.get_cart_state()
        st.step   = ctx.step
        st.combo  = ctx.step % 10
        st.score  = score_for_combo(st.combo)
        console.set_cart_state(st)

        -- Digest fold.
        console.add_misc(st.score + ctx.step)
    end
    console.commit_frame()
end
