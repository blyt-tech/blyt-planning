-- Spike N Stages 4-5 — Lua cart with coroutine in update_npc_dialog (baseline).
--
-- Extends det_lua_cart_pre with an update_npc_dialog() function that
-- runs a managed coroutine (yielding once per frame through a dialog
-- sequence).  Used as the PRE (save) side for edits l6-l10.
--
-- The coroutine yields inside update_npc_dialog(); edits l8-l10 target
-- that function to exercise cases (iv) and (v) from PLAN.md.

local function score_for_combo(combo)
    return combo * 25
end

-- NPC dialog coroutine — yields once per dialog line.
local NPC_DIALOG = {
    "Hello, traveller.",
    "The dungeon awaits.",
    "Beware the shadow.",
    "Good luck.",
}

local function update_npc_dialog(ctx)
    while ctx.dialog_step < #NPC_DIALOG do
        ctx.dialog_step = ctx.dialog_step + 1
        -- In production this would render the line; here we just fold it.
        coroutine.yield()
    end
end

-- Managed coroutine for the main game loop.
local game_cs = blyt32.coroutine.create(function(ctx)
    while ctx.step < 30 do
        ctx.step = ctx.step + 1
        ctx.angle = (ctx.step * 17) % 360
        coroutine.yield()
    end
end, { step = 0, angle = 0 })

-- Managed coroutine for the NPC dialog.
local npc_cs = blyt32.coroutine.create(update_npc_dialog,
    { dialog_step = 0 })

local last  = console.num_frames()
local start = console.frame()

for f = start, last - 1 do
    local ok, ctx = blyt32.coroutine.resume(game_cs)
    blyt32.coroutine.resume(npc_cs)

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
