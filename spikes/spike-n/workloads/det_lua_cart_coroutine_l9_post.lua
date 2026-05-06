-- Spike N Stage 5 — Lua cart after edit l9 (function rename, case v).
--
-- l9: update_npc_dialog renamed to update_dialog.
-- The npc_cs coroutine was created with update_npc_dialog as its body;
-- on reload, the slot table still references the old function name.
-- Expected: FAIL-WITH-DIAGNOSTIC via on_hot_reload_failed.
--
-- The on_hot_reload_failed hook is registered here to print the reason
-- to stderr for the harness to compare cross-host.

-- Register failure hook FIRST so it fires if the runtime detects
-- the stale slot before reaching the main loop.
console.set_on_hot_reload_failed(function(slot, reason)
    -- Print to stderr via printf — the harness greps for this.
    -- (In freestanding Lua, io.stderr is unavailable; we use
    --  blyt32.print_stderr if available, otherwise rely on the
    --  diagnostic already being on stderr from the C side.)
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

-- update_npc_dialog has been RENAMED to update_dialog.
-- The slot that was created with the old name cannot bind to this.
local function update_dialog(ctx)   -- was update_npc_dialog
    local NPC = { "Hello.", "Goodbye.", "Farewell." }
    while ctx.dialog_step < #NPC do
        ctx.dialog_step = ctx.dialog_step + 1
        coroutine.yield()
    end
end

-- New coroutine uses the renamed function.  The OLD slot (npc_cs
-- from the pre-edit cart) holds a reference to "update_npc_dialog"
-- which no longer exists — the runtime surfaces the diagnostic.
local npc_cs = blyt32.coroutine.create(update_dialog, { dialog_step = 0 })

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
