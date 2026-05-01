-- This script calls console.* to expose the same values
local v0 = console.unit_float()
local v1 = console.unit_float()
local v2 = console.unit_float()
-- print from Lua via io.write (stdout)
io.write(string.format("unit_float[0] = %.9g\n", v0))
io.write(string.format("unit_float[1] = %.9g\n", v1))
io.write(string.format("unit_float[2] = %.9g\n", v2))
local a0 = v2 * 6.2831853
local a0_after = a0 + 0.03
local vx0 = math.sin(a0_after)
local vy0 = math.cos(a0_after)
io.write(string.format("entity0 a_init=%.9g  a_after_turn=%.9g\n", a0, a0_after))
io.write(string.format("sinf(a0_after) = %.9g\n", vx0))
io.write(string.format("cosf(a0_after) = %.9g\n", vy0))
console.commit_frame()
