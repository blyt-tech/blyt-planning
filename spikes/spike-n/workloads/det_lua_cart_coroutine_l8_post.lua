-- Spike N Stage 4 — Lua cart after edit l8 (body change to update_npc_dialog, case iv).
--
-- l8: update_npc_dialog body changed — more lines in NPC_DIALOG.
-- The managed coroutine's ctx is POD (dialog_step: int); the new body's
-- loop condition fast-forwards to ctx.dialog_step on resume.
-- Expected: PASS — managed coroutine resumes against new body.

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

-- New body for update_npc_dialog: additional dialog lines (body changed).
local NPC_DIALOG_NEW = {
    "Hello, traveller.",
    "The dungeon awaits.",
    "Beware the shadow.",
    "Good luck.",
    "May your aim be true.",   -- added
    "Return victorious.",       -- added
}

local function update_npc_dialog(ctx)
    while ctx.dialog_step < #NPC_DIALOG_NEW do
        ctx.dialog_step = ctx.dialog_step + 1
        coroutine.yield()
    end
end

local npc_cs = blyt32.coroutine.create(update_npc_dialog, { dialog_step = 0 })

local last  = console.num_frames()
local start = console.frame()

for f = start, last - 1 do
    local ok, ctx = blyt32.coroutine.resume(game_cs)
    local ok2, nctx = blyt32.coroutine.resume(npc_cs)

    if ok and ctx then
        local st = console.get_cart_state()
        st.step  = ctx.step
        st.combo = ctx.step % 10
        st.score = score_for_combo(st.combo)
        console.set_cart_state(st)
        local nstep = (ok2 and nctx) and nctx.dialog_step or 0
        console.add_misc(st.score + ctx.step + nstep)
    end
    console.commit_frame()
end
