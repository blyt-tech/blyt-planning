-- Spike T stage 2: bridged scalar round-trip (>4 args, multiple returns)
-- plus typed-path regression, identical output on rv32 and WASM.

function init()
    local m = require("spike")
    local sum, n = m.sum5(1, 2, 3, 4, 5)
    blyt32.debug.print("spike_sum5:" .. sum .. "," .. n)
    blyt32.debug.print("spike_twice:" .. m.twice(21))
end

function update()
    blyt.quit()
end

function draw() end
