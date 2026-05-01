-- entity_update.lua — the "real" workload for spike B.
--
-- Models a typical retro-game update() function: a small fixed-size pool of
-- entities, each with position, velocity, hp, alive flag.  Each frame steps
-- positions and runs an AABB bounds check.  The success criterion is that
-- one *frame* (one call to update()) completes well inside 16.7 ms on a Pi
-- Zero 2 W under the chosen MIPS cap.
--
-- The cart driver (lua_cart.c) re-runs this whole script as one "iteration"
-- — so the FRAME line it prints corresponds to running update() NFRAMES
-- times.  Divide the reported microseconds by NFRAMES to get per-update
-- (per-game-frame) cost.
--
-- N_ENTITIES is the parameter to vary (64, 256, 1024).  Default is 64.

local N_ENTITIES = N_ENTITIES or 64
local NFRAMES    = 100      -- enough that timing is meaningful, not so many
                            -- that the cart iteration itself blows the budget

local entities = {}
local seed = 12345
local function pseudo_rand()
    -- Simple LCG to avoid hitting math.random's GC/state path during timing.
    seed = (seed * 1103515245 + 12345) & 0x7fffffff
    return seed / 2147483647
end

for i = 1, N_ENTITIES do
    entities[i] = {
        x  = pseudo_rand() * 128.0,
        y  = pseudo_rand() * 128.0,
        vx = pseudo_rand() * 2.0 - 1.0,
        vy = pseudo_rand() * 2.0 - 1.0,
        hp = 10,
        alive = true,
    }
end

local function update(dt)
    for i = 1, N_ENTITIES do
        local e = entities[i]
        if e.alive then
            e.x = e.x + e.vx * dt
            e.y = e.y + e.vy * dt
            if e.x < 0 or e.x > 128 or e.y < 0 or e.y > 128 then
                -- bounce instead of kill, so the loop keeps doing real work
                e.vx = -e.vx
                e.vy = -e.vy
                if e.x < 0   then e.x = 0   end
                if e.x > 128 then e.x = 128 end
                if e.y < 0   then e.y = 0   end
                if e.y > 128 then e.y = 128 end
            end
        end
    end
end

local DT = 1.0 / 60.0
for _ = 1, NFRAMES do update(DT) end

-- Return a side effect so dead-code elimination cannot delete the loop.
local cx = 0
for i = 1, N_ENTITIES do cx = cx + entities[i].x end
return cx
