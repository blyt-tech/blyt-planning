-- det_entity_update.lua — Spike D variant of entity_update.lua.
--
-- Pure-FP per-entity work with sin/cos angle update.  Every entity has a
-- scalar `angle` accumulator updated each tic; vx/vy come from sin/cos
-- of that angle.  The bounce-on-bounds logic is preserved so velocities
-- get sign-flipped occasionally (forces non-monotone state evolution).
--
-- All RNG comes from console.unit_float(); transcendentals from math.sin
-- / math.cos via the cart's musl libm.

local sin   = math.sin
local cos   = math.cos

local N_ENTITIES = 32
local NFRAMES    = 30
local DT         = 1.0 / 60.0
local TURN       = 0.03

local entities = {}
for i = 1, N_ENTITIES do
    entities[i] = {
        x  = console.unit_float() * 128.0,
        y  = console.unit_float() * 128.0,
        a  = console.unit_float() * 6.2831853,
        vx = 0.0, vy = 0.0,
    }
end

local function update()
    for i = 1, N_ENTITIES do
        local e = entities[i]
        e.a = e.a + TURN
        e.vx = sin(e.a)
        e.vy = cos(e.a)
        e.x  = e.x + e.vx
        e.y  = e.y + e.vy
        if e.x < 0 or e.x > 128 then e.a = -e.a;       e.vx = -e.vx end
        if e.y < 0 or e.y > 128 then e.a = 3.1415927 - e.a; e.vy = -e.vy end
        console.add_sin(e.vx)
        console.add_cos(e.vy)
    end
end

for f = 1, NFRAMES do
    update()
    for i = 1, N_ENTITIES do
        local e = entities[i]
        console.set_mob(i - 1, e.x, e.y, e.vx, e.vy, 1)
    end
    console.commit_frame()
end

local _ = DT
