-- doom_tick_gc.lua — same as doom_tick.lua, but forces a full GC after
-- every game tick.
--
-- This isolates the GC-pause variance question:
--   - doom_tick.lua    : let Lua 5.4's incremental collector schedule itself.
--                        Mean is fast; max-tic is bounded by the size of an
--                        opportunistic collection that happens to land on a
--                        tic.
--   - doom_tick_gc.lua : pay a small GC cost on every tic, hopefully
--                        eliminating the worst-case-tic outlier.  Higher
--                        mean, lower variance is the expected trade.
--
-- Both files are otherwise byte-for-byte equivalent so the comparison is
-- clean.  When forced GC works for a workload at the projected Pi speed,
-- it's the simplest mitigation for frame-drop risk.

local sqrt  = math.sqrt
local floor = math.floor
local gc    = collectgarbage

local STATE_IDLE   = 1
local STATE_CHASE  = 2
local STATE_ATTACK = 3
local STATE_DEAD   = 4

local PLAYER_X, PLAYER_Y = 64.0, 64.0
local SIGHT_RANGE        = 40.0
local ATTACK_RANGE       = 12.0
local ATTACK_PERIOD      = 8
local CHASE_SPEED        = 1.5
local PROJECTILE_SPEED   = 4.0
local PROJECTILE_TTL     = 30

local N_MOBS  = N_MOBS or 64
local NFRAMES = 100

local seed = 12345
local function lrand()
    seed = (seed * 1103515245 + 12345) & 0x7fffffff
    return seed / 2147483647
end

local mobs = {}
for i = 1, N_MOBS do
    mobs[i] = {
        x  = lrand() * 128.0,
        y  = lrand() * 128.0,
        vx = 0.0, vy = 0.0,
        hp = 10,
        state = STATE_IDLE,
        tics  = 0,
        alive = true,
    }
end

local projectiles = {}

local function spawn_projectile(from)
    local dx = PLAYER_X - from.x
    local dy = PLAYER_Y - from.y
    local d  = sqrt(dx * dx + dy * dy)
    if d < 0.001 then d = 1.0 end
    projectiles[#projectiles + 1] = {
        x  = from.x, y = from.y,
        vx = dx / d * PROJECTILE_SPEED,
        vy = dy / d * PROJECTILE_SPEED,
        ttl = PROJECTILE_TTL,
    }
end

local function tick_mob(m)
    if not m.alive then return end
    m.tics = m.tics + 1

    local dx   = PLAYER_X - m.x
    local dy   = PLAYER_Y - m.y
    local dist = sqrt(dx * dx + dy * dy)

    local s = m.state
    if s == STATE_IDLE then
        if dist < SIGHT_RANGE then
            m.state = STATE_CHASE
            m.tics  = 0
        end
    elseif s == STATE_CHASE then
        if dist < ATTACK_RANGE then
            m.state = STATE_ATTACK
            m.tics  = 0
        else
            if dist > 0.001 then
                m.vx = dx / dist * CHASE_SPEED
                m.vy = dy / dist * CHASE_SPEED
            end
            m.x = m.x + m.vx
            m.y = m.y + m.vy
        end
    elseif s == STATE_ATTACK then
        if dist > ATTACK_RANGE then
            m.state = STATE_CHASE
        elseif (m.tics % ATTACK_PERIOD) == 0 then
            spawn_projectile(m)
        end
    elseif s == STATE_DEAD then
        m.alive = false
    end
end

local function tick_projectiles()
    local n = #projectiles
    for i = 1, n do
        local p = projectiles[i]
        p.x = p.x + p.vx
        p.y = p.y + p.vy
        p.ttl = p.ttl - 1
    end
    for i = n, 1, -1 do
        local p = projectiles[i]
        if p.ttl <= 0 or p.x < 0 or p.x > 128 or p.y < 0 or p.y > 128 then
            table.remove(projectiles, i)
        end
    end
end

local function maybe_kill_one()
    if lrand() < 0.02 then
        local idx = floor(lrand() * N_MOBS) + 1
        local m = mobs[idx]
        if m.alive then m.state = STATE_DEAD end
    end
end

for _ = 1, NFRAMES do
    for i = 1, N_MOBS do tick_mob(mobs[i]) end
    tick_projectiles()
    maybe_kill_one()
    gc("collect")    -- ← only difference from doom_tick.lua
end

local total = #projectiles
for i = 1, N_MOBS do total = total + (mobs[i].alive and 1 or 0) end
return total
