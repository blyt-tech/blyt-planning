-- Spike N Stage 5 — Lua cart after edit l6 (rename with stale call site, case ii).
--
-- l6: rename score_for_combo → score_v2, update only one call site.
-- The second call site still uses score_for_combo → Lua error on init.
-- Expected: FAIL-WITH-DIAGNOSTIC (Lua "attempt to call a nil value").

local function score_v2(combo)    -- renamed from score_for_combo
    return combo * 25
end

-- Stale call site: score_for_combo still referenced here — will throw.
local function score_for_frame(f)
    return score_for_combo(f % 10)  -- BUG: name not updated
end

local game_cs = blyt32.coroutine.create(function(ctx)
    while ctx.step < 30 do
        ctx.step = ctx.step + 1
        ctx.angle = (ctx.step * 17) % 360
        coroutine.yield()
    end
end, { step = 0, angle = 0 })

local NPC_DIALOG = { "Hello.", "Goodbye." }

local function update_npc_dialog(ctx)
    while ctx.dialog_step < #NPC_DIALOG do
        ctx.dialog_step = ctx.dialog_step + 1
        coroutine.yield()
    end
end

local npc_cs = blyt32.coroutine.create(update_npc_dialog, { dialog_step = 0 })

local last  = console.num_frames()
local start = console.frame()

for f = start, last - 1 do
    local ok, ctx = blyt32.coroutine.resume(game_cs)
    blyt32.coroutine.resume(npc_cs)

    if ok and ctx then
        local st = console.get_cart_state()
        st.step   = ctx.step
        st.combo  = ctx.step % 10
        -- This call will throw on the first frame if score_for_combo is nil.
        local ok2, score = pcall(score_for_frame, f)
        st.score  = ok2 and score or score_v2(st.combo)
        console.set_cart_state(st)
        console.add_misc(st.score + ctx.step)
    end
    console.commit_frame()
end
