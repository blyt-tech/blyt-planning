-- det_lua_simple.lua — Spike K Stage 2 demo workload.
--
-- Mirrors the production cart contract that ADR-0010 + ADR-0011 lay out:
-- persistent simulation state lives in declared C-side regions
-- (cart_state_lua_simple), accessed via the console.* typed-buffer API.
-- The Lua side carries no per-frame mutable state of its own — every
-- mutation goes through console.set_entity() / console.get_entity().
--
-- Cart contract:
--   • console.is_load_resume() → true iff this run was started by
--     save_state_load(); skip init in that case (cart_state was just
--     filled in by the deserializer).
--   • console.frame() → the current resume frame.  On a fresh run that
--     is 0; on a load it is the save frame, so the loop body runs from
--     `frame + 1` (load resume) or `0` (fresh init).
--   • console.num_frames() → the upper bound of the loop.  Set by the
--     C-side driver: NFRAMES on a fresh / load run; the save frame on
--     a save run (so the script returns control to main() after that
--     frame's commit_frame and the driver writes the buffer).

local sin   = math.sin
local cos   = math.cos
local TURN  = 0.03

local function init_entities()
    for i = 0, console.num_entities - 1 do
        local x = console.unit_float() * 128.0
        local y = console.unit_float() * 128.0
        local a = console.unit_float() * 6.2831853
        console.set_entity(i, x, y, a)
    end
end

local function update_entities()
    for i = 0, console.num_entities - 1 do
        local x, y, a = console.get_entity(i)
        a = a + TURN
        local vx = sin(a)
        local vy = cos(a)
        x = x + vx
        y = y + vy
        if x < 0 or x > 128 then a = -a;            vx = -vx end
        if y < 0 or y > 128 then a = 3.1415927 - a; vy = -vy end
        console.add_misc(vx + vy * 0.5)
        console.set_entity(i, x, y, a)
    end
end

if not console.is_load_resume() then
    init_entities()
end

-- console.frame() points at the next frame to compute.  On a fresh run
-- that's 0; on a load it's save_frame + 1 (commit_frame post-increments
-- fs.frame, so the buffer body carries N+1, not N).  The loop condition
-- gives a count-up from `start` to `num_frames - 1` inclusive.
local last  = console.num_frames()
local start = console.frame()

for f = start, last - 1 do
    update_entities()
    console.commit_frame()
end
