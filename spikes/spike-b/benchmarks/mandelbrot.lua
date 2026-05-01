-- mandelbrot.lua (adapted; pure float arithmetic, no transcendentals)
-- Counts in-set pixels.  Avoids math.* calls so it runs even with stub libm.

local W, H = 64, 64       -- 64x64 image; small for the spike
local ITER = 50
local sum = 0
for y = 0, H - 1 do
    local ci = 2.0 * y / H - 1.0
    for x = 0, W - 1 do
        local cr = 2.0 * x / W - 1.5
        local zr, zi = 0.0, 0.0
        local i = 0
        while i < ITER do
            local zr2 = zr * zr
            local zi2 = zi * zi
            if zr2 + zi2 > 4.0 then break end
            zi = 2.0 * zr * zi + ci
            zr = zr2 - zi2 + cr
            i = i + 1
        end
        if i == ITER then sum = sum + 1 end
    end
end
return sum
