-- Spike N Stage 4 — Lua cart after edit l7 (closure-in-ctx attempt, case iii).
--
-- l7: a PRE-edit cart variant that attempts to put a function closure
-- into the managed coroutine's ctx table.  Spike M's flattener already
-- throws BLYT_ERR_FLATTEN_UNSUPPORTED_TYPE at save time.
--
-- This POST file is notional — l7 is a PRE-side failure: the save
-- itself fails before the reload starts.  The PLAN documents this as
-- "PASS-on-FLATTEN-ERROR": the composition gate is that Spike M's
-- pre-existing guard makes the failure mode unreachable in POST.
--
-- For the harness, l7 POST = l6 POST (unchanged post code); the test
-- is run on the PRE side only.  This file is present for documentation.

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
        st.step  = ctx.step
        st.combo = ctx.step % 10
        st.score = score_for_combo(st.combo)
        console.set_cart_state(st)
        console.add_misc(st.score + ctx.step)
    end
    console.commit_frame()
end
