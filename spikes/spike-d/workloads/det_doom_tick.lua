-- det_doom_tick.lua — Spike D variant of doom_tick.lua.
--
-- Differences from spike-b's doom_tick.lua:
--   • PCG32 driven by the host runtime via console.rng()/console.unit_float();
--     the in-script LCG is gone.  Spike D's pass condition is bit-identity,
--     so the RNG state must be the cart-runtime's PCG32 (whose state lives
--     in frame_state and is part of the digest).
--   • Per-tic angle update with sin/cos/atan2 to exercise libm
--     transcendentals.  The angle drives an extra component of the AI
--     (each chasing mob also rotates a bit each tic), so divergence in
--     sinf/cosf/atan2f propagates into mob positions and therefore the
--     digest.
--   • console.frame() advances the frame and emits the DIGEST line.
--     The Lua script does NOT call print/io.write — the only stdout
--     output is the host-side DIGEST emission.
--   • NFRAMES is small (30) — enough to give the digest stream substance
--     without making the qemu-amd64-on-arm64 path too slow.

local sqrt  = math.sqrt
local sin   = math.sin
local cos   = math.cos
local atan2 = math.atan         -- Lua 5.4: math.atan(y, x) two-arg form
local floor = math.floor

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
local TURN_RATE          = 0.05  -- radians per tic when chasing

local N_MOBS  = 32   -- fits inside FRAME_STATE_MAX_MOBS = 64
local NFRAMES = 30

local mobs = {}
for i = 1, N_MOBS do
    mobs[i] = {
        x      = console.unit_float() * 128.0,
        y      = console.unit_float() * 128.0,
        vx     = 0.0, vy = 0.0,
        angle  = console.unit_float() * 6.2831853,
        hp     = 10,
        state  = STATE_IDLE,
        tics   = 0,
        alive  = true,
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

    -- Drive an angle update through atan2 + sin/cos so the libm path is
    -- in the hot loop.  The angle accumulator nudges the mob's velocity
    -- so divergence in atan2f or sinf/cosf shows up in m.x / m.y.
    local target_angle = atan2(dy, dx)
    -- shortest-arc lerp (toy: no wrap-around handling, fine for spike).
    m.angle = m.angle + (target_angle - m.angle) * TURN_RATE
    local s = sin(m.angle)
    local c = cos(m.angle)
    console.add_sin(s)
    console.add_cos(c)
    console.add_sqrt(dist)

    if m.state == STATE_IDLE then
        if dist < SIGHT_RANGE then
            m.state = STATE_CHASE
            m.tics  = 0
        end
    elseif m.state == STATE_CHASE then
        if dist < ATTACK_RANGE then
            m.state = STATE_ATTACK
            m.tics  = 0
        else
            m.vx = c * CHASE_SPEED
            m.vy = s * CHASE_SPEED
            m.x  = m.x + m.vx
            m.y  = m.y + m.vy
        end
    elseif m.state == STATE_ATTACK then
        if dist > ATTACK_RANGE then
            m.state = STATE_CHASE
        elseif (m.tics % ATTACK_PERIOD) == 0 then
            spawn_projectile(m)
        end
    elseif m.state == STATE_DEAD then
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
    if console.unit_float() < 0.05 then
        local idx = floor(console.unit_float() * N_MOBS) + 1
        local m = mobs[idx]
        if m.alive then m.state = STATE_DEAD end
    end
end

for f = 1, NFRAMES do
    for i = 1, N_MOBS do tick_mob(mobs[i]) end
    tick_projectiles()
    maybe_kill_one()

    -- Publish a slice of the cart's state into frame_state.  The
    -- console.set_mob() entry is responsible for canonicalizing NaN
    -- on write; mob fields with no live mob (alive=false → x/y/vx/vy
    -- frozen at their last value) still go in so the digest captures
    -- "slot is dead" via mob.state.
    for i = 1, N_MOBS do
        local m = mobs[i]
        console.set_mob(i - 1, m.x, m.y, m.vx, m.vy, m.state)
    end
    console.commit_frame()
end
