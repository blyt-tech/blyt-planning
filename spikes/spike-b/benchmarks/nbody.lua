-- nbody.lua (adapted; float physics loop, sqrt-only transcendental)
-- Five-body solar-system simulation with leapfrog-ish integration.

local PI            = 3.141592653589793
local SOLAR_MASS    = 4 * PI * PI
local DAYS_PER_YEAR = 365.24

local function body(x, y, z, vx, vy, vz, mass)
    return { x=x, y=y, z=z, vx=vx, vy=vy, vz=vz, mass=mass }
end

local sun     = body(0,0,0,0,0,0, SOLAR_MASS)
local jupiter = body(4.84143144246472090, -1.16032004402742839, -0.103622044471123109,
                     0.001660076642744037 * DAYS_PER_YEAR,
                     0.007699011114880199 * DAYS_PER_YEAR,
                    -0.000069035348994071 * DAYS_PER_YEAR,
                     0.000954791938424327 * SOLAR_MASS)
local saturn  = body( 8.34336671824457987, 4.12479856412430479, -0.403523417114321381,
                    -0.002767425107268624 * DAYS_PER_YEAR,
                     0.004998528012349636 * DAYS_PER_YEAR,
                     0.000023041729757119 * DAYS_PER_YEAR,
                     0.000285885980666131 * SOLAR_MASS)
local uranus  = body(12.89436956213813, -15.111151401698631, -0.223307985023811152,
                     0.002964601375647078 * DAYS_PER_YEAR,
                     0.002378485615231587 * DAYS_PER_YEAR,
                    -0.000029658956854023 * DAYS_PER_YEAR,
                     0.000043624040002107 * SOLAR_MASS)
local neptune = body(15.379697114850917, -25.91931460998796, 0.179258772950371643,
                     0.002680677724903893 * DAYS_PER_YEAR,
                     0.001628241700382423 * DAYS_PER_YEAR,
                    -0.000095159225451971 * DAYS_PER_YEAR,
                     0.000051513890204748 * SOLAR_MASS)

local bodies = { sun, jupiter, saturn, uranus, neptune }
local n      = #bodies

-- offset momentum so the centre of mass stays put
local px, py, pz = 0, 0, 0
for i = 1, n do
    px = px + bodies[i].vx * bodies[i].mass
    py = py + bodies[i].vy * bodies[i].mass
    pz = pz + bodies[i].vz * bodies[i].mass
end
bodies[1].vx = -px / SOLAR_MASS
bodies[1].vy = -py / SOLAR_MASS
bodies[1].vz = -pz / SOLAR_MASS

local sqrt = math.sqrt

local STEPS = 50
local DT    = 0.01

for _ = 1, STEPS do
    for i = 1, n - 1 do
        local bi = bodies[i]
        for j = i + 1, n do
            local bj = bodies[j]
            local dx = bi.x - bj.x
            local dy = bi.y - bj.y
            local dz = bi.z - bj.z
            local d2 = dx*dx + dy*dy + dz*dz
            local mag = DT / (d2 * sqrt(d2))
            bi.vx = bi.vx - dx * bj.mass * mag
            bi.vy = bi.vy - dy * bj.mass * mag
            bi.vz = bi.vz - dz * bj.mass * mag
            bj.vx = bj.vx + dx * bi.mass * mag
            bj.vy = bj.vy + dy * bi.mass * mag
            bj.vz = bj.vz + dz * bi.mass * mag
        end
    end
    for i = 1, n do
        local b = bodies[i]
        b.x = b.x + DT * b.vx
        b.y = b.y + DT * b.vy
        b.z = b.z + DT * b.vz
    end
end

local e = 0
for i = 1, n do
    local bi = bodies[i]
    e = e + 0.5 * bi.mass * (bi.vx*bi.vx + bi.vy*bi.vy + bi.vz*bi.vz)
    for j = i + 1, n do
        local bj = bodies[j]
        local dx = bi.x - bj.x
        local dy = bi.y - bj.y
        local dz = bi.z - bj.z
        local d  = sqrt(dx*dx + dy*dy + dz*dz)
        e = e - bi.mass * bj.mass / d
    end
end
return e
