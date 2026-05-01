-- Diagnostic: compare sinf/cosf/atanf results between WASM and native

local function float_bits(x)
    -- Use string.pack to get IEEE 754 bits
    local s = string.pack("<f", x)
    local b0, b1, b2, b3 = string.byte(s, 1, 4)
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)
end

-- First few unit_floats from PCG32
local vals = {}
for i = 1, 5 do
    vals[i] = console.unit_float()
    local x = vals[i]
    -- Use console.add_misc to "report" the value (gets into frame_state)
    console.add_misc(x)
    print(string.format("unit_float[%d] = %.10g (bits=%08x)", i-1, x, float_bits(x)))
end

-- Test specific sin/cos/atan2 values
local test_angles = {0.3, 1.2, 2.5, -0.8, 3.14159}
for i, a in ipairs(test_angles) do
    local s = math.sin(a)
    local c = math.cos(a)
    local at = math.atan(1.0, a)
    print(string.format("sin(%g)=%g bits=%08x  cos=%g bits=%08x  atan2(1,%g)=%g bits=%08x",
        a, s, float_bits(s), c, float_bits(c), a, at, float_bits(at)))
end

-- Simulate first frame of det_doom_tick
local PLAYER_X, PLAYER_Y = 64.0, 64.0
local TURN_RATE = 0.05
local N_MOBS = 32
local sqrt = math.sqrt
local sin  = math.sin
local cos  = math.cos
local atan2 = math.atan

-- Re-init PCG32 via commit_frame (this commits current state)
-- Actually just compute accumulators directly
local xs = {}
local ys = {}
local angles = {}
for i = 1, N_MOBS do
    xs[i] = console.unit_float() * 128.0
    ys[i] = console.unit_float() * 128.0
    angles[i] = console.unit_float() * 6.2831853
end

local accum_sin = 0.0
local accum_cos = 0.0
local accum_sqrt = 0.0

for i = 1, N_MOBS do
    local dx = PLAYER_X - xs[i]
    local dy = PLAYER_Y - ys[i]
    local dist = sqrt(dx*dx + dy*dy)
    local target_angle = atan2(dy, dx)
    angles[i] = angles[i] + (target_angle - angles[i]) * TURN_RATE
    local s = sin(angles[i])
    local c = cos(angles[i])
    accum_sin = accum_sin + s
    accum_cos = accum_cos + c
    accum_sqrt = accum_sqrt + dist
end

print(string.format("accum_sin=%g (bits=%08x)", accum_sin, float_bits(accum_sin)))
print(string.format("accum_cos=%g (bits=%08x)", accum_cos, float_bits(accum_cos)))
print(string.format("accum_sqrt=%g (bits=%08x)", accum_sqrt, float_bits(accum_sqrt)))
