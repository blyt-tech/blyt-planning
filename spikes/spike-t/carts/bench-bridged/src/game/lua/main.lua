function init()
    local m = require("spike")
    local acc = 0
    for i = 1, 10000 do
        local s, n = m.bench(i, 2, 3, 4)
        acc = (acc + s + n) % 1000003
    end
    blyt32.debug.print("bench_bridged:" .. acc)
end

function update() blyt.quit() end
function draw() end
