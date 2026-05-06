-- Spike N Stage 5 — Lua cart after edit l10 (function deleted, case v variant).
--
-- l10: update_npc_dialog is deleted entirely (not renamed).
-- Expected: FAIL-WITH-DIAGNOSTIC with "deleted" wording.
-- Surface: on_hot_reload_failed(slot, reason) fires.

console.set_on_hot_reload_failed(function(slot, reason)
    -- C side has already emitted the diagnostic to stderr.
    -- The Lua hook is a notification point; no additional action needed.
end)

local function score_for_combo(combo)
    return combo * 25
end

local game_cs = blyt32.coroutine.create(function(ctx)
    while ctx.step < 30 do
        ctx.step = ctx.step + 1
        ctx.angle = (ctx.step * 17) % 360
        coroutine.yield()
    end
end, { step = 0, angle = 0 })

-- update_npc_dialog has been DELETED.  No replacement exists.
-- The runtime detects the slot references a missing function.
-- We do NOT create a new npc_cs here — the slot from the pre-edit
-- cart is the one the runtime diagnoses.

local last  = console.num_frames()
local start = console.frame()

for f = start, last - 1 do
    local ok, ctx = blyt32.coroutine.resume(game_cs)
    if ok and ctx then
        local st = console.get_cart_state()
        st.step  = ctx.step
        st.combo = ctx.step % 10
        st.score = score_for_combo(st.combo)
        console.set_cart_state(st)
        console.add_misc(st.score + ctx.step)
    end
    console.commit_frame()
end
